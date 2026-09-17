#include "point_cloud_recorder.h"

#include <zstd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

using std::uint8_t;
using std::uint64_t;

#define PROPAGATE_ERROR(expr) \
	do { \
		if (!(expr)) \
			return false; \
	} while (0)

namespace
{
	uint64_t get_unix_time_ms()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	uint64_t calc_frame_byte_size(const pcr::Frame& frame)
	{
		return sizeof(frame.packetSize) + sizeof(frame.frameCount) + sizeof(frame.deltaUs) +
			sizeof(uint64_t) + frame.points.size() * sizeof(pcr::Point) +
			sizeof(uint64_t) + frame.targets.size() * sizeof(pcr::Target) +
			sizeof(uint64_t);
	}

	template <class T>
	void serialize(std::vector<uint8_t>& binary, const T& data)
	{
		binary.resize(binary.size() + sizeof(T));
		std::memcpy(binary.data() + binary.size() - sizeof(T), &data, sizeof(T));
	}

	template <class T>
	void serialize_array(std::vector<uint8_t>& binary, const T* data, size_t size)
	{
		static_assert(std::is_trivial_v<T>, "Only trivial types can be serialized with serialize_array");
		binary.resize(binary.size() + size * sizeof(T));
		std::memcpy(binary.data() + binary.size() - size * sizeof(T), data, size * sizeof(T));
	}

	template <>
	void serialize<std::string>(std::vector<uint8_t>& binary, const std::string& str)
	{
		uint64_t length = str.size();
		serialize(binary, length);
		serialize_array(binary, str.data(), length);
	}

	template <>
	void serialize<pcr::Frame>(std::vector<uint8_t>& binary, const pcr::Frame& frame)
	{
		serialize(binary, frame.packetSize);
		serialize(binary, frame.frameCount);
		serialize(binary, frame.deltaUs);
		serialize(binary, frame.points.size());
		serialize_array(binary, frame.points.data(), frame.points.size());
		serialize(binary, frame.targets.size());
		serialize_array(binary, frame.targets.data(), frame.targets.size());
	}

	template <>
	void serialize<PointCloudRecorder::RecordFrame>(std::vector<uint8_t>& binary, const PointCloudRecorder::RecordFrame& record_frame)
	{
		serialize(binary, record_frame.frame);
		serialize(binary, record_frame.bytes);
	}

	template <>
	void serialize<PointCloudRecorder::Bookmark>(std::vector<uint8_t>& binary, const PointCloudRecorder::Bookmark& bookmark)
	{
		serialize(binary, bookmark.description);
		serialize(binary, bookmark.frameIndex);
	}

	template <class T>
	bool deserialize(std::vector<uint8_t>& binary, size_t& offset, T& data)
	{
		if (binary.size() < offset + sizeof(T))
			return false;

		std::memcpy(&data, binary.data() + offset, sizeof(T));
		offset += sizeof(T);

		return true;
	}

	template <class T>
	bool deserialize_array(std::vector<uint8_t>& binary, size_t& offset, T* data, size_t size)
	{
		if (binary.size() < offset + size * sizeof(T))
			return false;

		std::memcpy(data, binary.data() + offset, size * sizeof(T));
		offset += size * sizeof(T);

		return true;
	}

	template <>
	bool deserialize<std::string>(std::vector<uint8_t>& binary, size_t& offset, std::string& str)
	{
		uint64_t length;
		PROPAGATE_ERROR(deserialize(binary, offset, length));
		str.resize(length);
		return deserialize_array(binary, offset, str.data(), length);
	}

	template <>
	bool deserialize<pcr::Frame>(std::vector<uint8_t>& binary, size_t& offset, pcr::Frame& frame)
	{
		uint64_t point_count;
		uint64_t target_count;

		PROPAGATE_ERROR(deserialize(binary, offset, frame.packetSize));
		PROPAGATE_ERROR(deserialize(binary, offset, frame.frameCount));
		PROPAGATE_ERROR(deserialize(binary, offset, frame.deltaUs));

		PROPAGATE_ERROR(deserialize(binary, offset, point_count));
		frame.points.resize(point_count);
		PROPAGATE_ERROR(deserialize_array(binary, offset, frame.points.data(), frame.points.size()));

		PROPAGATE_ERROR(deserialize(binary, offset, target_count));
		frame.targets.resize(target_count);
		PROPAGATE_ERROR(deserialize_array(binary, offset, frame.targets.data(), frame.targets.size()));

		return true;
	}

