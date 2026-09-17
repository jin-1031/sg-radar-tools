#pragma once

#include <common/pcr/pcr_type.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

class PointCloudDataset;

class PlaybackController
{
public:
	enum class LoopMode
	{
		RandomRestart,
		RestartFromBeginning,
		RestartFromActivationStart,
	};

	struct Output
	{
		pcr::Frame frame;
		std::string recordName;
		std::size_t logicalFrameIndex = 0;
		std::size_t totalFrames = 0;
		std::uint32_t streamFrameCount = 0;
		bool valid = false;
	};

	PlaybackController();

	void setDataset(const PointCloudDataset* dataset);

	void setDefaultRecordName(std::string name);
	const std::string& getDefaultRecordName() const;

	void setHeldRecordName(std::optional<std::string> name);
	const std::optional<std::string>& getHeldRecordName() const;

	void setFps(float fps);
	float getFps() const;

	void setLoopMode(LoopMode loop_mode);
	LoopMode getLoopMode() const;

	void reset();
	bool popDueFrame(Output& out_output);

	const Output& getLastOutput() const;
	bool hasActiveRecord() const;
	std::string getActiveRecordName() const;
	std::size_t getActiveFrameIndex() const;
	std::size_t getActiveTotalFrames() const;

private:
	std::string resolveDesiredRecordName() const;
	bool reconcileActiveRecord();
	bool tryAdvanceClock();
	void activateRecord(std::string name, std::size_t total_frames);
	std::size_t chooseRandomIndex(std::size_t total_frames);
	std::size_t chooseLoopIndex(std::size_t total_frames);

	const PointCloudDataset* m_dataset = nullptr;

	std::string m_defaultRecordName;
	std::optional<std::string> m_heldRecordName;
	std::string m_activeRecordName;

	std::size_t m_nextFrameIndex = 0;
	std::size_t m_activationStartIndex = 0;
	std::uint32_t m_streamFrameCount = 0;

	float m_fps = 20.0f;
	LoopMode m_loopMode = LoopMode::RandomRestart;
	bool m_forceEmit = true;
	bool m_hasEmissionClock = false;
	std::chrono::steady_clock::time_point m_lastEmitTime;

	Output m_lastOutput;
	std::mt19937 m_rng;
};
