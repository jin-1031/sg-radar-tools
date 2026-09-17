#include "control_window.h"
#include "../../core/playback_controller.h"
#include "../../device/mock_server.h"
#include "../../my_app.h"

#include <common/os/dialog.h>
#include <common/os/network.h>
#include <common/ui/imgui_custom.h>

#include <imgui.h>

#include <cstring>
#include <exception>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace
{
	constexpr float kFieldLabelWidth = 86.0f;

	void copyPathToBuffer(const std::filesystem::path& path, char* buffer, std::size_t buffer_size)
	{
		const std::string text = path.string();
		if (buffer_size == 0)
			return;

		std::strncpy(buffer, text.c_str(), buffer_size - 1);
		buffer[buffer_size - 1] = '\0';
	}

	const char* loopModeLabel(PlaybackController::LoopMode loop_mode)
	{
		switch (loop_mode)
		{
		case PlaybackController::LoopMode::RandomRestart:
			return "Random restart";
		case PlaybackController::LoopMode::RestartFromBeginning:
			return "Restart from beginning";
		case PlaybackController::LoopMode::RestartFromActivationStart:
			return "Restart from hold start";
		default:
			return "Unknown";
		}
	}

	std::string makeProgressText(int current_file, int total_files, const std::filesystem::path& file_path, bool completed)
	{
		std::ostringstream oss;
		oss << (completed ? "Loaded " : "Loading ")
			<< "(" << current_file << "/" << total_files << ") "
			<< file_path.filename().string();
		return oss.str();
	}

	void drawHeading(ImFont* font, const char* icon, const char* title)
	{
		font_guard guard(font);
		MyGui::Icon(icon);
		ImGui::SameLine();
		ImGui::TextUnformatted(title);
	}

	void drawSectionGap()
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	void beginField(const char* label)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(kFieldLabelWidth);
	}

	void drawField(const char* label, const char* value)
	{
		beginField(label);
		ImGui::TextUnformatted(value != nullptr ? value : "N/A");
	}

	void drawFieldWrapped(const char* label, const char* value)
	{
		beginField(label);
		ImGui::TextWrapped("%s", value);
	}
}

ControlWindow::ControlWindow() :
	DockingWindow("Control Panel"),
	m_fonts(MyApp::get().getWindowFont()),
	m_dataset(MyApp::get().getDataset()),
	m_controller(MyApp::get().getPlaybackController()),
	m_server(MyApp::get().getServer())
{
}

ControlWindow::~ControlWindow()
{
	if (m_loaderThread.joinable())
		m_loaderThread.join();
}

void ControlWindow::onUpdate()
{
	joinLoaderIfReady();
	applyPendingLoadResult();
}

void ControlWindow::onDraw(const ImVec2& size)
{
	(void)size;

	drawLoadingBanner();

	ImGui::BeginDisabled(isLoading());
	drawTopPanel();
	ImGui::EndDisabled();
}

void ControlWindow::drawTopPanel()
{
	style_var_guard item_spacing(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));

	drawServerSection();
	drawSectionGap();
	drawDatasetSection();
	if (m_controller == nullptr || m_dataset == nullptr || m_dataset->empty())
		return;

	drawSectionGap();
	drawPlaybackSection();
	drawSectionGap();
	drawStatusSection();
}

void ControlWindow::drawServerSection()
{
	drawHeading(m_fonts.headingFont, ICON_FA_SERVER, "Server");

	os::LocalNetworkInfo network_info;
	const bool has_network_info = os::getLocalNetworkInfo(network_info);
	const MockServer::Stats server_stats = m_server != nullptr ? m_server->getStats() : MockServer::Stats{};

	beginField("State");
	MyGui::Icon(
		server_stats.running ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE_XMARK,
		0.0f,
		server_stats.running ? IM_COL32(70, 210, 100, 255) : IM_COL32(230, 85, 85, 255));
	ImGui::SameLine();
	ImGui::TextUnformatted(server_stats.running ? "Running" : "Stopped");

	beginField("Clients");
	ImGui::Text("%d", server_stats.clientCount);
	beginField("HTTP");
	ImGui::Text("%u", server_stats.httpPort);
	beginField("Stream");
	ImGui::Text("%u", server_stats.devicePort);
	drawField("IP", has_network_info ? network_info.address.c_str() : "N/A");

	if (!server_stats.lastError.empty())
		drawFieldWrapped("Error", server_stats.lastError.c_str());
}

