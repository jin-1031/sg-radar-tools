#include "launchpad_window.h"
#include "../../core/playback_controller.h"
#include "../../my_app.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>

namespace
{
	constexpr ImU32 kPadFillIdle = IM_COL32(35, 39, 46, 255);
	constexpr ImU32 kPadFillHeld = IM_COL32(122, 88, 36, 255);
	constexpr ImU32 kPadFillActive = IM_COL32(42, 108, 62, 255);
	constexpr ImU32 kPadFillDefault = IM_COL32(45, 68, 118, 255);
	constexpr ImU32 kPadFillHovered = IM_COL32(56, 61, 71, 255);
	constexpr ImU32 kPadBorder = IM_COL32(90, 96, 110, 255);
	constexpr ImU32 kPadTextName = IM_COL32(245, 248, 252, 255);
	constexpr ImU32 kPadTextPrimary = IM_COL32(238, 240, 244, 255);
	constexpr ImU32 kPadTextSecondary = IM_COL32(196, 202, 212, 255);
	constexpr ImU32 kPadTextTertiary = IM_COL32(176, 182, 192, 255);
	constexpr ImU32 kPadTextDefaultLabel = IM_COL32(196, 206, 255, 255);
	constexpr ImU32 kPadTextLiveLabel = IM_COL32(190, 255, 200, 255);

	constexpr float kPadMinWidth = 185.0f;
	constexpr float kPadHeight = 156.0f;
	constexpr float kPadRounding = 10.0f;
	constexpr float kPadBorderIdle = 1.2f;
	constexpr float kPadBorderEmphasis = 2.2f;
	constexpr float kPadPaddingX = 14.0f;
	constexpr float kPadNameY = 12.0f;
	constexpr float kPadLine1Y = 72.0f;
	constexpr float kPadLine2Y = 102.0f;
	constexpr float kPadLine3Y = 126.0f;
	constexpr float kPadDefaultLabelOffsetX = 84.0f;
	constexpr float kPadLiveLabelOffsetX = 54.0f;
	constexpr float kPadLiveLabelOffsetY = 24.0f;

	std::optional<std::string> getKeyboardHeldRecordName(const PointCloudDataset* dataset)
	{
		if (dataset == nullptr || ImGui::GetIO().WantTextInput)
			return std::nullopt;

		struct KeyBinding
		{
			ImGuiKey main_key;
			ImGuiKey keypad_key;
			const char* name;
		};

		static constexpr KeyBinding kBindings[] =
		{
			{ ImGuiKey_0, ImGuiKey_Keypad0, "0" },
			{ ImGuiKey_1, ImGuiKey_Keypad1, "1" },
			{ ImGuiKey_2, ImGuiKey_Keypad2, "2" },
			{ ImGuiKey_3, ImGuiKey_Keypad3, "3" },
			{ ImGuiKey_4, ImGuiKey_Keypad4, "4" },
			{ ImGuiKey_5, ImGuiKey_Keypad5, "5" },
			{ ImGuiKey_6, ImGuiKey_Keypad6, "6" },
			{ ImGuiKey_7, ImGuiKey_Keypad7, "7" },
			{ ImGuiKey_8, ImGuiKey_Keypad8, "8" },
			{ ImGuiKey_9, ImGuiKey_Keypad9, "9" },
		};

		for (const KeyBinding& binding : kBindings)
		{
			if (!ImGui::IsKeyDown(binding.main_key) && !ImGui::IsKeyDown(binding.keypad_key))
				continue;

			for (const auto& record : dataset->getRecords())
			{
				const auto dot = record.name.find_last_of('.');
				const std::string stem = (dot == std::string::npos) ? record.name : record.name.substr(0, dot);
				if (stem == binding.name && record.totalFrames > 0)
					return record.name;
			}
		}

		return std::nullopt;
	}

	ImU32 getPadFillColor(bool is_held, bool is_pressed, bool is_active, bool is_default, bool hovered)
	{
		if (is_held || is_pressed)
			return kPadFillHeld;
		if (is_active)
			return kPadFillActive;
		if (is_default)
			return kPadFillDefault;
		if (hovered)
			return kPadFillHovered;
		return kPadFillIdle;
	}

	struct PadCaptions
	{
		std::string line1;
		std::string line2;
		std::string line3;
	};

	PadCaptions makePadCaptions(
		const PcDatasetRecord& record,
		const PlaybackController::Output& output,
		bool has_live_details)
	{
		PadCaptions captions;
		captions.line1 = std::to_string(record.sessionCount) + " sessions";
		captions.line2 = std::to_string(record.totalFrames) + " frames";

		if (!has_live_details)
			return captions;

		const auto* ref = record.getFrameRef(output.logicalFrameIndex);
		const auto* session = record.getSession(output.logicalFrameIndex);
		if (ref == nullptr || session == nullptr)
			return captions;

		captions.line1 = "S" + std::to_string(ref->sessionOrdinal + 1) + "  F" +
			std::to_string(ref->frameIndex + 1) + "/" + std::to_string(session->frames.size());
		captions.line2 = "G" + std::to_string(output.logicalFrameIndex + 1) + "/" +
			std::to_string(output.totalFrames);
		return captions;
	}

	void addPadText(ImDrawList* draw_list, ImFont* font, float font_size, const ImVec2& pos, ImU32 color, const char* text)
	{
		if (font != nullptr)
			draw_list->AddText(font, font_size, pos, color, text);
		else
			draw_list->AddText(pos, color, text);
	}

