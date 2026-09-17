#include "imgui_custom.h"
#include "../util/colormap.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
	const char* makeIconLabel(const char* icon, const char* label)
	{
		static char s_buf[256];
		if (icon == nullptr || icon[0] == '\0')
			return label != nullptr ? label : "";
		if (label == nullptr || label[0] == '\0')
			return icon;
		std::snprintf(s_buf, sizeof(s_buf), "%s  %s", icon, label);
		return s_buf;
	}

	ImU32 resolveIconColor(ImU32 color)
	{
		if (color != 0)
			return color;
		return ImGui::GetColorU32(ImGuiCol_Text);
	}

	void drawIcon(ImDrawList* draw_list, const ImVec2& pos, const char* icon, float font_size, ImU32 color)
	{
		ImFont* font = ImGui::GetFont();
		draw_list->AddText(font, font_size, pos, color, icon);
	}

	bool iconButtonEx(const char* str_id, const char* icon, const char* label, ImVec2 size, ImU32 color)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		const ImGuiStyle& style = ImGui::GetStyle();
		ImFont* font = ImGui::GetFont();
		const float font_size = ImGui::GetFontSize();
		const ImVec2 icon_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, icon);
		ImVec2 label_size(0.0f, 0.0f);
		if (label != nullptr && label[0] != '\0')
			label_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);

		const float gap = (label != nullptr && label[0] != '\0') ? style.ItemInnerSpacing.x : 0.0f;
		if (size.x <= 0.0f)
			size.x = icon_size.x + gap + label_size.x + style.FramePadding.x * 2.0f;
		if (size.y <= 0.0f)
			size.y = ImMax(icon_size.y, label_size.y) + style.FramePadding.y * 2.0f;

		const bool pressed = ImGui::InvisibleButton(str_id, size);
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();
		const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;

		ImDrawList* draw_list = window->DrawList;
		const ImRect bb = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		const ImU32 bg_col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		const ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
		ImU32 icon_color = resolveIconColor(color);
		if (disabled)
		{
			ImVec4 tinted = ImGui::ColorConvertU32ToFloat4(icon_color);
			tinted.w *= 0.55f;
			icon_color = ImGui::ColorConvertFloat4ToU32(tinted);
		}

		draw_list->AddRectFilled(bb.Min, bb.Max, bg_col, style.FrameRounding);
		draw_list->AddRect(bb.Min, bb.Max, border_col, style.FrameRounding);

		const float content_width = icon_size.x + gap + label_size.x;
		ImVec2 cursor(
			bb.Min.x + (bb.GetWidth() - content_width) * 0.5f,
			bb.Min.y + (bb.GetHeight() - icon_size.y) * 0.5f);
		drawIcon(draw_list, cursor, icon, font_size, icon_color);
		if (label != nullptr && label[0] != '\0')
		{
			cursor.x += icon_size.x + gap;
			cursor.y = bb.Min.y + (bb.GetHeight() - label_size.y) * 0.5f;
			draw_list->AddText(font, font_size, cursor, icon_color, label);
		}

		return pressed;
	}

	ImVec4 rgb(std::uint32_t hex, float alpha = 1.0f)
	{
		return ImVec4(
			static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
			static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
			static_cast<float>(hex & 0xFF) / 255.0f,
			alpha);
	}

	void applyLayout(ImGuiStyle& style, bool high_contrast)
	{
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 5.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
		style.IndentSpacing = 16.0f;
		style.ScrollbarSize = 14.0f;
		style.GrabMinSize = high_contrast ? 14.0f : 12.0f;

		const float rounding = high_contrast ? 0.0f : 2.0f;
		style.WindowRounding = high_contrast ? 0.0f : 2.0f;
		style.ChildRounding = rounding;
		style.FrameRounding = rounding;
		style.PopupRounding = rounding;
		style.ScrollbarRounding = rounding;
		style.GrabRounding = rounding;
		style.TabRounding = rounding;
		style.TabBarBorderSize = high_contrast ? 2.0f : 1.0f;

		const float border = high_contrast ? 2.0f : 1.0f;
		style.WindowBorderSize = border;
		style.ChildBorderSize = border;
		style.PopupBorderSize = border;
		style.FrameBorderSize = high_contrast ? 1.0f : 0.0f;
		style.TabBorderSize = high_contrast ? 1.0f : 0.0f;
		style.ImageBorderSize = high_contrast ? 1.0f : 0.0f;

		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_Left;
		style.ColorButtonPosition = ImGuiDir_Right;
		style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
		style.SeparatorTextBorderSize = high_contrast ? 2.0f : 1.0f;
		style.HoverStationaryDelay = 0.15f;
		style.HoverDelayShort = 0.15f;
		style.HoverDelayNormal = 0.40f;
	}

	void applyDarkModern(ImVec4* c)
	{
		const ImVec4 editor = rgb(0x1F1F1F);
		const ImVec4 sidebar = rgb(0x181818);
		const ImVec4 widget = rgb(0x313131);
		const ImVec4 border = rgb(0x3C3C3C);
		const ImVec4 text = rgb(0xCCCCCC);
		const ImVec4 text_dim = rgb(0x9D9D9D);
		const ImVec4 accent = rgb(0x0078D4);
		const ImVec4 accent_hover = rgb(0x026EC1);
		const ImVec4 accent_active = rgb(0x07538F);
		const ImVec4 selection = rgb(0x04395E);
		const ImVec4 hover = rgb(0x2A2D2E);
		const ImVec4 link = rgb(0x4DAAFC);

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = text_dim;
		c[ImGuiCol_WindowBg] = editor;
		c[ImGuiCol_ChildBg] = sidebar;
		c[ImGuiCol_PopupBg] = rgb(0x252526, 0.98f);
		c[ImGuiCol_Border] = rgb(0x3C3C3C, 0.80f);
		c[ImGuiCol_BorderShadow] = rgb(0x000000, 0.00f);
		c[ImGuiCol_FrameBg] = widget;
		c[ImGuiCol_FrameBgHovered] = hover;
		c[ImGuiCol_FrameBgActive] = selection;
		c[ImGuiCol_TitleBg] = sidebar;
		c[ImGuiCol_TitleBgActive] = sidebar;
		c[ImGuiCol_TitleBgCollapsed] = rgb(0x181818, 0.75f);
		c[ImGuiCol_MenuBarBg] = sidebar;
		c[ImGuiCol_ScrollbarBg] = rgb(0x1F1F1F, 0.60f);
		c[ImGuiCol_ScrollbarGrab] = rgb(0x6E6E6E, 0.55f);
		c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x9E9E9E, 0.70f);
		c[ImGuiCol_ScrollbarGrabActive] = rgb(0xB0B0B0, 0.85f);
		c[ImGuiCol_CheckMark] = accent;
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = accent_hover;
		c[ImGuiCol_Button] = widget;
		c[ImGuiCol_ButtonHovered] = rgb(0x3E3E3E);
		c[ImGuiCol_ButtonActive] = rgb(0x505050);
		c[ImGuiCol_Header] = selection;
		c[ImGuiCol_HeaderHovered] = hover;
		c[ImGuiCol_HeaderActive] = accent_active;
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_SeparatorHovered] = accent_hover;
		c[ImGuiCol_SeparatorActive] = accent;
		c[ImGuiCol_ResizeGrip] = rgb(0x0078D4, 0.25f);
		c[ImGuiCol_ResizeGripHovered] = rgb(0x0078D4, 0.60f);
		c[ImGuiCol_ResizeGripActive] = accent;
		c[ImGuiCol_InputTextCursor] = text;
		c[ImGuiCol_TabHovered] = hover;
		c[ImGuiCol_Tab] = sidebar;
		c[ImGuiCol_TabSelected] = editor;
		c[ImGuiCol_TabSelectedOverline] = accent;
		c[ImGuiCol_TabDimmed] = rgb(0x141414);
		c[ImGuiCol_TabDimmedSelected] = rgb(0x1A1A1A);
		c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0x0078D4, 0.45f);
		c[ImGuiCol_DockingPreview] = rgb(0x0078D4, 0.35f);
		c[ImGuiCol_DockingEmptyBg] = sidebar;
		c[ImGuiCol_PlotLines] = rgb(0x9CDCFE);
		c[ImGuiCol_PlotLinesHovered] = rgb(0x4FC1FF);
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_PlotHistogramHovered] = accent_hover;
		c[ImGuiCol_TableHeaderBg] = rgb(0x2B2B2B);
		c[ImGuiCol_TableBorderStrong] = border;
		c[ImGuiCol_TableBorderLight] = rgb(0x3C3C3C, 0.55f);
		c[ImGuiCol_TableRowBg] = rgb(0x000000, 0.00f);
		c[ImGuiCol_TableRowBgAlt] = rgb(0xFFFFFF, 0.03f);
		c[ImGuiCol_TextLink] = link;
		c[ImGuiCol_TextSelectedBg] = rgb(0x264F78, 0.80f);
		c[ImGuiCol_TreeLines] = border;
		c[ImGuiCol_DragDropTarget] = rgb(0x0078D4, 0.90f);
		c[ImGuiCol_DragDropTargetBg] = rgb(0x0078D4, 0.20f);
		c[ImGuiCol_UnsavedMarker] = rgb(0xCCA700);
		c[ImGuiCol_NavCursor] = accent;
		c[ImGuiCol_NavWindowingHighlight] = rgb(0xFFFFFF, 0.70f);
		c[ImGuiCol_NavWindowingDimBg] = rgb(0x000000, 0.45f);
		c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.50f);
	}

	void applyLightModern(ImVec4* c)
	{
		const ImVec4 editor = rgb(0xFFFFFF);
		const ImVec4 sidebar = rgb(0xF8F8F8);
		const ImVec4 border = rgb(0xE5E5E5);
		const ImVec4 text = rgb(0x3B3B3B);
		const ImVec4 text_dim = rgb(0x8B8B8B);
		const ImVec4 accent = rgb(0x0078D4);
		const ImVec4 accent_hover = rgb(0x026EC1);
		const ImVec4 accent_active = rgb(0x005A9E);
		const ImVec4 selection = rgb(0x0060C0, 0.18f);
		const ImVec4 hover = rgb(0xE8E8E8);
		const ImVec4 link = rgb(0x005FB8);

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = text_dim;
		c[ImGuiCol_WindowBg] = editor;
		c[ImGuiCol_ChildBg] = sidebar;
		c[ImGuiCol_PopupBg] = rgb(0xFFFFFF, 0.98f);
		c[ImGuiCol_Border] = rgb(0xCECECE, 0.85f);
		c[ImGuiCol_BorderShadow] = rgb(0x000000, 0.00f);
		c[ImGuiCol_FrameBg] = rgb(0xF3F3F3);
		c[ImGuiCol_FrameBgHovered] = hover;
		c[ImGuiCol_FrameBgActive] = selection;
		c[ImGuiCol_TitleBg] = sidebar;
		c[ImGuiCol_TitleBgActive] = sidebar;
		c[ImGuiCol_TitleBgCollapsed] = rgb(0xF8F8F8, 0.80f);
		c[ImGuiCol_MenuBarBg] = sidebar;
		c[ImGuiCol_ScrollbarBg] = rgb(0xF8F8F8, 0.60f);
		c[ImGuiCol_ScrollbarGrab] = rgb(0xC2C2C2);
		c[ImGuiCol_ScrollbarGrabHovered] = rgb(0xA8A8A8);
		c[ImGuiCol_ScrollbarGrabActive] = rgb(0x8C8C8C);
		c[ImGuiCol_CheckMark] = accent;
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = accent_hover;
		c[ImGuiCol_Button] = accent;
		c[ImGuiCol_ButtonHovered] = accent_hover;
		c[ImGuiCol_ButtonActive] = accent_active;
		c[ImGuiCol_Header] = selection;
		c[ImGuiCol_HeaderHovered] = hover;
		c[ImGuiCol_HeaderActive] = rgb(0x0060C0, 0.28f);
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_SeparatorHovered] = accent_hover;
		c[ImGuiCol_SeparatorActive] = accent;
		c[ImGuiCol_ResizeGrip] = rgb(0x0078D4, 0.20f);
		c[ImGuiCol_ResizeGripHovered] = rgb(0x0078D4, 0.50f);
		c[ImGuiCol_ResizeGripActive] = accent;
		c[ImGuiCol_InputTextCursor] = text;
		c[ImGuiCol_TabHovered] = hover;
		c[ImGuiCol_Tab] = rgb(0xECECEC);
		c[ImGuiCol_TabSelected] = editor;
		c[ImGuiCol_TabSelectedOverline] = accent;
		c[ImGuiCol_TabDimmed] = rgb(0xE4E4E4);
		c[ImGuiCol_TabDimmedSelected] = rgb(0xF3F3F3);
		c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0x0078D4, 0.45f);
		c[ImGuiCol_DockingPreview] = rgb(0x0078D4, 0.28f);
		c[ImGuiCol_DockingEmptyBg] = sidebar;
		c[ImGuiCol_PlotLines] = rgb(0x005FB8);
		c[ImGuiCol_PlotLinesHovered] = accent;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_PlotHistogramHovered] = accent_hover;
		c[ImGuiCol_TableHeaderBg] = rgb(0xF0F0F0);
		c[ImGuiCol_TableBorderStrong] = rgb(0xCECECE);
		c[ImGuiCol_TableBorderLight] = rgb(0xE5E5E5);
		c[ImGuiCol_TableRowBg] = rgb(0x000000, 0.00f);
		c[ImGuiCol_TableRowBgAlt] = rgb(0x000000, 0.03f);
		c[ImGuiCol_TextLink] = link;
		c[ImGuiCol_TextSelectedBg] = rgb(0xADD6FF, 0.80f);
		c[ImGuiCol_TreeLines] = rgb(0xCECECE);
		c[ImGuiCol_DragDropTarget] = rgb(0x0078D4, 0.90f);
		c[ImGuiCol_DragDropTargetBg] = rgb(0x0078D4, 0.16f);
		c[ImGuiCol_UnsavedMarker] = rgb(0xBF8803);
		c[ImGuiCol_NavCursor] = accent;
		c[ImGuiCol_NavWindowingHighlight] = rgb(0x000000, 0.20f);
		c[ImGuiCol_NavWindowingDimBg] = rgb(0x000000, 0.18f);
		c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.28f);
	}

	void applyHighContrast(ImVec4* c)
	{
		const ImVec4 bg = rgb(0x000000);
		const ImVec4 fg = rgb(0xFFFFFF);
		const ImVec4 border = rgb(0x6FC3DF);
		const ImVec4 focus = rgb(0xF38518);
		const ImVec4 button = rgb(0x0E639C);

		c[ImGuiCol_Text] = fg;
		c[ImGuiCol_TextDisabled] = rgb(0xA0A0A0);
		c[ImGuiCol_WindowBg] = bg;
		c[ImGuiCol_ChildBg] = bg;
		c[ImGuiCol_PopupBg] = bg;
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_BorderShadow] = rgb(0x000000, 0.00f);
		c[ImGuiCol_FrameBg] = bg;
		c[ImGuiCol_FrameBgHovered] = rgb(0x1A1A1A);
		c[ImGuiCol_FrameBgActive] = rgb(0xFFFFFF, 0.18f);
		c[ImGuiCol_TitleBg] = bg;
		c[ImGuiCol_TitleBgActive] = bg;
		c[ImGuiCol_TitleBgCollapsed] = bg;
		c[ImGuiCol_MenuBarBg] = bg;
		c[ImGuiCol_ScrollbarBg] = bg;
		c[ImGuiCol_ScrollbarGrab] = border;
		c[ImGuiCol_ScrollbarGrabHovered] = focus;
		c[ImGuiCol_ScrollbarGrabActive] = focus;
		c[ImGuiCol_CheckMark] = focus;
		c[ImGuiCol_SliderGrab] = border;
		c[ImGuiCol_SliderGrabActive] = focus;
		c[ImGuiCol_Button] = button;
		c[ImGuiCol_ButtonHovered] = rgb(0x1177BB);
		c[ImGuiCol_ButtonActive] = focus;
		c[ImGuiCol_Header] = rgb(0xFFFFFF, 0.18f);
		c[ImGuiCol_HeaderHovered] = rgb(0xFFFFFF, 0.28f);
		c[ImGuiCol_HeaderActive] = focus;
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_SeparatorHovered] = focus;
		c[ImGuiCol_SeparatorActive] = focus;
		c[ImGuiCol_ResizeGrip] = border;
		c[ImGuiCol_ResizeGripHovered] = focus;
		c[ImGuiCol_ResizeGripActive] = focus;
		c[ImGuiCol_InputTextCursor] = fg;
		c[ImGuiCol_TabHovered] = rgb(0xFFFFFF, 0.20f);
		c[ImGuiCol_Tab] = bg;
		c[ImGuiCol_TabSelected] = bg;
		c[ImGuiCol_TabSelectedOverline] = border;
		c[ImGuiCol_TabDimmed] = bg;
		c[ImGuiCol_TabDimmedSelected] = bg;
		c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0x6FC3DF, 0.55f);
		c[ImGuiCol_DockingPreview] = rgb(0xF38518, 0.40f);
		c[ImGuiCol_DockingEmptyBg] = bg;
		c[ImGuiCol_PlotLines] = border;
		c[ImGuiCol_PlotLinesHovered] = focus;
		c[ImGuiCol_PlotHistogram] = border;
		c[ImGuiCol_PlotHistogramHovered] = focus;
		c[ImGuiCol_TableHeaderBg] = bg;
		c[ImGuiCol_TableBorderStrong] = border;
		c[ImGuiCol_TableBorderLight] = border;
		c[ImGuiCol_TableRowBg] = rgb(0x000000, 0.00f);
		c[ImGuiCol_TableRowBgAlt] = rgb(0xFFFFFF, 0.06f);
		c[ImGuiCol_TextLink] = rgb(0x4080D0);
		c[ImGuiCol_TextSelectedBg] = rgb(0xFFFFFF, 0.35f);
		c[ImGuiCol_TreeLines] = border;
		c[ImGuiCol_DragDropTarget] = focus;
		c[ImGuiCol_DragDropTargetBg] = rgb(0xF38518, 0.18f);
		c[ImGuiCol_UnsavedMarker] = focus;
		c[ImGuiCol_NavCursor] = focus;
		c[ImGuiCol_NavWindowingHighlight] = fg;
		c[ImGuiCol_NavWindowingDimBg] = rgb(0x000000, 0.70f);
		c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.75f);
	}

	void applyQuietLight(ImVec4* c)
	{
		const ImVec4 editor = rgb(0xF5F5F5);
		const ImVec4 sidebar = rgb(0xF2F2F2);
		const ImVec4 accent = rgb(0x705697);
		const ImVec4 accent_hover = rgb(0x5D457F);
		const ImVec4 accent_active = rgb(0x4B3768);
		const ImVec4 text = rgb(0x333333);
		const ImVec4 text_dim = rgb(0xAAAAAA);
		const ImVec4 keyword = rgb(0x4B69C6);
		const ImVec4 string = rgb(0x448C27);
		const ImVec4 selection = rgb(0xC9D0D9);
		const ImVec4 title = rgb(0xC4B7D7);

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = text_dim;
		c[ImGuiCol_WindowBg] = editor;
		c[ImGuiCol_ChildBg] = sidebar;
		c[ImGuiCol_PopupBg] = rgb(0xF7F7F7, 0.98f);
		c[ImGuiCol_Border] = rgb(0xD4D0DC);
		c[ImGuiCol_BorderShadow] = rgb(0x000000, 0.00f);
		c[ImGuiCol_FrameBg] = rgb(0xEDEDED);
		c[ImGuiCol_FrameBgHovered] = rgb(0xE4DEEA);
		c[ImGuiCol_FrameBgActive] = selection;
		c[ImGuiCol_TitleBg] = title;
		c[ImGuiCol_TitleBgActive] = title;
		c[ImGuiCol_TitleBgCollapsed] = rgb(0xC4B7D7, 0.70f);
		c[ImGuiCol_MenuBarBg] = rgb(0xEDEDED);
		c[ImGuiCol_ScrollbarBg] = rgb(0xF2F2F2, 0.60f);
		c[ImGuiCol_ScrollbarGrab] = rgb(0xC4B7D7);
		c[ImGuiCol_ScrollbarGrabHovered] = accent;
		c[ImGuiCol_ScrollbarGrabActive] = accent_hover;
		c[ImGuiCol_CheckMark] = accent;
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = accent_hover;
		c[ImGuiCol_Button] = accent;
		c[ImGuiCol_ButtonHovered] = accent_hover;
		c[ImGuiCol_ButtonActive] = accent_active;
		c[ImGuiCol_Header] = rgb(0xC4B7D7, 0.55f);
		c[ImGuiCol_HeaderHovered] = rgb(0xC4B7D7, 0.80f);
		c[ImGuiCol_HeaderActive] = accent;
		c[ImGuiCol_Separator] = rgb(0xD4D0DC);
		c[ImGuiCol_SeparatorHovered] = accent_hover;
		c[ImGuiCol_SeparatorActive] = accent;
		c[ImGuiCol_ResizeGrip] = rgb(0x705697, 0.25f);
		c[ImGuiCol_ResizeGripHovered] = rgb(0x705697, 0.55f);
		c[ImGuiCol_ResizeGripActive] = accent;
		c[ImGuiCol_InputTextCursor] = text;
		c[ImGuiCol_TabHovered] = rgb(0xE4DEEA);
		c[ImGuiCol_Tab] = rgb(0xEDEDED);
		c[ImGuiCol_TabSelected] = editor;
		c[ImGuiCol_TabSelectedOverline] = accent;
		c[ImGuiCol_TabDimmed] = rgb(0xE6E6E6);
		c[ImGuiCol_TabDimmedSelected] = rgb(0xF0F0F0);
		c[ImGuiCol_TabDimmedSelectedOverline] = rgb(0x705697, 0.45f);
		c[ImGuiCol_DockingPreview] = rgb(0x705697, 0.30f);
		c[ImGuiCol_DockingEmptyBg] = sidebar;
		c[ImGuiCol_PlotLines] = keyword;
		c[ImGuiCol_PlotLinesHovered] = accent;
		c[ImGuiCol_PlotHistogram] = string;
		c[ImGuiCol_PlotHistogramHovered] = accent;
		c[ImGuiCol_TableHeaderBg] = rgb(0xEDEDED);
		c[ImGuiCol_TableBorderStrong] = rgb(0xD4D0DC);
		c[ImGuiCol_TableBorderLight] = rgb(0xE4E0E8);
		c[ImGuiCol_TableRowBg] = rgb(0x000000, 0.00f);
		c[ImGuiCol_TableRowBgAlt] = rgb(0x705697, 0.04f);
		c[ImGuiCol_TextLink] = keyword;
		c[ImGuiCol_TextSelectedBg] = rgb(0xC9D0D9, 0.85f);
		c[ImGuiCol_TreeLines] = rgb(0xD4D0DC);
		c[ImGuiCol_DragDropTarget] = rgb(0x705697, 0.90f);
		c[ImGuiCol_DragDropTargetBg] = rgb(0x705697, 0.16f);
		c[ImGuiCol_UnsavedMarker] = rgb(0xAA3731);
		c[ImGuiCol_NavCursor] = accent;
		c[ImGuiCol_NavWindowingHighlight] = rgb(0x705697, 0.55f);
		c[ImGuiCol_NavWindowingDimBg] = rgb(0x000000, 0.16f);
		c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.22f);
	}
}