	template <>
	bool deserialize<PointCloudRecorder::RecordFrame>(std::vector<uint8_t>& binary, size_t& offset, PointCloudRecorder::RecordFrame& record_frame)
	{
		PROPAGATE_ERROR(deserialize(binary, offset, record_frame.frame));
		PROPAGATE_ERROR(deserialize(binary, offset, record_frame.bytes));
		return true;
	}

	template <>
	bool deserialize<PointCloudRecorder::Bookmark>(std::vector<uint8_t>& binary, size_t& offset, PointCloudRecorder::Bookmark& bookmark)
	{
		PROPAGATE_ERROR(deserialize(binary, offset, bookmark.description));
		PROPAGATE_ERROR(deserialize(binary, offset, bookmark.frameIndex));
		return true;
	}

	constexpr const char* kCopySuffix = " (copy)";

	bool isDigits(const std::string& text)
	{
		if (text.empty())
			return false;

		for (unsigned char ch : text)
		{
			if (!std::isdigit(ch))
				return false;
		}
		return true;
	}

	void parseCopyBase(const std::string& name, std::string& out_base, int& out_next_index)
	{
		const std::string numbered_prefix = std::string(kCopySuffix) + "(";
		if (name.size() > numbered_prefix.size() + 1 && name.back() == ')')
		{
			const std::size_t prefix_pos = name.rfind(numbered_prefix);
			if (prefix_pos != std::string::npos && prefix_pos + numbered_prefix.size() < name.size() - 1)
			{
				const std::string number = name.substr(
					prefix_pos + numbered_prefix.size(),
					name.size() - prefix_pos - numbered_prefix.size() - 1);
				if (isDigits(number) && prefix_pos + numbered_prefix.size() + number.size() + 1 == name.size())
				{
					out_base = name.substr(0, prefix_pos);
					out_next_index = std::stoi(number) + 1;
					return;
				}
			}
		}

		if (name.size() >= std::strlen(kCopySuffix) &&
			name.compare(name.size() - std::strlen(kCopySuffix), std::string::npos, kCopySuffix) == 0)
		{
			out_base = name.substr(0, name.size() - std::strlen(kCopySuffix));
			out_next_index = 1;
			return;
		}

		out_base = name;
		out_next_index = 0;
	}

	std::string makeCopyCandidate(const std::string& base, int index)
	{
		if (index <= 0)
			return base + kCopySuffix;

		return base + kCopySuffix + "(" + std::to_string(index) + ")";
	}
}

PointCloudRecorder::PointCloudRecorder()
{
	m_sensorSpec.hfovDeg = 90.0f;
	m_sensorSpec.vfovDeg = 90.0f;
	m_sensorSpec.sensorWidth = 0.13f;
	m_sensorSpec.sensorHeight = 0.13f;
	m_sensorSpec.posX = 0.0f;
	m_sensorSpec.posY = 0.0f;
	m_sensorSpec.posZ = 0.0f;
	m_sensorSpec.rangeMinX = -2.3f;
	m_sensorSpec.rangeMinY = -5.0f;
	m_sensorSpec.rangeMinZ = -1.9f;
	m_sensorSpec.rangeMaxX = 2.3f;
	m_sensorSpec.rangeMaxY = 5.0f;
	m_sensorSpec.rangeMaxZ = 2.1f;
	m_sensorSpec.yawDeg = 0.0f;
	m_sensorSpec.pitchDeg = 0.0f;
	m_sensorSpec.range = 10.0f;
}

