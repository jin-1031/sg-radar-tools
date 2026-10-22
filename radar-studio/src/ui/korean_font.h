#pragma once

#include <imgui.h>
#include <filesystem>

namespace radar_ui
{
	inline bool mergeKoreanFont(ImFontAtlas& atlas, const char* path, float size)
	{
		std::error_code error;
		if (path == nullptr || atlas.Fonts.empty() || size <= 0 || !std::filesystem::exists(path, error) || error)
			return false;
		// ImGui 1.92 loads glyphs dynamically: exclude non-Korean ranges explicitly.
		// Existing Consolas Latin and Font Awesome private-use glyphs remain untouched.
		static const ImWchar excluded[] = {
			0x0001, 0x10FF, 0x1200, 0x3130, 0x3164, 0xABFF, 0xD7A4, IM_UNICODE_CODEPOINT_MAX, 0
		};
		ImFontConfig config;
		config.MergeMode = true;
		config.PixelSnapH = true;
		config.GlyphExcludeRanges = excluded;
		return atlas.AddFontFromFileTTF(path, size, &config) != nullptr;
	}
}
