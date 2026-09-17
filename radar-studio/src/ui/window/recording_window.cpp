#include "recording_window.h"
#include "../../my_app.h"
#include "../../device/retina.h"

#include <common/os/dialog.h>
#include <common/ui/imgui_custom.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
	static std::string make_timestamp_name()
	{
		using namespace std::chrono;
	
		const auto now = system_clock::now();
		const std::time_t now_time = system_clock::to_time_t(now);
		std::tm local_tm{};
	#ifdef _WIN32
		localtime_s(&local_tm, &now_time);
	#else
		local_tm = *std::localtime(&now_time);
	#endif
		std::ostringstream oss;
		oss << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
		return oss.str();
	}
	
	static std::string format_output_path(std::string directory, std::string file_name)
	{
		namespace fs = std::filesystem;
	
		if (directory.empty())
			directory = ".";
		if (file_name.empty())
			file_name = make_timestamp_name();
	
		fs::path dir_path(directory);
		fs::path base_path = dir_path / file_name;
		fs::path candidate = base_path;
		candidate += ".pcr";
	
		int suffix = 1;
		while (fs::exists(candidate))
		{
			candidate = (dir_path / (file_name + "(" + std::to_string(suffix) + ")"));
			candidate += ".pcr";
			++suffix;
		}
	
		return candidate.string();
	}
}

RecordingWindow::RecordingWindow() :
	DockingWindow("Recording")
{
	auto* session = m_recorder.addSession();

	std::strcpy(m_folderNameBuf, "../../recordings");
	std::strcpy(m_fileNameBuf, "");
	std::strcpy(m_tagBuf, session->tag.c_str());
	std::strcpy(m_renameBuf, "");

	m_currSessionIdx = 0;
	m_currSession = session;
}

PointCloudRecorder& RecordingWindow::getRecorder()
{
	return m_recorder;
}

const PointCloudRecorder::RecordSession* RecordingWindow::getCurrentSession() const
{
	return m_currSession;
}

void RecordingWindow::onUpdate()
{
	if (MyApp::get().getDeviceInfoUpdated())
		m_recorder.setSensorSpec(MyApp::get().getDeviceInfo().sensorSpec);

	if (!m_pendingOpenPath.empty())
	{
		onFileOpen(m_pendingOpenPath.c_str());
		m_pendingOpenPath.clear();
	}
	if (!m_pendingImportPath.empty())
	{
		onImportFile(m_pendingImportPath.c_str());
		m_pendingImportPath.clear();
	}
	if (m_hasPendingDelete)
	{
		onDeleteSessionButtonClicked();
		m_hasPendingDelete = false;
	}
}

void RecordingWindow::onDraw(const ImVec2& size)
{
	const float content_width = ImGui::GetContentRegionAvail().x;

	drawFileInfo(content_width);
	drawSessionList(content_width);
	drawSessionInfo(content_width);
}

void RecordingWindow::onFolderOpen(const char* dir)
{
	std::strcpy(m_folderNameBuf, dir);
}

void RecordingWindow::onFileOpen(const char* path)
{
	std::ifstream file(path, std::ios::binary);

	PointCloudRecorder new_recorder;
	
	if (new_recorder.deserializeFromStream(file))
	{
		m_recorder = std::move(new_recorder);

		log() << "[info] recording: successfully loaded file: " << path << "\n";

		if (!m_recorder.empty())
		{
			m_currSession = m_recorder.getSessionByIndex(0);
			m_currSessionIdx = 0;
			m_currFrameIdx = 0;
		}
		else
		{
			m_currSession = nullptr;
			m_currSessionIdx = -1;
			m_currFrameIdx = -1;
		}

		namespace fs = std::filesystem;

		const auto file_path = fs::path(path);
		const auto working_dir = fs::current_path();
		const auto parent = fs::relative(file_path.parent_path(), working_dir);
		const auto filename = file_path.filename().replace_extension();

		std::strcpy(m_folderNameBuf, parent.string().c_str());
		std::strcpy(m_fileNameBuf, filename.string().c_str());
	}
	else
	{
		log() << "[error] recording: the file: " << path << " is invalid or corrupted\n";
	}
}

