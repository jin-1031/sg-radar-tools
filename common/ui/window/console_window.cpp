#include "console_window.h"

#include "../imgui_custom.h"

#include <vector>

ConsoleWindow::ConsoleWindow(DebugConsole& console) :
	DockingWindow("Console"),
	m_console(console)
{
}

DebugConsole& ConsoleWindow::getConsole()
{
	return m_console;
}

void ConsoleWindow::onDraw(const ImVec2& size)
{
	if (!ImGui::BeginChild("ConsoleLog", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::EndChild();
		return;
	}

	if (ImGui::BeginPopupContextWindow())
	{
		if (MyGui::IconMenu(ICON_FA_ERASER, "Clear"))
			m_console.clear();
		ImGui::EndPopup();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

	std::vector<std::string> items;
	m_console.snapshot(items);
	for (const std::string& item : items)
	{
		if (item.find("[error]") != std::string::npos)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::TextUnformatted(item.c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::TextUnformatted(item.c_str());
		}
	}

	if (m_console.takeScrollToBottom() || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);

	ImGui::PopStyleVar();
	ImGui::EndChild();
}
