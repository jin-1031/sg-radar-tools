#include "playback_controller.h"
#include "point_cloud_dataset.h"

#include <algorithm>
#include <utility>

PlaybackController::PlaybackController() :
	m_rng(std::random_device{}())
{
}

void PlaybackController::setDataset(const PointCloudDataset* dataset)
{
	if (m_dataset == dataset)
		return;

	m_dataset = dataset;
	reset();
}

void PlaybackController::setDefaultRecordName(std::string name)
{
	if (m_defaultRecordName == name)
		return;

	m_defaultRecordName = std::move(name);
	m_forceEmit = true;
}

const std::string& PlaybackController::getDefaultRecordName() const
{
	return m_defaultRecordName;
}

void PlaybackController::setHeldRecordName(std::optional<std::string> name)
{
	if (m_heldRecordName == name)
		return;

	m_heldRecordName = std::move(name);
	m_forceEmit = true;
}

const std::optional<std::string>& PlaybackController::getHeldRecordName() const
{
	return m_heldRecordName;
}

void PlaybackController::setFps(float fps)
{
	m_fps = std::clamp(fps, 1.0f, 240.0f);
}

float PlaybackController::getFps() const
{
	return m_fps;
}

void PlaybackController::setLoopMode(LoopMode loop_mode)
{
	m_loopMode = loop_mode;
}

PlaybackController::LoopMode PlaybackController::getLoopMode() const
{
	return m_loopMode;
}

void PlaybackController::reset()
{
	m_activeRecordName.clear();
	m_nextFrameIndex = 0;
	m_activationStartIndex = 0;
	m_streamFrameCount = 0;
	m_lastOutput = {};
	m_forceEmit = true;
	m_hasEmissionClock = false;
}

bool PlaybackController::popDueFrame(Output& out_output)
{
	out_output = {};

	if (m_dataset == nullptr || m_dataset->empty())
		return false;
	if (!reconcileActiveRecord())
		return false;
	if (!tryAdvanceClock())
		return false;

	const PcDatasetRecord* record = m_dataset->findRecord(m_activeRecordName);
	if (record == nullptr || record->totalFrames == 0)
		return false;

	const std::size_t output_frame_index = std::min(m_nextFrameIndex, record->totalFrames - 1);
	const pcr::Frame* source_frame = record->getFrame(output_frame_index);
	if (source_frame == nullptr)
		return false;

	Output output;
	output.frame = *source_frame;
	output.frame.frameCount = ++m_streamFrameCount;
	output.recordName = record->name;
	output.logicalFrameIndex = output_frame_index;
	output.totalFrames = record->totalFrames;
	output.streamFrameCount = m_streamFrameCount;
	output.valid = true;

	m_lastOutput = output;
	out_output = output;

	++m_nextFrameIndex;
	if (m_nextFrameIndex >= record->totalFrames)
		m_nextFrameIndex = chooseLoopIndex(record->totalFrames);

	return true;
}

const PlaybackController::Output& PlaybackController::getLastOutput() const
{
	return m_lastOutput;
}

bool PlaybackController::hasActiveRecord() const
{
	return !m_activeRecordName.empty();
}

std::string PlaybackController::getActiveRecordName() const
{
	return m_lastOutput.valid ? m_lastOutput.recordName : m_activeRecordName;
}

std::size_t PlaybackController::getActiveFrameIndex() const
{
	return m_lastOutput.valid ? m_lastOutput.logicalFrameIndex : 0;
}

std::size_t PlaybackController::getActiveTotalFrames() const
{
	return m_lastOutput.valid ? m_lastOutput.totalFrames : 0;
}

std::string PlaybackController::resolveDesiredRecordName() const
{
	if (m_heldRecordName.has_value() && m_dataset->hasRecord(*m_heldRecordName))
		return *m_heldRecordName;
	if (m_dataset->hasRecord(m_defaultRecordName))
		return m_defaultRecordName;
	return m_dataset->getFirstAvailableRecordName();
}

bool PlaybackController::reconcileActiveRecord()
{
	const std::string desired_name = resolveDesiredRecordName();
	if (desired_name.empty())
	{
		m_activeRecordName.clear();
		return false;
	}

	const PcDatasetRecord* record = m_dataset->findRecord(desired_name);
	if (record == nullptr || record->totalFrames == 0)
	{
		m_activeRecordName.clear();
		return false;
	}

	if (m_activeRecordName != desired_name)
		activateRecord(desired_name, record->totalFrames);

	return true;
}

bool PlaybackController::tryAdvanceClock()
{
	const auto frame_interval = std::chrono::duration<double>(1.0 / static_cast<double>(m_fps));
	const auto now = std::chrono::steady_clock::now();

	if (m_forceEmit)
	{
		m_forceEmit = false;
		m_lastEmitTime = now;
		m_hasEmissionClock = true;
		return true;
	}

	if (!m_hasEmissionClock)
	{
		m_lastEmitTime = now;
		m_hasEmissionClock = true;
		return true;
	}

	if (now - m_lastEmitTime < frame_interval)
		return false;

	m_lastEmitTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_interval);
	return true;
}

void PlaybackController::activateRecord(std::string name, std::size_t total_frames)
{
	m_activeRecordName = std::move(name);
	m_activationStartIndex = chooseRandomIndex(total_frames);
	m_nextFrameIndex = m_activationStartIndex;
	m_forceEmit = true;
}

std::size_t PlaybackController::chooseRandomIndex(std::size_t total_frames)
{
	if (total_frames <= 1)
		return 0;

	std::uniform_int_distribution<std::size_t> distribution(0, total_frames - 1);
	return distribution(m_rng);
}

std::size_t PlaybackController::chooseLoopIndex(std::size_t total_frames)
{
	if (total_frames == 0)
		return 0;

	switch (m_loopMode)
	{
	case LoopMode::RestartFromBeginning:
		return 0;
	case LoopMode::RestartFromActivationStart:
		return std::min(m_activationStartIndex, total_frames - 1);
	case LoopMode::RandomRestart:
	default:
		return chooseRandomIndex(total_frames);
	}
}