bool PointCloudRecorder::serializeToStream(std::ostream& os)
{
	std::vector<uint8_t> comp;
	uint64_t total_frame_count = 0;

	serialize(comp, pcr::kPayloadMagic);

	for (auto id : m_sessionOrder)
	{
		const auto& session = m_sessions.at(id);
		
		serialize(comp, session.id);
		serialize(comp, session.timestamp);
		serialize(comp, session.lengthUs);
		serialize(comp, session.bytes);
		serialize(comp, session.name);
		serialize(comp, session.description);
		serialize(comp, session.tag);
		serialize(comp, session.frames.size());
		for (const auto& frame : session.frames)
			serialize(comp, frame);
		serialize(comp, session.bookmarks.size());
		for (const auto& bookmark : session.bookmarks)
			serialize(comp, bookmark);

		total_frame_count += session.frames.size();
	}

	const size_t bound = ZSTD_compressBound(comp.size());
	std::vector<uint8_t> comp_buf(bound);
	const size_t compressed_size = ZSTD_compress(
		comp_buf.data(), comp_buf.size(),
		comp.data(), comp.size(),
		ZSTD_CLEVEL_DEFAULT);
	if (ZSTD_isError(compressed_size))
		return false;

	RecordHeader header = pcr::makeFileHeader();
	header.timestamp = get_unix_time_ms();
	header.sensorSpec = m_sensorSpec;
	header.sessionCount = m_sessions.size();
	header.totalFrameCount = total_frame_count;
	std::memset(header.reserved, 0, sizeof(header.reserved));
	header.uncompressedSize = comp.size();
	header.compressedSize = static_cast<uint64_t>(compressed_size);

	os.write(reinterpret_cast<const char*>(&header), sizeof(header));
	os.write(reinterpret_cast<const char*>(comp_buf.data()), static_cast<std::streamsize>(compressed_size));

	comp.clear();

	return true;
}

bool PointCloudRecorder::deserializeFromStream(std::istream & is)
{
	clear();

	if (!is)
		return false;

	RecordHeader header;
	is.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (!pcr::isValidFileHeader(header))
		return false;

	std::vector<uint8_t> comp_buf(header.compressedSize);
	is.read(reinterpret_cast<char*>(comp_buf.data()), header.compressedSize);

	std::vector<uint8_t> binary(header.uncompressedSize);
	const size_t decompressed_size = ZSTD_decompress(
		binary.data(), binary.size(),
		comp_buf.data(), comp_buf.size());
	if (ZSTD_isError(decompressed_size))
		return false;

	if (!deserializeImpl(header, binary))
	{
		clear();
		return false;
	}

	// TODO: Remove later when binary data is fixed
	m_sensorSpec.rangeMinY = 0.0f;

	return true;
}

void PointCloudRecorder::clear()
{
	m_sessions.clear();
	m_sessionOrder.clear();
}

void PointCloudRecorder::setSensorSpec(const pcr::SensorSpec& spec)
{
	m_sensorSpec = spec;
}

const pcr::SensorSpec& PointCloudRecorder::getSensorSpec() const
{
	return m_sensorSpec;
}

void PointCloudRecorder::importFrom(PointCloudRecorder&& other)
{
	for (auto&& [id, session] : other.m_sessions)
	{
		(void)id;
		const SessionID new_id = allocateUnusedSessionId();
		std::string new_name = session.name + " (Imported)";
		int suffix_index = 1;
		while (hasSessionName(new_name))
		{
			new_name = session.name + " (Imported)(" + std::to_string(suffix_index) + ")";
			++suffix_index;
		}

		auto& new_session = m_sessions[new_id];
		new_session = std::move(session);
		new_session.id = new_id;
		new_session.name = std::move(new_name);

		m_sessionOrder.push_back(new_id);
	}
}

PointCloudRecorder::RecordSession* PointCloudRecorder::addSession()
{
	const SessionID new_id = allocateUnusedSessionId();
	const std::string new_name = allocateUnusedSessionName();

	auto& new_session = m_sessions[new_id];
	new_session.name = new_name;
	new_session.id = new_id;
	new_session.timestamp = get_unix_time_ms();
	new_session.bytes = 0;

	m_sessionOrder.push_back(new_id);

	return &new_session;
}

PointCloudRecorder::RecordSession* PointCloudRecorder::splitSession(SessionID id, size_t frame_offset)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		if (auto& session = it->second; frame_offset < session.frames.size())
		{
			const SessionID new_id = allocateUnusedSessionId();
			std::string new_name = session.name + " (Part)";
			int suffix_index = 1;
			while (hasSessionName(new_name))
			{
				new_name = session.name + " (Part)(" + std::to_string(suffix_index) + ")";
				++suffix_index;
			}

			auto& new_session = m_sessions[new_id];
			new_session.id = new_id;
			new_session.name = std::move(new_name);
			new_session.timestamp = get_unix_time_ms();
			new_session.bytes = 0;

			const size_t first_idx = frame_offset;
			const size_t last_idx = session.frames.size();

			new_session.frames.reserve(last_idx - first_idx);

			for (size_t i = first_idx; i < last_idx; ++i)
			{
				uint64_t length = session.frames[i].frame.deltaUs;
				uint64_t bytes = session.frames[i].bytes;
				new_session.frames.push_back(std::move(session.frames[i]));
				session.lengthUs -= length;
				session.bytes -= bytes;
				new_session.lengthUs += length;
				new_session.bytes += bytes;
			}

			size_t idx_off = getOrderedSessionIndex(id);
			m_sessionOrder.insert(m_sessionOrder.begin() + idx_off + 1, new_id);

			return &new_session;
		}
	}

	return nullptr;
}

