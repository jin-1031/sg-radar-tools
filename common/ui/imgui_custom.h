#pragma once

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace MyGui
{
	enum class Theme
	{
		Dark,
		Light,
		HighContrast,
		QuietLight,
	};

	constexpr int ThemeCount = 4;

	void applyTheme(Theme theme);
	const char* themeLabel(Theme theme);
	const char* themeKey(Theme theme);
	Theme themeFromKey(const char* key);
	const char* const* themeLabels();

	bool mergeFont(const char* path, float size = 0.0f);

	void Icon(const char* icon, float size = 0.0f, ImU32 color = 0);
	bool IconButton(const char* str_id, const char* icon, ImVec2 size = ImVec2(0.0f, 0.0f), ImU32 color = 0);
	bool IconButton(const char* str_id, const char* icon, const char* label, ImVec2 size = ImVec2(0.0f, 0.0f), ImU32 color = 0);
	bool IconMenu(const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
	bool IconMenu(const char* icon, const char* label, const char* shortcut, bool* p_selected, bool enabled = true);

	bool TimelineFrameSlider(const char* label, int* value, int min, int max);
	bool FrameSlider(const char* label, int* value, int min, int max);
	void RecordIndicator(const char* label, bool value, ImVec2 size = ImVec2(48.0f, 24.0f));
	void LegendBar(const ImVec4& left, const ImVec4& right);
	void HeatMap(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size);
	void HeatMapH(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size);
	void HeatMapV(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size);
}

namespace
{
	class style_var_guard
	{
	public:
		style_var_guard(ImGuiStyleVar idx, const ImVec2& value)
		{
			ImGui::PushStyleVar(idx, value);
		}

		~style_var_guard()
		{
			ImGui::PopStyleVar();
		}
	};
}