void MyGui::applyTheme(Theme theme)
{
	ImGuiStyle& style = ImGui::GetStyle();
	const bool high_contrast = (theme == Theme::HighContrast);

	if (theme == Theme::Light || theme == Theme::QuietLight)
		ImGui::StyleColorsLight();
	else
		ImGui::StyleColorsDark();

	applyLayout(style, high_contrast);

	ImVec4* colors = style.Colors;
	switch (theme)
	{
	case Theme::Light:
		applyLightModern(colors);
		break;
	case Theme::HighContrast:
		applyHighContrast(colors);
		break;
	case Theme::QuietLight:
		applyQuietLight(colors);
		break;
	case Theme::Dark:
	default:
		applyDarkModern(colors);
		break;
	}
}

const char* MyGui::themeLabel(Theme theme)
{
	switch (theme)
	{
	case Theme::Light: return "Light";
	case Theme::HighContrast: return "High Contrast";
	case Theme::QuietLight: return "Quiet Light";
	case Theme::Dark:
	default: return "Dark";
	}
}

const char* MyGui::themeKey(Theme theme)
{
	switch (theme)
	{
	case Theme::Light: return "Light";
	case Theme::HighContrast: return "HighContrast";
	case Theme::QuietLight: return "QuietLight";
	case Theme::Dark:
	default: return "Dark";
	}
}

