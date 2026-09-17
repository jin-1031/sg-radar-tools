#include "point_cloud_dataset.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <numeric>
#include <string_view>

namespace
{
	std::string toLower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	std::string filenameOf(const std::filesystem::path& path)
	{
		return path.filename().string();
	}

	int compareNaturalIgnoreCase(std::string_view lhs, std::string_view rhs)
	{
		std::size_t i = 0;
		std::size_t j = 0;

		while (i < lhs.size() && j < rhs.size())
		{
			const unsigned char left = static_cast<unsigned char>(lhs[i]);
			const unsigned char right = static_cast<unsigned char>(rhs[j]);
			const bool left_digit = std::isdigit(left) != 0;
			const bool right_digit = std::isdigit(right) != 0;

			if (left_digit && right_digit)
			{
				std::size_t left_end = i;
				std::size_t right_end = j;
				while (left_end < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[left_end])))
					++left_end;
				while (right_end < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[right_end])))
					++right_end;

				std::size_t left_value = i;
				std::size_t right_value = j;
				while (left_value + 1 < left_end && lhs[left_value] == '0')
					++left_value;
				while (right_value + 1 < right_end && rhs[right_value] == '0')
					++right_value;

				const std::size_t left_digits = left_end - left_value;
				const std::size_t right_digits = right_end - right_value;
				if (left_digits != right_digits)
					return left_digits < right_digits ? -1 : 1;

				const int numeric = lhs.compare(left_value, left_digits, rhs, right_value, right_digits);
				if (numeric != 0)
					return numeric < 0 ? -1 : 1;

				const std::size_t left_raw = left_end - i;
				const std::size_t right_raw = right_end - j;
				if (left_raw != right_raw)
					return left_raw < right_raw ? -1 : 1;

				i = left_end;
				j = right_end;
				continue;
			}

			const unsigned char left_folded = static_cast<unsigned char>(std::tolower(left));
			const unsigned char right_folded = static_cast<unsigned char>(std::tolower(right));
			if (left_folded != right_folded)
				return left_folded < right_folded ? -1 : 1;

			++i;
			++j;
		}

		if (i == lhs.size() && j == rhs.size())
			return 0;
		return i == lhs.size() ? -1 : 1;
	}

	bool filenameLess(const std::string& lhs, const std::string& rhs)
	{
		return compareNaturalIgnoreCase(lhs, rhs) < 0;
	}

	bool isPcrFile(const std::filesystem::path& path)
	{
		return toLower(path.extension().string()) == ".pcr";
	}

	std::vector<std::filesystem::path> collectPcrCandidates(const std::filesystem::path& folder_path, std::string& out_error)
	{
		std::error_code ec;
		std::vector<std::filesystem::path> candidates;

		for (const auto& entry : std::filesystem::directory_iterator(folder_path, ec))
		{
			if (ec)
			{
				out_error = "Failed to scan folder: " + folder_path.string();
				return {};
			}

			if (!entry.is_regular_file() || !isPcrFile(entry.path()))
				continue;

			candidates.push_back(entry.path());
		}

		std::sort(candidates.begin(), candidates.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
		{
			return filenameLess(filenameOf(lhs), filenameOf(rhs));
		});

		return candidates;
	}

	void sortRecordsByName(std::vector<PcDatasetRecord>& records)
	{
		std::vector<std::size_t> order(records.size());
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [&records](std::size_t lhs, std::size_t rhs)
		{
			return filenameLess(records[lhs].name, records[rhs].name);
		});

		std::vector<PcDatasetRecord> sorted;
		sorted.reserve(records.size());
		for (const std::size_t index : order)
			sorted.push_back(std::move(records[index]));
		records = std::move(sorted);
	}
}

const pcr::Frame* PcDatasetRecord::getFrame(std::size_t logical_index) const
{
	const PcDatasetFrameRef* ref = getFrameRef(logical_index);
	if (ref == nullptr)
		return nullptr;

	const auto* session = recorder.getSession(ref->sessionId);
	if (session == nullptr || ref->frameIndex >= session->frames.size())
		return nullptr;

	return &session->frames[ref->frameIndex].frame;
}