void RecordingWindow::onImportFile(const char* path)
{
	std::ifstream file(path, std::ios::binary);

	PointCloudRecorder new_recorder;
	
	if (new_recorder.deserializeFromStream(file))
	{
		m_recorder.importFrom(std::move(new_recorder));

		log() << "[info] recording: successfully imported file: " << path << "\n";

		if (!m_recorder.empty())
		{
			m_currSession = m_recorder.getSessionByIndex(0);
			m_currSessionIdx = 0;
			m_currFrameIdx = 0;
		}
		else
		{
			m_currSession = nullptr;
			m_currSessionIdx = -1;
			m_currFrameIdx = -1;
		}
	}
	else
	{
		log() << "[error] recording: the file: " << path << " is invalid or corrupted\n";
	}
}

void RecordingWindow::onFileSave(const char* path)
{
	namespace fs = std::filesystem;

	fs::path out_path(path);
	fs::path parent = out_path.parent_path();
	
	if (!parent.empty())
	{
		if (!fs::exists(parent))
			log() << "[info] recording: creating output directories: " << parent << "\n";

		std::error_code ec;
		fs::create_directories(parent, ec);
		if (ec) log() << "[error] recording: failed to create directories: " << ec.message() << "\n";
	}
}

void RecordingWindow::onAddSessionButtonClicked()
{
	m_isRenaming = false;

	const int session_count = static_cast<int>(m_recorder.getSessionCount());

	m_currSession = m_recorder.addSession();
	m_currSessionIdx = m_recorder.getOrderedSessionIndex(m_currSession->id);
	m_currFrameIdx = 0;

	m_currSession->tag = m_tagBuf;
	std::strcpy(m_descriptionBuf, "");
}

void RecordingWindow::onDeleteSessionButtonClicked()
{
	m_isRenaming = false;

	const int session_count = static_cast<int>(m_recorder.getSessionCount());
	const SessionID id = m_recorder.getOrderedSessionID(m_currSessionIdx);

	m_recorder.removeSession(id);

	if (session_count > 0)
		onSessionSelected(std::min(m_currSessionIdx, session_count - 2));
	else
		onSessionSelected(-1);
}

void RecordingWindow::onCopySessionButtonClicked()
{
	auto session_id = m_recorder.getOrderedSessionID(m_currSessionIdx);
	m_recorder.copySession(session_id);
}

void RecordingWindow::onSessionSelected(int idx)
{
	if (idx != -1)
	{
		m_currSessionIdx = idx;
		m_currSession = m_recorder.getSessionByIndex(idx);
		m_currFrameIdx = 0;

		std::strcpy(m_descriptionBuf, m_currSession->description.c_str());
		std::strcpy(m_tagBuf, m_currSession->tag.c_str());
	}
	else
	{
		m_currSession = nullptr;
		m_currSessionIdx = -1;
		m_currFrameIdx = -1;

		std::strcpy(m_descriptionBuf, "");
		std::strcpy(m_tagBuf, "");
	}
}

