#pragma once

#include "point_cloud_window.h"

#include <cstdint>
#include <vector>

class AnalysisWindow : public DockingWindow
{
public:
	static constexpr int kHistogramBins = 128;
	static constexpr int kDopplerHistogramBins = 64;
	static constexpr int kPowerHistogramBins = 128;

	enum class SourceType
	{
		None = 0,
		Sensor = 1,
		CurrentSession = 2,
		Session = 3,
		Window1 = 4,
		Window2 = 5,
		Window3 = 6,
		Window4 = 7,
	};

	struct AnalysisSetting
	{
		float dopplerMin = -2.0f;
		float dopplerMax = 2.0f;
		float powerMin = 25.0f;
		float powerMax = 60.0f;
	};

	AnalysisWindow();
	~AnalysisWindow() = default;

	void setSourceRecordingWindow(RecordingWindow* window);
	void setSourcePointWindows(const std::vector<PointCloudWindow*>& windows);

private:
	void onUpdate() override;
	void onDraw(const ImVec2& size) override;
	void onSettingReadLine(void* entry, const char* line) override;
	void onSettingWriteAll(ImGuiTextBuffer* buf) const override;

	void drawSourceSelector();
	void drawSessionSelector();
	void drawFrameSelector();
	void drawFrameInfo();
	void drawTargetSelector();
	void drawHistograms();

	void syncCurrentFrameFromSource();
	void updateSelectedTargetsFromFrame();
	void buildDopplerHistogram(const std::vector<float>& data, std::vector<float>& histogram);
	void buildPowerHistogram(const std::vector<float>& data, std::vector<float>& histogram);

	SourceType m_source = SourceType::None;
	RecordingWindow* m_sourceRecordingWindow = nullptr;
	std::vector<PointCloudWindow*> m_sourcePointWindows;

	const PointCloudRecorder::RecordSession* m_currSession = nullptr;
	SessionID m_sessionId = static_cast<SessionID>(-1);
	const retina::Frame* m_currFrame = nullptr;
	int m_playbackIdx = 0;

	std::vector<uint8_t> m_selectedTargets;
	bool m_selectAllTargets = false;

	AnalysisSetting m_analysisSetting;

	std::vector<float> m_dopplerHistogramX;
	std::vector<float> m_dopplerHistogramCount;
	std::vector<float> m_powerHistogramX;
	std::vector<float> m_powerHistogramCount;
	float m_dopplerMaxCount = 0.0f;
	float m_powerMaxCount = 0.0f;
	float m_dopplerHeightScale = 1.1f;
	float m_powerHeightScale = 1.1f;

	SourceType m_lastHistogramSource = SourceType::None;
	SessionID m_lastHistogramSessionId = static_cast<SessionID>(-1);
	int m_lastHistogramPlaybackIdx = -1;
	uint32_t m_lastHistogramFrameCount = 0;
	bool m_histogramDirty = true;
};