MyGui::Theme MyGui::themeFromKey(const char* key)
{
	if (key == nullptr || key[0] == '\0')
		return Theme::Dark;
	if (std::strcmp(key, "Light") == 0)
		return Theme::Light;
	if (std::strcmp(key, "HighContrast") == 0 || std::strcmp(key, "High Contrast") == 0)
		return Theme::HighContrast;
	if (std::strcmp(key, "QuietLight") == 0 || std::strcmp(key, "Quiet Light") == 0)
		return Theme::QuietLight;
	return Theme::Dark;
}

const char* const* MyGui::themeLabels()
{
	static const char* labels[] = {
		themeLabel(Theme::Dark),
		themeLabel(Theme::Light),
		themeLabel(Theme::HighContrast),
		themeLabel(Theme::QuietLight),
	};
	return labels;
}

bool MyGui::mergeFont(const char* path, float size)
{
	ImGuiIO& io = ImGui::GetIO();
	if (path == nullptr || path[0] == '\0')
		return false;
	if (size <= 0.0f)
		size = io.FontDefault != nullptr ? io.FontDefault->LegacySize : 16.0f;
	if (!std::filesystem::exists(path))
		return false;

	static const ImWchar kRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig config;
	config.MergeMode = true;
	config.PixelSnapH = true;
	config.SizePixels = size;
	config.GlyphMinAdvanceX = size;
	return io.Fonts->AddFontFromFileTTF(path, size, &config, kRanges) != nullptr;
}

