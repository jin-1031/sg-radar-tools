#pragma once

#include "docking_window.h"
#include "../../util/debug_console.h"

class ConsoleWindow : public DockingWindow
{
public:
	ConsoleWindow(DebugConsole& console);

	DebugConsole& getConsole();

private:
	void onDraw(const ImVec2& size) override;

private:
	DebugConsole& m_console;
};