void RecordingWindow::drawFileInfo(float content_width)
{
	const auto& style = ImGui::GetStyle();
	const float window_width = ImGui::GetWindowWidth();

	// Folder
	const float folder_btn_width = 180.0f;

	ImGui::SeparatorText("Folder: ");
	ImGui::TextUnformatted("Folder Name:");
	ImGui::SetNextItemWidth(content_width);
	ImGui::InputText("##Folder Name", m_folderNameBuf, sizeof(m_folderNameBuf));
	
	ImGui::SetCursorPosX(window_width - folder_btn_width - style.WindowPadding.x);
	if (ImGui::Button("Open Folder", ImVec2(folder_btn_width, 0.0f)))
	{
		char dir[kFolderNameSize];
		std::strcpy(dir, m_folderNameBuf);

		auto res = os::openFolderDialog("Select Folder", dir, sizeof(dir));
		if (res == os::FileDialogResult::OK)
			onFolderOpen(dir);
		else if (res == os::FileDialogResult::Canceled)
			log() << "[info] recording: folder open canceled\n";
		else // Error
			log() << "[error] recording: failed to open folder: " << dir << "\n";
	}

	ImGui::SetCursorPosX(window_width - folder_btn_width - style.WindowPadding.x);
	if (ImGui::Button("Open in explorer", ImVec2(folder_btn_width, 0.0f)))
		os::openFolderInExplorer(m_folderNameBuf);

	// File
	const float file_btn_width = 80.0f;

	ImGui::SeparatorText("File: ");
	ImGui::TextUnformatted("File Name:");
	ImGui::SetNextItemWidth(content_width);
	ImGui::InputText("##File Name", m_fileNameBuf, sizeof(m_fileNameBuf));
	
	auto resolved = format_output_path(m_folderNameBuf, m_fileNameBuf);
	ImGui::TextUnformatted("file will be saved as:");
	ImGui::TextWrapped(resolved.c_str());

	ImGui::SetCursorPosX(window_width - 3.0f * file_btn_width - 2.0f * style.ItemSpacing.x - style.WindowPadding.x);

	if (ImGui::Button("Open", ImVec2(file_btn_width, 0.0f)))
	{
		const char* title = "Open File";
		const char* filter = "Point Cloud Recording (*.pcr)|*.pcr";

		char path[512];
		std::strcpy(path, resolved.c_str());

		auto res = os::openFileDialog(title, filter, path, sizeof(path));
		if (res == os::FileDialogResult::OK)
			m_pendingOpenPath = path;
		else if (res == os::FileDialogResult::Canceled)
			log() << "[info] recording: file open canceled\n";
		else // Error
			log() << "[error] recording: failed to open file: " << path << "\n";
	}

	ImGui::SameLine();
	if (ImGui::Button("Import", ImVec2(file_btn_width, 0.0f)))
	{
		const char* title = "Import File";
		const char* filter = "Point Cloud Recording (*.pcr)|*.pcr";

		char path[512];
		std::strcpy(path, resolved.c_str());

		auto res = os::openFileDialog(title, filter, path, sizeof(path));
		if (res == os::FileDialogResult::OK)
			m_pendingImportPath = path;
		else if (res == os::FileDialogResult::Canceled)
			log() << "[info] recording: file import canceled\n";
		else // Error
			log() << "[error] recording: failed to import file: " << path << "\n";
	}
	
	ImGui::SameLine();
	if (ImGui::Button("Save", ImVec2(file_btn_width, 0.0f)))
	{
		try
		{
			onFileSave(resolved.c_str());
		}
		catch (const std::exception& e)
		{
			log() << "[error] recording: exception while creating directories: " << e.what() << "\n";
		}

		std::ofstream file(resolved, std::ios::binary);
		if (file)
		{
			m_recorder.serializeToStream(file);
			log() << "[info] recording saved to " << resolved << "\n";
		}
		else
		{
			log() << "[error] recording: failed to create output file: " << resolved << "\n";
		}
	}
}

void RecordingWindow::drawSessionList(float content_width)
{
	ImGui::SeparatorText("Session List:");
	const float action_button_width = 26.0f;
	const bool can_delete = m_recorder.getSessionCount() > 0;

	if (ImGui::Button("+", ImVec2(action_button_width, 0.0f)))
		onAddSessionButtonClicked();

	ImGui::SameLine();
	ImGui::BeginDisabled(!can_delete);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 60, 60, 255));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(180, 50, 50, 255));
	if (ImGui::Button("-", ImVec2(action_button_width, 0.0f)) && can_delete)
		m_hasPendingDelete = true;
	ImGui::PopStyleColor(2);
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(20.0f, 0.0f));
	ImGui::SameLine();
	ImGui::BeginDisabled(!can_delete);
	if (ImGui::Button("copy"))
		onCopySessionButtonClicked();
	ImGui::EndDisabled();

	const ImGuiStyle& style = ImGui::GetStyle();
	const float button_spacing = style.ItemSpacing.y;
	const float list_height = 120.0f;
	const float button_width = 28.0f;
	const float list_width = std::max(1.0f, content_width - button_width - 8.0f);
	const float button_height = (list_height - button_spacing) * 0.5f;
	const int session_count = static_cast<int>(m_recorder.getSessionCount());

	ImGui::BeginGroup();
	ImGui::SetNextItemWidth(content_width);
	if (ImGui::BeginChild("##RecordingList", ImVec2(list_width, list_height), ImGuiChildFlags_Borders))
	{
		for (int i = 0; i < session_count; ++i)
		{
			const bool selected = (i == m_currSessionIdx);
			auto* session = m_recorder.getSessionByIndex(i);

			if (selected && m_isRenaming)
			{
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (m_renamePendingFocus)
				{
					ImGui::SetKeyboardFocusHere();
					m_renamePendingFocus = false;
				}

				const auto input_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
				ImGui::InputText(("##Rename" + std::to_string(i)).c_str(), m_renameBuf, sizeof(m_renameBuf), input_flags);
				
				if (ImGui::IsItemDeactivated())
				{
					session->name = m_renameBuf;
					m_isRenaming = false;
				}
			}
			else
			{
				const bool item_selected = ImGui::Selectable(session->name.c_str(), selected);

				if (item_selected)
				{
					onSessionSelected(i);
					m_isRenaming = false;
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_isRenaming = true;
					m_renamePendingFocus = true;
					std::strncpy(m_renameBuf, session->name.c_str(), sizeof(m_renameBuf) - 1);
					m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
				}
			}
		}
	}

	ImGui::EndChild();
	ImGui::EndGroup();

	const auto move_btn_size = ImVec2(button_width, button_height);
	const bool can_move_up = m_currSessionIdx > 0;
	const bool can_move_down = m_currSessionIdx >= 0 && m_currSessionIdx < session_count - 1;

	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::BeginDisabled(!can_move_up);
	if (MyGui::IconButton("##MoveUp", ICON_FA_ARROW_UP, move_btn_size))
		m_recorder.reorderSession(m_currSession->id, m_currSessionIdx -= 1);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!can_move_down);
	if (MyGui::IconButton("##MoveDown", ICON_FA_ARROW_DOWN, move_btn_size))
		m_recorder.reorderSession(m_currSession->id, m_currSessionIdx += 1);
	ImGui::EndDisabled();
	ImGui::EndGroup();
}