void MyGui::Icon(const char* icon, float size, ImU32 color)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems || icon == nullptr)
		return;

	ImFont* font = ImGui::GetFont();
	const float font_size = size > 0.0f ? size : ImGui::GetFontSize();
	const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, icon);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	drawIcon(window->DrawList, pos, icon, font_size, resolveIconColor(color));
	ImGui::Dummy(text_size);
}

bool MyGui::IconButton(const char* str_id, const char* icon, ImVec2 size, ImU32 color)
{
	return iconButtonEx(str_id, icon, nullptr, size, color);
}

bool MyGui::IconButton(const char* str_id, const char* icon, const char* label, ImVec2 size, ImU32 color)
{
	return iconButtonEx(str_id, icon, label, size, color);
}

bool MyGui::IconMenu(const char* icon, const char* label, const char* shortcut, bool selected, bool enabled)
{
	return ImGui::MenuItem(makeIconLabel(icon, label), shortcut, selected, enabled);
}

bool MyGui::IconMenu(const char* icon, const char* label, const char* shortcut, bool* p_selected, bool enabled)
{
	return ImGui::MenuItem(makeIconLabel(icon, label), shortcut, p_selected, enabled);
}

bool MyGui::TimelineFrameSlider(const char* label, int* value, int min, int max)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const float width = ImGui::CalcItemWidth();
	const float height = 32.0f;
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	const bool has_frames = max >= min;
	const bool interactive = has_frames && max > min && ((g.CurrentItemFlags & ImGuiItemFlags_Disabled) == 0);
	const ImRect track_bb(ImVec2(bb.Min.x, bb.Min.y + 14.0f), ImVec2(bb.Max.x, bb.Min.y + 22.0f));

	bool hovered = false;
	bool held = false;
	bool pressed = false;
	if (interactive)
		pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	bool changed = false;

	if (interactive && hovered && g.IO.MouseWheel != 0.0f)
	{
		int delta = static_cast<int>(g.IO.MouseWheel);
		const int next = *value + delta;
		if (next != *value)
		{
			*value = next;
			changed = true;
			ImGui::MarkItemEdited(id);
		}
	}

	if (interactive && (pressed || held))
	{
		const float t = ImSaturate((g.IO.MousePos.x - track_bb.Min.x) / ImMax(track_bb.GetWidth(), 1.0f));
		const int next = min + static_cast<int>(std::round(t * static_cast<float>(max - min)));
		if (next != *value)
		{
			*value = next;
			changed = true;
			ImGui::MarkItemEdited(id);
		}
	}

	if (has_frames)
		*value = std::clamp(*value, min, max);
	else
		*value = min;

	ImDrawList* draw_list = window->DrawList;
	const ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg, interactive ? 1.0f : 0.5f);
	const ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 tick_col = IM_COL32(255, 255, 255, 150);
	const ImU32 cursor_col = ImGui::GetColorU32(held ? ImGuiCol_SliderGrabActive : (hovered ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab));

	draw_list->AddRectFilled(track_bb.Min, track_bb.Max, bg_col, 3.0f);
	draw_list->AddRect(track_bb.Min, track_bb.Max, border_col, 3.0f);

	if (has_frames)
	{
		const int frame_count = max - min + 1;
		if (frame_count >= 2)
		{
			const float track_width = ImMax(track_bb.GetWidth(), 1.0f);
			const float spacing = track_width / static_cast<float>(frame_count - 1);
			const float max_tick_height = 8.0f;
			const float min_tick_height = 2.0f;
			const float tick_height = ImClamp((spacing * 0.6f), min_tick_height, max_tick_height);
			const float tick_top = bb.Min.y + 2.0f;
			const float tick_bottom = tick_top + tick_height;
			for (int i = 0; i < frame_count; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(frame_count - 1);
				const float x = ImLerp(track_bb.Min.x, track_bb.Max.x, t);
				draw_list->AddLine(ImVec2(x, tick_top), ImVec2(x, tick_bottom), tick_col, 1.0f);
			}
		}

		const float t = max > min ? static_cast<float>(*value - min) / static_cast<float>(max - min) : 0.0f;
		const float x = ImLerp(track_bb.Min.x, track_bb.Max.x, ImSaturate(t));
		const float top = bb.Min.y + 2.0f;
		const float shoulder = top + 7.0f;
		const float bottom = track_bb.Max.y + 4.0f;
		const float half = 7.0f;
		const ImVec2 pts[5] = {
			ImVec2(x, top),
			ImVec2(x + half, shoulder),
			ImVec2(x + half, bottom),
			ImVec2(x - half, bottom),
			ImVec2(x - half, shoulder)
		};
		draw_list->AddConvexPolyFilled(pts, 5, cursor_col);
		draw_list->AddPolyline(pts, 5, IM_COL32(15, 15, 18, 220), ImDrawFlags_Closed, 1.0f);
	}

	if (label[0] != '#' || label[1] != '#')
		ImGui::RenderText(ImVec2(bb.Max.x + style.ItemInnerSpacing.x, bb.Min.y + 8.0f), label);

	return changed;
}

