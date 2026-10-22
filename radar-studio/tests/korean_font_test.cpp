#include "../src/ui/korean_font.h"
#include <common/ui/imgui_custom.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

float testFonts(bool korean, const char* iconPath)
{
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(640, 480);
	io.DeltaTime = 1.0f / 60.0f;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.FontDefault = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 18.0f);
	expect(io.FontDefault != nullptr, "Consolas loaded");
	expect(MyGui::mergeFont(iconPath, 18.0f), "existing Font Awesome loader succeeds");
	const int sourceCount = io.Fonts->Sources.Size;
	expect(!radar_ui::mergeKoreanFont(*io.Fonts, "C:/Windows/Fonts/__missing_korean_font__.ttf", 18.0f), "missing fallback is safe");
	expect(io.Fonts->Sources.Size == sourceCount, "missing fallback leaves fonts unchanged");
	if (korean) expect(radar_ui::mergeKoreanFont(*io.Fonts, "C:/Windows/Fonts/malgun.ttf", 18.0f), "production Korean fallback loaded");
	ImGui::NewFrame();
	auto* baked = ImGui::GetFontBaked();
	const auto* latin = baked->FindGlyphNoFallback('A');
	expect(latin != nullptr && latin->Visible, "Latin glyph retained");
	const float advance = latin->AdvanceX;
	expect(baked->FindGlyphNoFallback(0xF111) != nullptr, "Font Awesome circle retained");
	for (const ImWchar code : { ImWchar(0xAC00), ImWchar(0xD55C), ImWchar(0xAE00) })
	{
		const auto* glyph = baked->FindGlyphNoFallback(code);
		if (korean) expect(glyph && glyph->Visible && glyph->U1 > glyph->U0 && glyph->V1 > glyph->V0, "Korean glyph rasterized, not fallback");
		else expect(glyph == nullptr, "baseline lacks Korean glyph");
	}
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(600, 300));
	ImGui::Begin("Font regression");
	ImGui::TextUnformatted("ASCII 10054 " ICON_FA_CIRCLE);
	if (korean) ImGui::TextUnformatted("\xED\x95\x9C\xEA\xB8\x80");
	ImGui::End();
	ImGui::Render();
	expect(ImGui::GetDrawData()->TotalVtxCount > 0, "CPU text draw data generated");
	ImGui::DestroyContext();
	return advance;
}

int main(int argc, char** argv)
{
	try
	{
		expect(argc == 2, "pass existing Font Awesome ttf path");
		const float baseline = testFonts(false, argv[1]);
		const float merged = testFonts(true, argv[1]);
		expect(std::abs(baseline - merged) < 0.001f, "existing Latin metrics unchanged");
		std::cout << "Korean font tests PASS (CPU glyph rasterization/draw data; no GPU window)\n";
		return 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; if (ImGui::GetCurrentContext()) ImGui::DestroyContext(); return 1; }
}
