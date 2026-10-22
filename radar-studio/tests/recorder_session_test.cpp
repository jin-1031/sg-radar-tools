#include <common/pcr/point_cloud_recorder.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	try
	{
		PointCloudRecorder recorder;
		auto* oldSession = recorder.addSession();
		const auto oldId = oldSession->id;
		pcr::Frame first{};
		first.frameCount = 123;
		first.deltaUs = 50000;
		first.points.push_back({});
		first.points.front().x = 1.25f;
		first.points.front().targetId = 255;
		recorder.appendFrame(oldId, { first });
		// Recovery must use addSession, not clear/import or an old-session overwrite.
		auto* nextSession = recorder.addSession();
		expect(nextSession->id != oldId, "new session identity");
		pcr::Frame next = first;
		next.frameCount = 1;
		recorder.appendFrame(nextSession->id, { next });
		expect(recorder.getSession(oldId)->frames.size() == 1, "old session is preserved");
		expect(recorder.getSession(oldId)->frames.front().frame.frameCount == 123, "old frame is unchanged");
		std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
		expect(recorder.serializeToStream(binary), "existing serializer succeeds");
		binary.seekg(0);
		PointCloudRecorder loaded;
		expect(loaded.deserializeFromStream(binary), "existing reader accepts recording");
		expect(loaded.getSessionCount() == 2, "Save preserves both sessions");
		for (std::size_t i = 0; i < 2; ++i)
		{
			const auto* session = loaded.getSessionByIndex(i);
			expect(session && session->frames.size() == 1, "no additional blank frame in fixture");
			const auto& frame = session->frames.front().frame;
			expect(frame.frameCount == (i == 0 ? 123u : 1u), "frame counts survive roundtrip");
			expect(frame.points.size() == 1 && frame.points.front().x == 1.25f &&
				frame.points.front().targetId == 255, "point values survive roundtrip");
		}
		std::cout << "Recorder session roundtrip tests PASS\n";
		return 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
