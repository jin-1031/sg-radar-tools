#pragma once

#include <common/pcr/point_cloud_recorder.h>
#include <deque>
#include <algorithm>
#include <new>
#include <mutex>
#include <limits>

namespace recording
{
	// Only enqueue() runs on the I/O thread. Session operations remain on the main thread.
	class Capture
	{
	public:
		struct Limits
		{
			size_t frames = 512;
			size_t bytes = 64 * 1024 * 1024;
		};
		struct Status
		{
			uint64_t run = 0;
			uint64_t generation = 0;
			SessionID session = 0;
			int owner = -1;
			size_t initialFrames = 0;
			size_t insertion = 0;
			size_t eligible = 0;
			size_t accepted = 0;
			size_t enqueueFailed = 0;
			size_t applied = 0;
			size_t pending = 0;
			size_t bytes = 0;
			size_t peakBytes = 0;
			size_t peakFrames = 0;
			size_t staleGeneration = 0;
			bool accepting = false;
			bool sinkFailed = false;
			const char* error = nullptr;
			bool complete() const
			{
				return run != 0 && !accepting && !error && pending == 0 && enqueueFailed == 0 &&
					eligible == accepted && accepted == applied;
			}
			bool busy() const { return accepting || pending != 0; }
		};

		Capture() = default;
		explicit Capture(Limits limits) : m_limits(limits) {}

		bool begin(PointCloudRecorder& recorder, SessionID sessionId, int owner, uint64_t generation,
			size_t insertion, size_t maxSessionFrames)
		{
			const auto* session = recorder.getSession(sessionId);
			if (!session || insertion > session->frames.size() || session->frames.size() >= maxSessionFrames)
				return false;
			// Existing edit inconsistencies must not become a successful capture report.
			uint64_t length = 0;
			for (const auto& frame : session->frames)
			{
				if (frame.frame.deltaUs > (std::numeric_limits<uint64_t>::max)() - length) return false;
				length += frame.frame.deltaUs;
			}
			if (length != session->lengthUs) return false;
			std::lock_guard lock(m_mutex);
			if (m_status.busy() || !m_batch.empty() || !m_queue.empty()) return false;
			m_status = {};
			m_status.run = ++m_nextRun;
			m_status.generation = generation;
			m_status.session = sessionId;
			m_status.owner = owner;
			m_status.initialFrames = session->frames.size();
			m_status.insertion = insertion;
			m_status.accepting = true;
			m_frameBudget = maxSessionFrames - session->frames.size();
			m_sinkFailed = false;
			return true;
		}

		void enqueue(const pcr::Frame& frame, uint64_t generation)
		{
			std::lock_guard lock(m_mutex);
			if (!m_status.accepting) return;
			if (generation != m_status.generation)
			{
				++m_status.staleGeneration;
				return;
			}
			++m_status.eligible;
			size_t bytes = sizeof(Item);
			if (bytes > m_limits.bytes || frame.points.size() > (m_limits.bytes - bytes) / sizeof(pcr::Point))
			{
				failEnqueue("queue_bytes");
				return;
			}
			bytes += frame.points.size() * sizeof(pcr::Point);
			if (frame.targets.size() > (m_limits.bytes - bytes) / sizeof(pcr::Target))
			{
				failEnqueue("queue_bytes");
				return;
			}
			bytes += frame.targets.size() * sizeof(pcr::Target);
			if (m_status.pending >= m_limits.frames || bytes > m_limits.bytes - m_status.bytes)
			{
				failEnqueue("queue_capacity");
				return;
			}
			try
			{
				m_queue.push_back({ frame, m_status.run, generation, m_status.accepted + 1, bytes });
			}
			catch (const std::bad_alloc&)
			{
				failEnqueue("allocation_failed");
				return;
			}
			++m_status.accepted;
			++m_status.pending;
			m_status.bytes += bytes;
			m_status.peakBytes = (std::max)(m_status.peakBytes, m_status.bytes);
			m_status.peakFrames = (std::max)(m_status.peakFrames, m_status.pending);
			if (m_status.accepted == m_frameBudget) m_status.accepting = false;
		}

		void stop()
		{
			std::lock_guard lock(m_mutex);
			m_status.accepting = false;
		}

		void pump(PointCloudRecorder& recorder)
		{
			if (m_sinkFailed) return;
			{
				std::lock_guard lock(m_mutex);
				m_batch.swap(m_queue);
			}
			while (!m_batch.empty())
			{
				auto& item = m_batch.front();
				const auto status = snapshot();
				auto* session = recorder.getSession(status.session);
				if (!session || session->frames.size() != status.initialFrames + status.applied ||
					item.run != status.run || item.generation != status.generation || item.sequence != status.applied + 1)
				{
					failSink("destination_changed");
					return;
				}
				if (status.initialFrames == 0 && status.applied == 0) item.frame.deltaUs = 0;
				if (item.frame.deltaUs > (std::numeric_limits<uint64_t>::max)() - session->lengthUs)
				{
					failSink("time_overflow");
					return;
				}
				try
				{
					if (status.insertion + status.applied == session->frames.size())
						recorder.appendFrame(status.session, { item.frame });
					else
						recorder.insertFrame(status.session, { item.frame }, status.insertion + status.applied);
				}
				catch (const std::exception&)
				{
					failSink("recorder_write_failed");
					return;
				}
				{
					std::lock_guard lock(m_mutex);
					++m_status.applied;
					--m_status.pending;
					m_status.bytes -= item.bytes;
				}
				m_batch.pop_front();
			}
		}

		Status snapshot() const
		{
			std::lock_guard lock(m_mutex);
			return m_status;
		}

	private:
		struct Item
		{
			pcr::Frame frame;
			uint64_t run;
			uint64_t generation;
			size_t sequence;
			size_t bytes;
		};
		void failEnqueue(const char* error)
		{
			++m_status.enqueueFailed;
			m_status.error = error;
			m_status.accepting = false;
		}
		void failSink(const char* error)
		{
			std::lock_guard lock(m_mutex);
			m_status.error = error;
			m_status.accepting = false;
			m_status.sinkFailed = true;
			m_sinkFailed = true; // Retain uncommitted batch; never retry a partially failed insert.
		}
		Limits m_limits;
		mutable std::mutex m_mutex;
		std::deque<Item> m_queue;
		std::deque<Item> m_batch;
		Status m_status;
		uint64_t m_nextRun = 0;
		size_t m_frameBudget = 0;
		bool m_sinkFailed = false;
	};
}
