#pragma once

#include "pcr_type.h"

#include <iosfwd>
#include <unordered_map>
#include <vector>

using SessionID = pcr::SessionId;

class PointCloudRecorder
{
public:
	using RecordFrame = pcr::RecordFrame;
	using Bookmark = pcr::Bookmark;
	using RecordSession = pcr::RecordSession;
	using RecordHeader = pcr::FileHeader;

	PointCloudRecorder();

	bool serializeToStream(std::ostream& os);
	bool deserializeFromStream(std::istream& is);

	void clear();

	void setSensorSpec(const pcr::SensorSpec& spec);
	const pcr::SensorSpec& getSensorSpec() const;

	void importFrom(PointCloudRecorder&& other);

	RecordSession* addSession();
	RecordSession* splitSession(SessionID id, size_t frame_offset);
	RecordSession* copySession(SessionID id);
	void removeSession(SessionID id);
	void clearSession(SessionID id);
	void reorderSession(SessionID id, size_t new_position);

	RecordSession* getSession(SessionID id);
	const RecordSession* getSession(SessionID id) const;
	RecordSession* getSessionByIndex(size_t idx);
	const RecordSession* getSessionByIndex(size_t idx) const;

	SessionID getOrderedSessionID(size_t idx) const;
	size_t getOrderedSessionIndex(SessionID id) const;
	size_t getSessionCount() const;

	void appendFrame(SessionID id, const RecordFrame& frame);
	void insertFrame(SessionID id, const RecordFrame& frame, size_t offset);
	void overwriteFrame(SessionID id, const RecordFrame& frame, size_t offset);
	void removeFrame(SessionID id, size_t offset, size_t count = 1);

	RecordFrame* getFrame(SessionID id, size_t offset);
	const RecordFrame* getFrame(SessionID id, size_t offset) const;

	auto sessionOrderBegin() { return m_sessionOrder.begin(); }
	auto sessionOrderEnd() { return m_sessionOrder.end(); }

	bool empty() const;

private:
	SessionID allocateUnusedSessionId() const;
	bool hasSessionName(const std::string& name) const;
	std::string allocateUnusedSessionName() const;
	std::string makeUniqueCopyName(const std::string& source_name) const;
	bool deserializeImpl(const RecordHeader& header, std::vector<std::uint8_t>& binary);

private:
	std::unordered_map<SessionID, RecordSession> m_sessions;
	std::vector<SessionID> m_sessionOrder;

	pcr::SensorSpec m_sensorSpec;
};