void ControlWindow::drawDatasetSection()
{
	drawHeading(m_fonts.headingFont, ICON_FA_FOLDER_OPEN, "Dataset");

	if (MyGui::IconButton("##OpenFolder", ICON_FA_FOLDER_OPEN, "Open Folder", ImVec2(150.0f, 0.0f)))
	{
		char selected_path[kFolderBufferSize] = {};
		std::strncpy(selected_path, m_folderBuffer, sizeof(selected_path) - 1);
		const auto result = os::openFolderDialog("Select Gesture Folder", selected_path, sizeof(selected_path));
		if (result == os::FileDialogResult::OK)
			startLoadingFolder(selected_path);
		else if (result == os::FileDialogResult::ErrorUnknown)
			log() << "[error] dataset: failed to open folder dialog\n";
	}

	beginField("Folder");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputText("##Folder", m_folderBuffer, sizeof(m_folderBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
		startLoadingFolder(m_folderBuffer);

	if (m_dataset == nullptr || m_dataset->empty())
	{
		ImGui::Spacing();
		ImGui::TextUnformatted("Load a folder containing .pcr files.");
		return;
	}

	beginField("Classes");
	ImGui::Text("%zu", m_dataset->getRecords().size());
	drawField("Folder", m_dataset->getFolder().string().c_str());
}

void ControlWindow::drawPlaybackSection()
{
	drawHeading(m_fonts.headingFont, ICON_FA_PLAY, "Playback");

	const std::string& default_name = m_controller->getDefaultRecordName();
	const auto* current_record = m_dataset->findRecord(default_name);
	const std::string preview_text = current_record != nullptr ? current_record->name : std::string("None");

	ImGui::TextUnformatted("Default record");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##DefaultRecord", preview_text.c_str()))
	{
		for (const auto& record : m_dataset->getRecords())
		{
			if (record.totalFrames == 0)
				continue;

			const bool selected = record.name == default_name;
			if (ImGui::Selectable(record.name.c_str(), selected))
				m_controller->setDefaultRecordName(record.name);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	float fps = m_controller->getFps();
	ImGui::TextUnformatted("FPS");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputFloat("##FPS", &fps, 1.0f, 5.0f, "%.1f"))
		m_controller->setFps(fps);

	int loop_mode = static_cast<int>(m_controller->getLoopMode());
	static const char* kLoopModeItems =
		"Random restart\0"
		"Restart from beginning\0"
		"Restart from hold start\0";
	ImGui::TextUnformatted("Loop mode");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##LoopMode", &loop_mode, kLoopModeItems))
		m_controller->setLoopMode(static_cast<PlaybackController::LoopMode>(loop_mode));
}

void ControlWindow::drawStatusSection()
{
	drawHeading(m_fonts.headingFont, ICON_FA_SIGNAL, "Status");

	const PlaybackController::Output& output = m_controller->getLastOutput();
	const PcDatasetFrameRef* active_ref = nullptr;
	const PointCloudRecorder::RecordSession* active_session = nullptr;
	if (output.valid)
	{
		if (const PcDatasetRecord* active_record = m_dataset->findRecord(output.recordName))
		{
			active_ref = active_record->getFrameRef(output.logicalFrameIndex);
			active_session = active_record->getSession(output.logicalFrameIndex);
		}
	}

	if (active_ref != nullptr)
	{
		beginField("Session");
		ImGui::Text("S%zu", active_ref->sessionOrdinal + 1);
	}
	else
	{
		drawField("Session", "N/A");
	}

	if (active_ref != nullptr && active_session != nullptr)
	{
		beginField("Frame");
		ImGui::Text("%zu/%zu", active_ref->frameIndex + 1, active_session->frames.size());
	}
	else
	{
		drawField("Frame", "N/A");
	}

	if (output.valid)
	{
		beginField("Global");
		ImGui::Text("%zu/%zu", output.logicalFrameIndex + 1, output.totalFrames);
		beginField("Stream");
		ImGui::Text("%u", output.streamFrameCount);
	}
	else
	{
		drawField("Global", "N/A");
		drawField("Stream", "N/A");
	}

	drawField("Loop", loopModeLabel(m_controller->getLoopMode()));
	if (active_session != nullptr && !active_session->name.empty())
		drawFieldWrapped("Name", active_session->name.c_str());
}

void ControlWindow::drawLoadingBanner()
{
	if (!isLoading())
		return;

	std::string status_text;
	{
		std::lock_guard<std::mutex> lock(m_loadingMutex);
		status_text = m_loadingStatus;
	}

	{
		font_guard guard(m_fonts.headingFont);
		MyGui::Icon(ICON_FA_SPINNER);
		ImGui::SameLine();
		ImGui::TextUnformatted("Loading Dataset");
	}

	ImGui::TextWrapped("%s", status_text.c_str());
	ImGui::Separator();
	ImGui::Spacing();
}

bool ControlWindow::startLoadingFolder(const char* folder_path)
{
	if (m_dataset == nullptr || m_controller == nullptr || folder_path == nullptr || folder_path[0] == '\0' || isLoading())
		return false;

	if (m_loaderThread.joinable())
		m_loaderThread.join();

	const std::filesystem::path requested_folder = folder_path;
	m_loadingActive = true;
	m_loadingReady = false;

	{
		std::lock_guard<std::mutex> lock(m_loadingMutex);
		m_pendingLoadResult.reset();
		m_loadingStatus = "Preparing load...";
	}

	log() << "[info] dataset: scanning " << requested_folder.string() << "\n";

	m_loaderThread = std::thread([this, requested_folder]()
	{
		auto result = std::make_unique<LoadResult>();
		result->folderPath = requested_folder;

		try
		{
			result->success = result->dataset.loadFromFolder(requested_folder, result->error,
				[this](int current_file, int total_files, const std::filesystem::path& current_path, bool completed)
				{
					const std::string progress_text = makeProgressText(current_file, total_files, current_path, completed);
					{
						std::lock_guard<std::mutex> lock(m_loadingMutex);
						m_loadingStatus = progress_text;
					}
					log() << "[info] dataset: " << progress_text << "\n";
				});
		}
		catch (const std::exception& exception)
		{
			result->error = exception.what();
			result->success = false;
		}

		{
			std::lock_guard<std::mutex> lock(m_loadingMutex);
			m_pendingLoadResult = std::move(result);
		}

		m_loadingReady = true;
		m_loadingActive = false;
	});

	return true;
}

void ControlWindow::applyPendingLoadResult()
{
	if (!m_loadingReady.load() || m_dataset == nullptr || m_controller == nullptr)
		return;

	std::unique_ptr<LoadResult> result;
	{
		std::lock_guard<std::mutex> lock(m_loadingMutex);
		result = std::move(m_pendingLoadResult);
		m_loadingStatus = "Idle";
	}
	m_loadingReady = false;

	if (!result)
		return;

	if (!result->success)
	{
		log() << "[error] dataset: " << result->error << "\n";
		return;
	}

	const std::string previous_default_name = m_controller->getDefaultRecordName();
	*m_dataset = std::move(result->dataset);
	copyPathToBuffer(m_dataset->getFolder(), m_folderBuffer, sizeof(m_folderBuffer));

	m_controller->setDataset(m_dataset);
	if (m_dataset->hasRecord(previous_default_name))
		m_controller->setDefaultRecordName(previous_default_name);
	else
		m_controller->setDefaultRecordName(m_dataset->getFirstAvailableRecordName());

	log() << "[info] dataset: completed loading " << result->folderPath.string() << "\n";
}

void ControlWindow::joinLoaderIfReady()
{
	if (m_loaderThread.joinable() && !m_loadingActive.load())
		m_loaderThread.join();
}

bool ControlWindow::isLoading() const
{
	return m_loadingActive.load();
}
