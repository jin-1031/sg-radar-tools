#pragma once

#include "ui/window_font.h"

#include <common/util/debug_console.h>

#include <memory>
#include <vector>

struct GLFWwindow;

class DockingWindow;
class MockServer;
class PlaybackController;
class PointCloudDataset;

class MyApp
{
public:
	static MyApp& get();

	MyApp();
	~MyApp();

	void run();

	WindowFont getWindowFont() const;
	PointCloudDataset* getDataset();
	PlaybackController* getPlaybackController();
	MockServer* getServer();

private:
	void initWindow();
	void loadFonts();
	void setupWindows();
	void setupDockLayout(unsigned int dockspace_id);

	void updateWindows();
	void drawFrame();
	void limitFrameRate(float fps) const;

private:
	static void glfwErrorCallback(int error, const char* description);
	static void glfwIconifyCallback(GLFWwindow* window, int iconified);

private:
	static MyApp* s_instance;

	GLFWwindow* m_window = nullptr;
	bool m_windowMinimized = false;
	bool m_dockLayoutInitialized = false;

	std::unique_ptr<PointCloudDataset> m_dataset;
	std::unique_ptr<PlaybackController> m_controller;
	std::unique_ptr<MockServer> m_server;

	std::vector<std::unique_ptr<DockingWindow>> m_windows;
	WindowFont m_fonts;
	DebugConsole m_console;
};