PointCloudRecorder::RecordSession* PointCloudRecorder::copySession(SessionID id)
{
	auto it = m_sessions.find(id);
	if (it == m_sessions.end())
		return nullptr;

	RecordSession copied = it->second;
	const std::string new_name = makeUniqueCopyName(copied.name);
	const SessionID new_id = allocateUnusedSessionId();
	const size_t idx_off = getOrderedSessionIndex(id);

	copied.id = new_id;
	copied.timestamp = get_unix_time_ms();
	copied.name = new_name;

	auto& new_session = m_sessions[new_id];
	new_session = std::move(copied);
	m_sessionOrder.insert(m_sessionOrder.begin() + idx_off + 1, new_id);
	return &new_session;
}

void PointCloudRecorder::removeSession(SessionID id)
{
	m_sessions.erase(id);

	auto it = std::find(m_sessionOrder.begin(), m_sessionOrder.end(), id);
	if (it != m_sessionOrder.end())
		m_sessionOrder.erase(it);
}

void PointCloudRecorder::clearSession(SessionID id)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
		it->second.frames.clear();
}

void PointCloudRecorder::reorderSession(SessionID id, size_t new_position)
{
	auto it = std::find(m_sessionOrder.begin(), m_sessionOrder.end(), id);

	if (it != m_sessionOrder.end())
	{
		m_sessionOrder.erase(it);
		if (new_position >= m_sessionOrder.size())
			m_sessionOrder.push_back(id);
		else
			m_sessionOrder.insert(m_sessionOrder.begin() + new_position, id);
	}
}

PointCloudRecorder::RecordSession* PointCloudRecorder::getSession(SessionID id)
{
	auto it = m_sessions.find(id);
	return it != m_sessions.end() ? &it->second : nullptr;
}

const PointCloudRecorder::RecordSession* PointCloudRecorder::getSession(SessionID id) const
{
	return const_cast<PointCloudRecorder*>(this)->getSession(id);
}

PointCloudRecorder::RecordSession* PointCloudRecorder::getSessionByIndex(size_t idx)
{
	if (idx < m_sessionOrder.size())
	{
		SessionID id = m_sessionOrder[idx];
		return getSession(id);
	}

	return nullptr;
}

const PointCloudRecorder::RecordSession* PointCloudRecorder::getSessionByIndex(size_t idx) const
{
	return const_cast<PointCloudRecorder*>(this)->getSessionByIndex(idx);
}

SessionID PointCloudRecorder::getOrderedSessionID(size_t idx) const
{
	if (idx < m_sessionOrder.size())
		return m_sessionOrder[idx];
	return -1;
}

size_t PointCloudRecorder::getOrderedSessionIndex(SessionID id) const
{
	auto it = std::find(m_sessionOrder.begin(), m_sessionOrder.end(), id);
	if (it != m_sessionOrder.end())
		return std::distance(m_sessionOrder.begin(), it);
	return static_cast<size_t>(-1);
}

size_t PointCloudRecorder::getSessionCount() const
{
	return m_sessions.size();
}

void PointCloudRecorder::appendFrame(SessionID id, const RecordFrame& frame)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		auto& session = it->second;
		auto byte_size = calc_frame_byte_size(frame.frame);
		session.frames.emplace_back(frame.frame, byte_size);
		session.lengthUs += frame.frame.deltaUs;
		session.bytes += byte_size;
	}
}

void PointCloudRecorder::insertFrame(SessionID id, const RecordFrame& frame, size_t offset)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		assert(offset <= it->second.frames.size());
		
		auto& session = it->second;
		auto byte_size = calc_frame_byte_size(frame.frame);

		if (offset == session.frames.size())
			session.frames.emplace_back(frame.frame, byte_size);
		else
			session.frames.insert(session.frames.begin() + offset, RecordFrame(frame.frame, byte_size));

		session.lengthUs += frame.frame.deltaUs;
		session.bytes += byte_size;
	}
}