void RecordingWindow::drawSessionInfo(float content_width)
{
	ImGui::SeparatorText("Session:");
	if (m_currSession)
	{
		using timepoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
		auto tp = timepoint(std::chrono::milliseconds(m_currSession->timestamp));
		auto seconds = std::chrono::system_clock::to_time_t(tp);
		std::tm* lt = std::localtime(&seconds);

		char date_buf[64];
		char time_buf[64];

		std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", lt);
		std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", lt);

		uint64_t total_ms = (m_currSession->lengthUs / 1000) % 1000;
		uint64_t total_s = (m_currSession->lengthUs / 1000000) % 60;
		uint64_t total_m = (m_currSession->lengthUs / 60000000) % 60;

		ImGui::Text("ID    : %u", m_currSession->id);
		ImGui::Text("Frames: %zu", m_currSession->frames.size());
		ImGui::Text("Date  : %s", date_buf);
		ImGui::Text("Time  : %s", time_buf);
		ImGui::Text("Length: %02llu:%02llu.%03llu", total_m, total_s, total_ms);

		if (m_currSession->bytes > 1000000)
			ImGui::Text("Bytes : %.2f MB", m_currSession->bytes / 1e6f);
		else if (m_currSession->bytes > 1000)
			ImGui::Text("Bytes : %.2f KB", m_currSession->bytes / 1e3f);
		else
			ImGui::Text("Bytes : %zu B", m_currSession->bytes);
	}
	else
	{
		ImGui::TextUnformatted("ID    : N/A");
		ImGui::TextUnformatted("Frames: N/A");
		ImGui::TextUnformatted("Date  : N/A");
		ImGui::TextUnformatted("Time  : N/A");
		ImGui::TextUnformatted("Length: N/A");
		ImGui::TextUnformatted("Bytes : N/A");
	}

	ImGui::TextUnformatted("Tag:");
	ImGui::BeginDisabled(m_currSession == nullptr);
	ImGui::SetNextItemWidth(content_width);
	if (ImGui::InputText("##Tag", m_tagBuf, sizeof(m_tagBuf)))
	{
		if (m_currSession != nullptr)
			m_currSession->tag = m_tagBuf;
	}
	ImGui::EndDisabled();

	ImGui::TextUnformatted("Description:");
	ImGui::BeginDisabled(m_currSession == nullptr);
	ImGui::SetNextItemWidth(content_width);
	if (ImGui::InputTextMultiline("##Description", m_descriptionBuf, sizeof(m_descriptionBuf)))
	{
		if (m_currSession != nullptr)
			m_currSession->description = m_descriptionBuf;
	}
	ImGui::EndDisabled();
}