const PcDatasetFrameRef* PcDatasetRecord::getFrameRef(std::size_t logical_index) const
{
	if (logical_index >= tape.size())
		return nullptr;

	return &tape[logical_index];
}

const PointCloudRecorder::RecordSession* PcDatasetRecord::getSession(std::size_t logical_index) const
{
	const PcDatasetFrameRef* ref = getFrameRef(logical_index);
	if (ref == nullptr)
		return nullptr;

	return recorder.getSession(ref->sessionId);
}

bool PointCloudDataset::loadFromFolder(const std::filesystem::path& folder_path, std::string& out_error, ProgressCallback progress_callback)
{
	clear();
	out_error.clear();

	if (folder_path.empty())
	{
		out_error = "Folder path is empty.";
		return false;
	}

	std::error_code ec;
	if (!std::filesystem::exists(folder_path, ec) || ec)
	{
		out_error = "Folder does not exist: " + folder_path.string();
		return false;
	}

	if (!std::filesystem::is_directory(folder_path, ec) || ec)
	{
		out_error = "Path is not a directory: " + folder_path.string();
		return false;
	}

	const std::vector<std::filesystem::path> candidates = collectPcrCandidates(folder_path, out_error);
	if (!out_error.empty())
	{
		clear();
		return false;
	}
	if (candidates.empty())
	{
		out_error = "No .pcr files were found in: " + folder_path.string();
		return false;
	}

	const int total_files = static_cast<int>(candidates.size());
	for (int candidate_index = 0; candidate_index < total_files; ++candidate_index)
	{
		const std::filesystem::path& path = candidates[static_cast<std::size_t>(candidate_index)];
		if (progress_callback)
			progress_callback(candidate_index + 1, total_files, path, false);

		if (!loadRecord(path, out_error))
		{
			clear();
			return false;
		}

		if (progress_callback)
			progress_callback(candidate_index + 1, total_files, path, true);
	}

	sortRecordsByName(m_records);

	m_folderPath = folder_path;
	return true;
}

bool PointCloudDataset::loadRecord(const std::filesystem::path& path, std::string& out_error)
{
	std::ifstream input(path, std::ios::binary);
	if (!input.is_open())
	{
		out_error = "Failed to open file: " + path.string();
		return false;
	}

	PcDatasetRecord record;
	record.name = filenameOf(path);

	if (!record.recorder.deserializeFromStream(input))
	{
		out_error = "Failed to load .pcr file: " + path.string();
		return false;
	}

	record.sessionCount = record.recorder.getSessionCount();
	for (std::size_t session_index = 0; session_index < record.sessionCount; ++session_index)
	{
		const SessionID session_id = record.recorder.getOrderedSessionID(session_index);
		const auto* session = record.recorder.getSession(session_id);
		if (session == nullptr)
			continue;

		for (std::size_t frame_index = 0; frame_index < session->frames.size(); ++frame_index)
			record.tape.push_back({ session_id, session_index, frame_index });
	}

	record.totalFrames = record.tape.size();
	m_records.push_back(std::move(record));
	return true;
}

void PointCloudDataset::clear()
{
	m_folderPath.clear();
	m_records.clear();
}

bool PointCloudDataset::empty() const
{
	return m_records.empty();
}

const std::filesystem::path& PointCloudDataset::getFolder() const
{
	return m_folderPath;
}

const std::vector<PcDatasetRecord>& PointCloudDataset::getRecords() const
{
	return m_records;
}

const PcDatasetRecord* PointCloudDataset::findRecord(const std::string& name) const
{
	if (name.empty())
		return nullptr;

	const auto it = std::find_if(m_records.begin(), m_records.end(), [&name](const PcDatasetRecord& record)
	{
		return record.name == name;
	});
	return it != m_records.end() ? &*it : nullptr;
}

bool PointCloudDataset::hasRecord(const std::string& name) const
{
	return findRecord(name) != nullptr;
}

std::string PointCloudDataset::getFirstAvailableRecordName() const
{
	for (const PcDatasetRecord& record : m_records)
	{
		if (record.totalFrames > 0)
			return record.name;
	}
	return {};
}