bool MyGui::FrameSlider(const char* label, int* value, int min, int max)
{
	return TimelineFrameSlider(label, value, min, max);
}

void MyGui::RecordIndicator(const char* label, bool value, ImVec2 size)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	struct RecStorage
	{
		float t;
		bool last_value;
	};

	auto* storage = ImGui::GetStateStorage();
	auto* rec_storage = reinterpret_cast<RecStorage*>(storage->GetVoidPtr(ImGui::GetID(label)));

	if (rec_storage == nullptr)
	{
		rec_storage = IM_NEW(RecStorage);
		rec_storage->t = 0.0f;
		rec_storage->last_value = value;
		storage->SetVoidPtr(ImGui::GetID(label), rec_storage);
	}

	if (rec_storage->last_value != value)
	{
		rec_storage->t = 0.0f;
		rec_storage->last_value = value;
	}

	const bool active = value && (rec_storage->t < 1.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
	const ImVec2 text_size = ImGui::CalcTextSize("REC");
	const ImColor rec_color = active ? IM_COL32(230, 40, 40, 255) : IM_COL32(110, 40, 40, 120);
	const ImColor text_color = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 150);

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), rec_color, ImMin(size.x, size.y) * 0.5f);
	draw_list->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f), text_color, "REC");

	rec_storage->t = fmodf(rec_storage->t + ImGui::GetIO().DeltaTime, 2.0f);

	ImGui::Dummy(size);
}

