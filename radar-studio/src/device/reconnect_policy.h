#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace retina
{
	// Scheduling only; connection ownership remains in MyApp.
	class ReconnectPolicy
	{
	public:
		using Clock = std::chrono::steady_clock;
		bool schedule(Clock::time_point now)
		{
			if (m_pending || m_attempts == kDelays.size()) return false;
			m_due = now + std::chrono::seconds(kDelays[m_attempts++]);
			m_pending = true;
			return true;
		}
		bool ready(Clock::time_point now) const { return m_pending && now >= m_due; }
		void started() { m_pending = false; }
		void reset() { m_pending = false; m_attempts = 0; }
		bool pending() const { return m_pending; }
		std::size_t attempts() const { return m_attempts; }
		int delaySeconds() const { return m_attempts == 0 ? 0 : kDelays[m_attempts - 1]; }

	private:
		inline static constexpr std::array<int, 5> kDelays{ 2, 4, 8, 15, 30 };
		std::size_t m_attempts = 0;
		bool m_pending = false;
		Clock::time_point m_due{};
	};
}
