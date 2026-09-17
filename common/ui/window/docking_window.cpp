#include "docking_window.h"

#include <cassert>
#include <cstring>

DockingWindow::DockingWindow(const std::string& title, bool enable_settings) :
	m_title(title),
	m_windowFlags(),
	m_hasSetting(enable_settings),
	m_isOpen(false),
	m_isActive(false),
	m_stateInitialized(false),
	m_lastSyncedOpen(false),
	m_log(nullptr)
{
}

void DockingWindow::initialize(std::ostream* log)
{
	m_log = log;

	if (m_hasSetting)
	{
		ImGuiSettingsHandler ini_handler;
		ini_handler.TypeName = m_title.c_str();
		ini_handler.TypeHash = ImHashStr(m_title.c_str());
		ini_handler.ReadOpenFn = window_read_open;
		ini_handler.ReadLineFn = window_readline;
		ini_handler.WriteAllFn = window_write_all;
		ini_handler.UserData = this;

		ImGui::GetCurrentContext()->SettingsHandlers.push_back(ini_handler);
	}

	onInitialize();
}

void DockingWindow::update()
{
	onUpdate();
}

void DockingWindow::draw(const ImVec2& size)
{
	ensureStateInitialized();
	syncOpenStateIfNeeded();

	if (!m_isOpen)
	{
		m_isActive = false;
		return;
	}

	const bool was_open = m_isOpen;

	if (ImGui::Begin(m_title.c_str(), nullptr, m_windowFlags))
	{
		m_isActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (was_open != m_isOpen)
			syncOpenStateToSettings();

		onDraw(size);
	}
	else
	{
		m_isActive = false;
	}

	ImGui::End();

	if (was_open != m_isOpen)
		syncOpenStateToSettings();
}

void DockingWindow::open()
{
	ensureStateInitialized();
	m_isOpen = true;
	syncOpenStateToSettings();
}

void DockingWindow::close()
{
	ensureStateInitialized();
	m_isOpen = false;
	syncOpenStateToSettings();
}

bool DockingWindow::isOpen() const
{
	ensureStateInitialized();
	syncOpenStateIfNeeded();
	return m_isOpen;
}

bool& DockingWindow::isOpenRef()
{
	return m_isOpen;
}

bool DockingWindow::isActive() const
{
	return m_isActive;
}

void DockingWindow::setWindowFlags(ImGuiWindowFlags flags)
{
	m_windowFlags = flags;
}

std::ostream& DockingWindow::log() const
{
	assert(m_log != nullptr);
	return *m_log;
}

void DockingWindow::syncOpenStateToSettings()
{
	if (ImGui::GetCurrentContext() == nullptr)
		return;

	if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(m_title.c_str())))
	{
		settings->Collapsed = !m_isOpen;
		m_lastSyncedOpen = m_isOpen;
		ImGui::MarkIniSettingsDirty();
	}
}

void DockingWindow::ensureStateInitialized() const
{
	if (m_stateInitialized)
		return;

	if (ImGui::GetCurrentContext() == nullptr)
		return;

	if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(m_title.c_str())))
		const_cast<DockingWindow*>(this)->m_isOpen = !settings->Collapsed;

	m_lastSyncedOpen = m_isOpen;
	m_stateInitialized = true;
}

void DockingWindow::syncOpenStateIfNeeded() const
{
	if (!m_stateInitialized || m_lastSyncedOpen == m_isOpen)
		return;

	const_cast<DockingWindow*>(this)->syncOpenStateToSettings();
}

void* DockingWindow::window_read_open(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name)
{
	(void)ctx;
	auto& this_ptr = *reinterpret_cast<DockingWindow*>(handler->UserData);
	if (strcmp(name, this_ptr.m_title.c_str()) == 0)
		return handler->UserData;
	return nullptr;
}

void DockingWindow::window_readline(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line)
{
	(void)ctx;
	auto& this_ptr = *reinterpret_cast<DockingWindow*>(handler->UserData);
	this_ptr.onSettingReadLine(entry, line);
}

void DockingWindow::window_write_all(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
	(void)ctx;
	auto& this_ptr = *reinterpret_cast<DockingWindow*>(handler->UserData);

	buf->appendf("[%s][%s]\n", handler->TypeName, this_ptr.m_title.c_str());
	this_ptr.onSettingWriteAll(buf);
	buf->append("\n");
}