void PointCloudRecorder::overwriteFrame(SessionID id, const RecordFrame& frame, size_t offset)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		assert(offset <= it->second.frames.size());

		auto& session = it->second;
		auto byte_size = calc_frame_byte_size(frame.frame);

		if (offset == session.frames.size())
			session.frames.emplace_back(frame.frame, byte_size);
		else
			session.frames[offset] = RecordFrame(frame.frame, byte_size);

		session.lengthUs += frame.frame.deltaUs;
		session.bytes += byte_size;
	}
}

void PointCloudRecorder::removeFrame(SessionID id, size_t offset, size_t count)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		auto& session = it->second;
		if (offset < session.frames.size())
		{
			size_t last_index = std::min(offset + count, session.frames.size());

			for (size_t i = offset; i < last_index; ++i)
			{
				session.lengthUs -= session.frames[i].frame.deltaUs;
				session.bytes -= session.frames[i].bytes;
			}
			session.frames.erase(session.frames.begin() + offset, session.frames.begin() + last_index);
		}
	}
}

PointCloudRecorder::RecordFrame* PointCloudRecorder::getFrame(SessionID id, size_t offset)
{
	if (auto it = m_sessions.find(id); it != m_sessions.end())
	{
		if (auto& entry = it->second; offset < entry.frames.size())
			return &entry.frames[offset];
	}

	return nullptr;
}

const PointCloudRecorder::RecordFrame* PointCloudRecorder::getFrame(SessionID id, size_t offset) const
{
	return const_cast<PointCloudRecorder*>(this)->getFrame(id, offset);
}

bool PointCloudRecorder::empty() const
{
	return m_sessions.empty();
}

SessionID PointCloudRecorder::allocateUnusedSessionId() const
{
	for (SessionID id = 0; ; ++id)
	{
		if (m_sessions.find(id) == m_sessions.end())
			return id;
	}
}

bool PointCloudRecorder::hasSessionName(const std::string& name) const
{
	for (const auto& [id, session] : m_sessions)
	{
		(void)id;
		if (session.name == name)
			return true;
	}
	return false;
}

std::string PointCloudRecorder::allocateUnusedSessionName() const
{
	for (SessionID index = 0; ; ++index)
	{
		std::string name = "Session #" + std::to_string(index);
		if (!hasSessionName(name))
			return name;
	}
}

std::string PointCloudRecorder::makeUniqueCopyName(const std::string& source_name) const
{
	std::string base;
	int next_index = 0;
	parseCopyBase(source_name, base, next_index);

	for (int index = next_index; ; ++index)
	{
		const std::string candidate = makeCopyCandidate(base, index);
		if (!hasSessionName(candidate))
			return candidate;
	}
}

bool PointCloudRecorder::deserializeImpl(const RecordHeader& header, std::vector<uint8_t>& binary)
{
	m_sensorSpec = header.sensorSpec;

	uint64_t magic;
	size_t offset = 0;

	PROPAGATE_ERROR(deserialize(binary, offset, magic));
	if (magic != pcr::kPayloadMagic)
		return false;

	for (uint64_t i = 0; i < header.sessionCount; ++i)
	{
		RecordSession session;
		uint64_t frame_count;
		uint64_t bookmark_count;

		PROPAGATE_ERROR(deserialize(binary, offset, session.id));
		PROPAGATE_ERROR(deserialize(binary, offset, session.timestamp));
		PROPAGATE_ERROR(deserialize(binary, offset, session.lengthUs));
		PROPAGATE_ERROR(deserialize(binary, offset, session.bytes));
		PROPAGATE_ERROR(deserialize(binary, offset, session.name));
		PROPAGATE_ERROR(deserialize(binary, offset, session.description));
		PROPAGATE_ERROR(deserialize(binary, offset, session.tag));
		PROPAGATE_ERROR(deserialize(binary, offset, frame_count));
		session.frames.resize(frame_count);
		for (auto& frame : session.frames)
			PROPAGATE_ERROR(deserialize(binary, offset, frame));
		PROPAGATE_ERROR(deserialize(binary, offset, bookmark_count));
		session.bookmarks.resize(bookmark_count);
		for (auto& bookmark : session.bookmarks)
			PROPAGATE_ERROR(deserialize(binary, offset, bookmark));

		m_sessionOrder.push_back(session.id);
		m_sessions[session.id] = std::move(session);
	}

	return true;
}