void MyGui::LegendBar(const ImVec4& left, const ImVec4& right)
{
	ImVec2 size(ImGui::GetContentRegionAvail().x, 18.0f);
	size.x = size.x > 1.0f ? size.x : 1.0f;

	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + size.x, p0.y + size.y);
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(p0, p1, IM_COL32(32, 32, 36, 255), 3.0f);
	draw_list->AddRectFilledMultiColor(
		p0,
		p1,
		ImGui::ColorConvertFloat4ToU32(left),
		ImGui::ColorConvertFloat4ToU32(right),
		ImGui::ColorConvertFloat4ToU32(right),
		ImGui::ColorConvertFloat4ToU32(left));
	draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 48), 3.0f);
	ImGui::Dummy(size);
}

void MyGui::HeatMap(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;
	if (values == nullptr || width <= 0 || height <= 0)
		return;

	const ImGuiID id = window->GetID(label);
	ImVec2 draw_size = size;
	if (draw_size.x <= 0.0f)
		draw_size.x = ImGui::GetContentRegionAvail().x;
	if (draw_size.y <= 0.0f)
		draw_size.y = 200.0f;
	if (draw_size.x < 1.0f)
		draw_size.x = 1.0f;
	if (draw_size.y < 1.0f)
		draw_size.y = 1.0f;

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y));
	ImGui::ItemSize(bb);
	if (!ImGui::ItemAdd(bb, id))
		return;

	ImDrawList* draw_list = window->DrawList;
	const ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
	draw_list->AddRectFilled(bb.Min, bb.Max, IM_COL32(0, 0, 0, 255), 3.0f);
	draw_list->AddRect(bb.Min, bb.Max, border_col, 3.0f);

	const float cell_w = draw_size.x / static_cast<float>(width);
	const float cell_h = draw_size.y / static_cast<float>(height);

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			float v = values[y * width + x];
			if (v < 0.0f)
				continue;
			v = std::clamp(v, 0.0f, 1.0f);

			const auto mapped = colormap::map(static_cast<colormap::Colormap>(map_type), v);
			const ImU32 col = IM_COL32(
				static_cast<int>(mapped.r * 255.0f),
				static_cast<int>(mapped.g * 255.0f),
				static_cast<int>(mapped.b * 255.0f),
				255);
			const ImVec2 p0(bb.Min.x + x * cell_w, bb.Min.y + y * cell_h);
			const ImVec2 p1(p0.x + cell_w + 0.5f, p0.y + cell_h + 0.5f);
			draw_list->AddRectFilled(p0, p1, col);
		}
	}
}

