#pragma once

#include <common/pcr/point_cloud_recorder.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct PcDatasetFrameRef
{
	SessionID sessionId = static_cast<SessionID>(-1);
	std::size_t sessionOrdinal = 0;
	std::size_t frameIndex = 0;
};

struct PcDatasetRecord
{
	std::string name;
	PointCloudRecorder recorder;
	std::vector<PcDatasetFrameRef> tape;
	std::size_t sessionCount = 0;
	std::size_t totalFrames = 0;

	const pcr::Frame* getFrame(std::size_t logical_index) const;
	const PcDatasetFrameRef* getFrameRef(std::size_t logical_index) const;
	const PointCloudRecorder::RecordSession* getSession(std::size_t logical_index) const;
};

class PointCloudDataset
{
public:
	using ProgressCallback = std::function<void(int current_file, int total_files, const std::filesystem::path& current_path, bool completed)>;

	bool loadFromFolder(const std::filesystem::path& folder_path, std::string& out_error, ProgressCallback progress_callback = {});
	void clear();

	bool empty() const;

	const std::filesystem::path& getFolder() const;

	const std::vector<PcDatasetRecord>& getRecords() const;
	const PcDatasetRecord* findRecord(const std::string& name) const;
	bool hasRecord(const std::string& name) const;
	std::string getFirstAvailableRecordName() const;

private:
	bool loadRecord(const std::filesystem::path& path, std::string& out_error);

	std::filesystem::path m_folderPath;
	std::vector<PcDatasetRecord> m_records;
};