	void drawRecordPad(
		const WindowFont& fonts,
		std::optional<std::string>& held_record_name,
		const PcDatasetRecord& record,
		const PlaybackController::Output& output,
		const std::string& default_record_name,
		const std::string& active_record_name,
		const ImVec2& tile_size)
	{
		const bool is_default = record.name == default_record_name;
		const bool is_active = record.name == active_record_name;
		const bool is_playable = record.totalFrames > 0;
		const PadCaptions captions = makePadCaptions(record, output, is_active && output.valid);

		ImGui::PushID(record.name.c_str());
		ImGui::BeginDisabled(!is_playable);
		ImGui::InvisibleButton("##ClassPad", tile_size);
		const bool hovered = ImGui::IsItemHovered();
		const bool is_pressed = ImGui::IsItemActive();
		if (is_playable && is_pressed)
			held_record_name = record.name;
		ImGui::EndDisabled();

		const bool is_held = held_record_name.has_value() && *held_record_name == record.name;
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImVec2 rect_min = ImGui::GetItemRectMin();
		const ImVec2 rect_max = ImGui::GetItemRectMax();
		const ImU32 fill = getPadFillColor(is_held, is_pressed, is_active, is_default, hovered);
		const float border_thickness = (is_active || is_held) ? kPadBorderEmphasis : kPadBorderIdle;

		draw_list->AddRectFilled(rect_min, rect_max, fill, kPadRounding);
		draw_list->AddRect(rect_min, rect_max, kPadBorder, kPadRounding, 0, border_thickness);
		draw_list->PushClipRect(
			ImVec2(rect_min.x + 1.0f, rect_min.y + 1.0f),
			ImVec2(rect_max.x - 1.0f, rect_max.y - 1.0f),
			true);

		addPadText(
			draw_list,
			fonts.padFont,
			fonts.padFont != nullptr ? fonts.padFont->LegacySize : ImGui::GetFontSize(),
			ImVec2(rect_min.x + kPadPaddingX, rect_min.y + kPadNameY),
			kPadTextName,
			record.name.c_str());

		ImFont* small_font = fonts.baseFont != nullptr ? fonts.baseFont : ImGui::GetFont();
		ImFont* medium_font = fonts.statusFont != nullptr ? fonts.statusFont : ImGui::GetFont();
		const float small_font_size = fonts.baseFont != nullptr ? fonts.baseFont->LegacySize : ImGui::GetFontSize();
		const float medium_font_size = fonts.statusFont != nullptr ? fonts.statusFont->LegacySize : small_font_size;

		addPadText(draw_list, medium_font, medium_font_size, ImVec2(rect_min.x + kPadPaddingX, rect_min.y + kPadLine1Y), kPadTextPrimary, captions.line1.c_str());
		addPadText(draw_list, small_font, small_font_size, ImVec2(rect_min.x + kPadPaddingX, rect_min.y + kPadLine2Y), kPadTextSecondary, captions.line2.c_str());
		if (!captions.line3.empty())
			addPadText(draw_list, small_font, small_font_size, ImVec2(rect_min.x + kPadPaddingX, rect_min.y + kPadLine3Y), kPadTextTertiary, captions.line3.c_str());

		if (is_default)
		{
			addPadText(
				draw_list,
				small_font,
				small_font_size,
				ImVec2(rect_max.x - kPadDefaultLabelOffsetX, rect_min.y + kPadPaddingX),
				kPadTextDefaultLabel,
				"DEFAULT");
		}
		if (is_active)
		{
			addPadText(
				draw_list,
				small_font,
				small_font_size,
				ImVec2(rect_max.x - kPadLiveLabelOffsetX, rect_max.y - kPadLiveLabelOffsetY),
				kPadTextLiveLabel,
				"LIVE");
		}

		draw_list->PopClipRect();
		ImGui::PopID();
	}
}

LaunchpadWindow::LaunchpadWindow() :
	DockingWindow("Launchpad"),
	m_fonts(MyApp::get().getWindowFont()),
	m_dataset(MyApp::get().getDataset()),
	m_controller(MyApp::get().getPlaybackController())
{
}

void LaunchpadWindow::onDraw(const ImVec2& size)
{
	(void)size;

	m_heldRecordName = getKeyboardHeldRecordName(m_dataset);

	if (m_dataset == nullptr || m_dataset->empty())
	{
		ImGui::TextUnformatted("No gesture classes loaded.");
		if (m_controller)
			m_controller->setHeldRecordName(m_heldRecordName);
		return;
	}

	if (ImGui::BeginChild("LaunchpadGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar))
	{
		const auto& records = m_dataset->getRecords();
		const std::string default_record_name = m_controller ? m_controller->getDefaultRecordName() : std::string();
		const std::string active_record_name = m_controller ? m_controller->getActiveRecordName() : std::string();
		const PlaybackController::Output& output = m_controller ? m_controller->getLastOutput() : PlaybackController::Output{};

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float available_width = ImGui::GetContentRegionAvail().x;
		const int columns = std::max(1, static_cast<int>((available_width + spacing) / (kPadMinWidth + spacing)));
		const float button_width = std::max(kPadMinWidth, (available_width - spacing * (columns - 1)) / columns);
		const ImVec2 tile_size(button_width, kPadHeight);

		for (std::size_t index = 0; index < records.size(); ++index)
		{
			drawRecordPad(
				m_fonts,
				m_heldRecordName,
				records[index],
				output,
				default_record_name,
				active_record_name,
				tile_size);

			const bool is_end_of_row = (index + 1) % columns == 0;
			if (!is_end_of_row && index + 1 < records.size())
				ImGui::SameLine();
		}
	}
	ImGui::EndChild();

	if (m_controller)
		m_controller->setHeldRecordName(m_heldRecordName);
}