void MyGui::HeatMapH(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;
	if (values == nullptr || width <= 0 || height <= 0)
		return;

	const ImGuiID id = window->GetID(label);
	ImVec2 draw_size = size;
	if (draw_size.x <= 0.0f)
		draw_size.x = ImGui::GetContentRegionAvail().x;
	if (draw_size.y <= 0.0f)
		draw_size.y = 200.0f;
	if (draw_size.x < 1.0f)
		draw_size.x = 1.0f;
	if (draw_size.y < 1.0f)
		draw_size.y = 1.0f;

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y));
	ImGui::ItemSize(bb);
	if (!ImGui::ItemAdd(bb, id))
		return;

	ImDrawList* draw_list = window->DrawList;
	const ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
	draw_list->AddRectFilled(bb.Min, bb.Max, IM_COL32(0, 0, 0, 255), 3.0f);
	draw_list->AddRect(bb.Min, bb.Max, border_col, 3.0f);

	const float cell_w = draw_size.x / static_cast<float>(height);
	const float cell_h = draw_size.y / static_cast<float>(width);

	for (int col = 0; col < height; ++col)
	{
		for (int row = 0; row < width; ++row)
		{
			float v = values[col * width + row];
			if (v < 0.0f)
				continue;
			v = std::clamp(v, 0.0f, 1.0f);
			const auto mapped = colormap::map(static_cast<colormap::Colormap>(map_type), v);
			const ImU32 col_u32 = IM_COL32(
				static_cast<int>(mapped.r * 255.0f),
				static_cast<int>(mapped.g * 255.0f),
				static_cast<int>(mapped.b * 255.0f),
				255);
			const ImVec2 p0(bb.Min.x + col * cell_w, bb.Min.y + row * cell_h);
			const ImVec2 p1(p0.x + cell_w + 0.5f, p0.y + cell_h + 0.5f);
			draw_list->AddRectFilled(p0, p1, col_u32);
		}
	}
}

void MyGui::HeatMapV(const char* label, const float* values, int width, int height, int map_type, const ImVec2& size)
{
	HeatMap(label, values, width, height, map_type, size);
}
