#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <ostream>
#include <string>

class DockingWindow
{
public:
	DockingWindow(const std::string& title, bool enable_settings = false);
	virtual ~DockingWindow() = default;

	void initialize(std::ostream* log);
	void update();
	void draw(const ImVec2& size);

	void open();
	void close();
	bool isOpen() const;
	bool& isOpenRef();
	bool isActive() const;

protected:
	void setWindowFlags(ImGuiWindowFlags flags);
	std::ostream& log() const;

	virtual void onInitialize() {}
	virtual void onUpdate() {}
	virtual void onDraw(const ImVec2& size) = 0;
	virtual void onSettingReadLine(void* entry, const char* line) {}
	virtual void onSettingWriteAll(ImGuiTextBuffer* buf) const {}

private:
	void syncOpenStateToSettings();
	void ensureStateInitialized() const;
	void syncOpenStateIfNeeded() const;

	static void* window_read_open(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name);
	static void window_readline(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line);
	static void window_write_all(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf);

	std::string m_title;
	ImGuiWindowFlags m_windowFlags = 0;
	bool m_hasSetting = false;

	bool m_isOpen = false;
	bool m_isActive = false;

	mutable bool m_stateInitialized = false;
	mutable bool m_lastSyncedOpen = false;

	std::ostream* m_log = nullptr;
};
