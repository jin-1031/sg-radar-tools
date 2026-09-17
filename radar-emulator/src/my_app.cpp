#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include "my_app.h"

#include "core/playback_controller.h"
#include "core/point_cloud_dataset.h"
#include "device/mock_server.h"
#include "ui/window/control_window.h"
#include "ui/window/launchpad_window.h"

#include <common/ui/imgui_custom.h>
#include <common/ui/window/console_window.h>
#include <common/ui/window/docking_window.h>

#include <GLFW/glfw3.h>
#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#define FONT_AWESOME_PATH "../../asset/font/fa-solid-900.ttf"
#define CONSOLAS_FONT_PATH "C:/Windows/Fonts/consola.ttf"

MyApp* MyApp::s_instance = nullptr;

MyApp& MyApp::get()
{
	assert(s_instance != nullptr);
	return *s_instance;
}

MyApp::MyApp()
{
	s_instance = this;

	m_dataset = std::make_unique<PointCloudDataset>();
	m_controller = std::make_unique<PlaybackController>();
	m_server = std::make_unique<MockServer>(m_console.getOutput());

	initWindow();
	setupWindows();

	if (!m_server->start())
		m_console.getOutput() << "[error] mock server: failed to start\n";

	glfwShowWindow(m_window);
}

MyApp::~MyApp()
{
	m_server.reset();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(m_window);
	glfwTerminate();

	s_instance = nullptr;
}

void MyApp::run()
{
	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();

		if (m_windowMinimized)
		{
			glfwWaitEventsTimeout(0.01);
			continue;
		}

		updateWindows();
		drawFrame();

		limitFrameRate(60.0f);
	}
}

WindowFont MyApp::getWindowFont() const
{
	return m_fonts;
}

PointCloudDataset* MyApp::getDataset()
{
	return m_dataset.get();
}

PlaybackController* MyApp::getPlaybackController()
{
	return m_controller.get();
}

MockServer* MyApp::getServer()
{
	return m_server.get();
}

void MyApp::initWindow()
{
	glfwSetErrorCallback(glfwErrorCallback);

	if (!glfwInit())
		throw std::runtime_error("[glfw] failed to initialize");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	m_window = glfwCreateWindow(1280, 900, "Radar Emulator", nullptr, nullptr);
	if (m_window == nullptr)
		throw std::runtime_error("[glfw] failed to create window");

	glfwSetWindowUserPointer(m_window, this);
	glfwSetWindowIconifyCallback(m_window, glfwIconifyCallback);
	glfwMakeContextCurrent(m_window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	MyGui::applyTheme(MyGui::Theme::Dark);

	loadFonts();

	if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true))
		throw std::runtime_error("[imgui] failed to initialize GLFW backend");
	if (!ImGui_ImplOpenGL3_Init("#version 130"))
		throw std::runtime_error("[imgui] failed to initialize OpenGL backend");
}

void MyApp::loadFonts()
{
	namespace fs = std::filesystem;

	ImGuiIO& io = ImGui::GetIO();

	if (fs::exists(CONSOLAS_FONT_PATH))
	{
		m_fonts.baseFont = io.Fonts->AddFontFromFileTTF(CONSOLAS_FONT_PATH, 18.0f);
		MyGui::mergeFont(FONT_AWESOME_PATH, 18.0f);
		m_fonts.headingFont = io.Fonts->AddFontFromFileTTF(CONSOLAS_FONT_PATH, 24.0f);
		MyGui::mergeFont(FONT_AWESOME_PATH, 24.0f);
		m_fonts.statusFont = io.Fonts->AddFontFromFileTTF(CONSOLAS_FONT_PATH, 20.0f);
		MyGui::mergeFont(FONT_AWESOME_PATH, 20.0f);
		m_fonts.padFont = io.Fonts->AddFontFromFileTTF(CONSOLAS_FONT_PATH, 42.0f);
		MyGui::mergeFont(FONT_AWESOME_PATH, 36.0f);
	}

	if (m_fonts.baseFont == nullptr)
	{
		m_fonts.baseFont = io.Fonts->AddFontDefault();
		MyGui::mergeFont(FONT_AWESOME_PATH, 16.0f);
		m_fonts.headingFont = m_fonts.baseFont;
		m_fonts.statusFont = m_fonts.baseFont;
		m_fonts.padFont = m_fonts.baseFont;
	}

	io.FontDefault = m_fonts.baseFont;
}

void MyApp::setupWindows()
{
	m_windows.emplace_back(new ControlWindow());
	m_windows.emplace_back(new LaunchpadWindow());
	m_windows.emplace_back(new ConsoleWindow(m_console));

	for (auto& window : m_windows)
	{
		window->initialize(&m_console.getOutput());
		window->open();
	}
}

void MyApp::setupDockLayout(unsigned int dockspace_id)
{
	ImGui::DockBuilderRemoveNode(dockspace_id);
	ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

	ImGuiID dock_main = dockspace_id;
	ImGuiID dock_right = 0;
	ImGuiID dock_left = 0;
	ImGuiID dock_bottom = 0;
	ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.32f, &dock_right, &dock_left);
	ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.28f, &dock_bottom, &dock_left);

	ImGui::DockBuilderDockWindow("Control Panel", dock_right);
	ImGui::DockBuilderDockWindow("Launchpad", dock_left);
	ImGui::DockBuilderDockWindow("Console", dock_bottom);
	ImGui::DockBuilderFinish(dockspace_id);
}

void MyApp::updateWindows()
{
	for (auto& window : m_windows)
		window->update();
}

void MyApp::drawFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport();
	if (!m_dockLayoutInitialized)
	{
		setupDockLayout(dockspace_id);
		m_dockLayoutInitialized = true;
	}

	for (auto& window : m_windows)
		if (window->isOpen())
			window->draw(ImVec2(-1.0f, -1.0f));

	PlaybackController::Output output;
	int safety_counter = 0;
	while (safety_counter < 8 && m_controller->popDueFrame(output))
	{
		if (m_server)
			m_server->broadcastFrame(output.frame);
		++safety_counter;
	}

	ImGui::Render();

	int display_width = 0;
	int display_height = 0;
	glfwGetFramebufferSize(m_window, &display_width, &display_height);
	glViewport(0, 0, display_width, display_height);
	glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(m_window);
}

void MyApp::limitFrameRate(float fps) const
{
	const float delta = ImGui::GetIO().DeltaTime;
	const float remaining = (1.0f / fps) - delta;
	if (remaining <= 0.0f)
		return;

	const auto delay_us = static_cast<long long>(remaining * 1'000'000.0f);
	std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
}

void MyApp::glfwErrorCallback(int, const char* description)
{
	throw std::runtime_error(std::string("[glfw] ") + description);
}

void MyApp::glfwIconifyCallback(GLFWwindow* window, int iconified)
{
	auto* app = reinterpret_cast<MyApp*>(glfwGetWindowUserPointer(window));
	if (app)
		app->m_windowMinimized = (iconified != 0);
}

int main()
{
	try
	{
		MyApp app;
		app.run();
		return 0;
	}
	catch (const std::runtime_error& e)
	{
		return MessageBoxA(nullptr, e.what(), "Radar Emulator", MB_OK | MB_ICONERROR) == 0 ? 1 : 1;
	}
}
