#pragma once

#include <imgui.h>

struct WindowFont
{
	ImFont* baseFont;
	ImFont* headingFont;
	ImFont* statusFont;
	ImFont* padFont;
};

class font_guard
{
public:
	font_guard(ImFont* font) :
		m_font(font)
	{
		if (m_font)
			ImGui::PushFont(m_font);
	}

	~font_guard()
	{
		if (m_font)
			ImGui::PopFont();
	}

	void pop()
	{
		if (m_font)
		{
			ImGui::PopFont();
			m_font = nullptr;
		}
	}

private:
	ImFont* m_font;
};
