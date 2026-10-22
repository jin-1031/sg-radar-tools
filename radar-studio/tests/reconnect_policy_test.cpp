#include "../src/device/reconnect_policy.h"

#include <iostream>
#include <stdexcept>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	try
	{
		using namespace std::chrono;
		retina::ReconnectPolicy policy;
		auto now = retina::ReconnectPolicy::Clock::time_point{};
		expect(!policy.pending() && policy.attempts() == 0, "initial state");
		for (const int delay : { 2, 4, 8, 15, 30 })
		{
			expect(policy.schedule(now), "retry must be scheduled");
			expect(policy.delaySeconds() == delay, "backoff sequence");
			expect(!policy.schedule(now), "duplicate schedule rejected");
			expect(!policy.ready(now + seconds(delay) - milliseconds(1)), "must not retry early");
			now += seconds(delay);
			expect(policy.ready(now), "retry at deadline");
			policy.started();
			expect(!policy.ready(now), "no duplicate start");
		}
		expect(policy.attempts() == 5 && !policy.schedule(now), "five attempt limit including flapping connections");
		policy.reset();
		expect(!policy.pending() && policy.attempts() == 0, "cancel or stable connection resets budget");
		expect(policy.schedule(now) && policy.delaySeconds() == 2, "new incident starts at first delay");
		policy.reset();
		expect(!policy.ready(now + hours(1)), "cancel removes pending retry");
		std::cout << "Reconnect policy tests PASS\n";
		return 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
