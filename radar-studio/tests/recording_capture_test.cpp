#include "../src/recording_capture.h"
#include "../src/device/retina.h"
#include "../../radar-emulator/src/device/mock_protocol.h"

#include <bit>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace retina
{
	struct DeviceClientParserTest
	{
		static bool parseAt(DeviceClient& client, const std::vector<uint8_t>& packet, Frame& frame, uint64_t now)
		{
			if (!client.parseSinglePacket(packet, frame)) return false;
			client.stampSuccessfulFrame(frame, now);
			return true;
		}
	};
}

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	pcr::Frame frame(uint32_t number, uint64_t delta = 50000)
	{
		pcr::Frame result{};
		result.packetSize = 108;
		result.frameCount = number;
		result.deltaUs = delta;
		result.points.push_back({ 1.25f, -2.5f, 3, -0.25f, 18, 7 });
		pcr::Target target{};
		target.x = 1.5f;
		target.y = -2.25f;
		target.status = pcr::TargetStatus::Walking;
		target.targetId = 7;
		target.minx = target.maxx = 1.25f;
		target.miny = target.maxy = -2.5f;
		target.minz = target.maxz = 3;
		result.targets.push_back(target);
		return result;
	}

	void sameFrame(const pcr::Frame& a, const pcr::Frame& b)
	{
		expect(a.frameCount == b.frameCount && a.packetSize == b.packetSize && a.deltaUs == b.deltaUs, "frame/time mismatch");
		expect(a.points.size() == b.points.size() && a.targets.size() == b.targets.size(), "point/target count mismatch");
		for (size_t i = 0; i < a.points.size(); ++i)
			expect(std::memcmp(&a.points[i], &b.points[i], sizeof(pcr::Point)) == 0, "point bits changed");
		for (size_t i = 0; i < a.targets.size(); ++i)
			expect(std::memcmp(&a.targets[i], &b.targets[i], sizeof(pcr::Target)) == 0, "target bits changed");
	}

	void roundTrip(PointCloudRecorder& recorder)
	{
		std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
		expect(recorder.serializeToStream(stream), "serialize failed");
		stream.seekg(0);
		PointCloudRecorder loaded;
		expect(loaded.deserializeFromStream(stream), "reload failed");
		expect(loaded.getSessionCount() == recorder.getSessionCount(), "session count changed");
		for (size_t i = 0; i < recorder.getSessionCount(); ++i)
		{
			const auto* a = recorder.getSessionByIndex(i);
			const auto* b = loaded.getSessionByIndex(i);
			expect(a->id == b->id && a->frames.size() == b->frames.size(), "reload frame count changed");
			expect(a->lengthUs == b->lengthUs && a->bytes == b->bytes, "reload totals changed");
			for (size_t j = 0; j < a->frames.size(); ++j) sameFrame(a->frames[j].frame, b->frames[j].frame);
		}
	}

	void complete(const recording::Capture& capture, size_t count)
	{
		const auto s = capture.snapshot();
		expect(s.complete() && s.eligible == count && s.accepted == count && s.applied == count, "capture count invariant");
		expect(s.pending == 0 && s.enqueueFailed == 0 && s.bytes == 0, "unfinished capture");
	}

	void testUnit()
	{
		size_t passed = 0;
		const auto run = [&](const char* name, auto test)
		{
			try { test(); }
			catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
			std::cout << "PASS " << name << '\n';
			++passed;
		};
		run("batch-before-render-and-roundtrip", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 0, 1000), "begin");
			for (unsigned i = 0; i < 100; ++i) capture.enqueue(frame(i / 2), 1); // duplicate sensor IDs remain distinct frames
			expect(recorder.getSession(id)->frames.empty(), "producer wrote Session");
			capture.stop();
			capture.pump(recorder);
			complete(capture, 100);
			const auto* session = recorder.getSession(id);
			expect(session->lengthUs == 99 * 50000, "elapsed time");
			for (unsigned i = 0; i < 100; ++i) sameFrame(session->frames[i].frame, frame(i / 2, i == 0 ? 0 : 50000));
			roundTrip(recorder);
		});
		run("start-stop-boundary-owned-copy", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture;
			capture.enqueue(frame(0), 1);
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			auto input = frame(1);
			capture.enqueue(input, 1);
			input.points.clear();
			capture.stop();
			capture.enqueue(frame(2), 1);
			capture.pump(recorder);
			complete(capture, 1);
			sameFrame(recorder.getSession(id)->frames[0].frame, frame(1, 0));
		});
		run("single-writer-and-selection-independent", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			const auto other = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			expect(!capture.begin(recorder, other, 1, 1, 0, 100), "second writer allowed");
			capture.enqueue(frame(1), 1);
			capture.stop(); capture.pump(recorder);
			expect(recorder.getSession(other)->frames.empty(), "selection redirected capture");
			complete(capture, 1);
		});
		run("generation-and-new-session", [] {
			PointCloudRecorder recorder;
			const auto first = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, first, 0, 1, 0, 100), "begin");
			capture.enqueue(frame(9), 1); capture.stop(); capture.pump(recorder);
			const auto second = recorder.addSession()->id;
			expect(capture.begin(recorder, second, 0, 2, 0, 100), "restart");
			capture.enqueue(frame(10), 1);
			capture.enqueue(frame(1, 999999), 2);
			capture.stop(); capture.pump(recorder);
			complete(capture, 1);
			expect(capture.snapshot().staleGeneration == 1, "old generation not tracked");
			expect(recorder.getSession(first)->frames.size() == 1 && recorder.getSession(second)->frames[0].frame.deltaUs == 0, "session isolation");
			roundTrip(recorder);
		});
		run("frame-capacity-failure-preserves-prefix", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture({ 2, 100000 });
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			for (unsigned i = 0; i < 4; ++i) capture.enqueue(frame(i), 1);
			capture.pump(recorder);
			const auto s = capture.snapshot();
			expect(!s.complete() && s.eligible == 3 && s.accepted == 2 && s.enqueueFailed == 1 && s.applied == 2 && s.pending == 0, "overflow falsely succeeded");
			roundTrip(recorder);
		});
		run("byte-capacity-failure", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture({ 100, 1 });
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			capture.enqueue(frame(0), 1); capture.pump(recorder);
			expect(capture.snapshot().enqueueFailed == 1 && !capture.snapshot().complete() && recorder.getSession(id)->frames.empty(), "byte limit");
		});
		run("max-frame-limit-exact-boundary", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 0, 2), "begin");
			for (unsigned i = 0; i < 5; ++i) capture.enqueue(frame(i), 1);
			capture.pump(recorder); complete(capture, 2);
		});
		run("resume-insertion-preserves-existing-deltas", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recorder.appendFrame(id, { frame(10, 12345) });
			recorder.appendFrame(id, { frame(20, 77777) });
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 1, 100), "resume");
			capture.enqueue(frame(11, 50000), 1); capture.stop(); capture.pump(recorder);
			complete(capture, 1);
			const auto* s = recorder.getSession(id);
			sameFrame(s->frames[0].frame, frame(10, 12345));
			sameFrame(s->frames[1].frame, frame(11, 50000));
			sameFrame(s->frames[2].frame, frame(20, 77777));
			roundTrip(recorder);
		});
		run("snapshot-during-active-capture", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			capture.enqueue(frame(1), 1); capture.pump(recorder);
			expect(capture.snapshot().busy() && !capture.snapshot().complete(), "snapshot finalized capture");
			roundTrip(recorder);
			capture.enqueue(frame(2), 1); capture.stop(); capture.pump(recorder); complete(capture, 2);
		});
		run("deleted-destination-fails-with-retained-batch", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture;
			expect(capture.begin(recorder, id, 0, 1, 0, 100), "begin");
			capture.enqueue(frame(1), 1);
			recorder.removeSession(id); // Simulates a caller bypassing the UI edit guard.
			capture.pump(recorder);
			expect(capture.snapshot().sinkFailed && capture.snapshot().pending == 1 && !capture.snapshot().complete(), "missing Session was silently dropped");
		});
		run("inconsistent-legacy-edit-rejected", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recorder.appendFrame(id, { frame(1) });
			recorder.clearSession(id);
			recording::Capture capture;
			expect(!capture.begin(recorder, id, 0, 1, 0, 100), "known legacy edit inconsistency hidden");
		});
		run("concurrent-producer-batch-consumer", [] {
			PointCloudRecorder recorder;
			const auto id = recorder.addSession()->id;
			recording::Capture capture({ 2048, 64 * 1024 * 1024 });
			expect(capture.begin(recorder, id, 0, 1, 0, 2000), "begin");
			std::atomic<bool> done{ false };
			std::thread producer([&] {
				for (unsigned i = 0; i < 1000; ++i) capture.enqueue(frame(i), 1);
				done = true;
			});
			while (!done.load()) { capture.pump(recorder); std::this_thread::yield(); }
			producer.join(); capture.stop(); capture.pump(recorder); complete(capture, 1000);
			for (unsigned i = 0; i < 1000; ++i) expect(recorder.getSession(id)->frames[i].frame.frameCount == i, "order changed");
		});
		run("success-failure-success-clock", [] {
			asio::io_context io;
			std::ostringstream log;
			retina::DeviceClient client(io, "127.0.0.1", log);
			const auto good = mock::encodeRetinaPacket(frame(1));
			auto bad = good; bad[36] = 0;
			pcr::Frame first{}, failed{}, next{};
			const bool firstOk = retina::DeviceClientParserTest::parseAt(client, good, first, 0);
			const bool badOk = retina::DeviceClientParserTest::parseAt(client, bad, failed, 50000);
			const bool nextOk = retina::DeviceClientParserTest::parseAt(client, good, next, 100000);
			client.shutdown(); io.run();
			expect(firstOk && !badOk && nextOk && first.deltaUs == 0 && next.deltaUs == 100000, "failed parse advanced success clock");
		});
		std::cout << "Recording capture unit tests PASS cases=" << passed << '\n';
	}

	void loopback(bool reset)
	{
		asio::io_context io;
		retina::tcp::acceptor acceptor(io);
		acceptor.open(retina::tcp::v4());
		acceptor.set_option(asio::socket_base::reuse_address(true));
		acceptor.bind({ asio::ip::address_v4::loopback(), mock::kDevicePort });
		acceptor.listen();
		retina::tcp::socket server(io);
		asio::steady_timer closeTimer(io);
		PointCloudRecorder recorder;
		const auto id = recorder.addSession()->id;
		recording::Capture capture;
		expect(capture.begin(recorder, id, 0, 1, 0, 100), "loopback begin");
		auto bytes = mock::encodeRetinaPacket(frame(1));
		const auto second = mock::encodeRetinaPacket(frame(2));
		bytes.insert(bytes.end(), second.begin(), second.end());
		acceptor.async_accept(server, [&](const std::error_code& ec) {
			if (ec) return;
			asio::async_write(server, asio::buffer(bytes), [&](const std::error_code& writeError, size_t) {
				if (writeError) return;
				closeTimer.expires_after(std::chrono::milliseconds(100));
				closeTimer.async_wait([&](const std::error_code& timerError) {
					if (timerError) return;
					std::error_code ignored;
					if (reset) server.set_option(asio::socket_base::linger(true, 0), ignored);
					else server.shutdown(retina::tcp::socket::shutdown_send, ignored);
					server.close(ignored);
				});
			});
		});
		std::ostringstream log;
		bool disconnected = false;
		retina::DeviceClient client(io, "127.0.0.1", log);
		client.setOnFrame([&](const pcr::Frame& f) { capture.enqueue(f, 1); });
		client.setOnDisconnected([&] { disconnected = true; capture.stop(); });
		io.run_for(std::chrono::seconds(3));
		client.shutdown();
		std::error_code ignored;
		acceptor.close(ignored); server.close(ignored); closeTimer.cancel();
		io.restart(); io.run();
		capture.stop(); capture.pump(recorder);
		std::cout << log.str();
		expect(disconnected, "EOF/RST not observed");
		complete(capture, 2);
		expect(recorder.getSession(id)->frames[0].frame.deltaUs == 0, "first delta");
		for (size_t i = 0; i < 2; ++i) {
			auto expected = frame(static_cast<uint32_t>(i + 1), recorder.getSession(id)->frames[i].frame.deltaUs);
			expected.packetSize = static_cast<uint32_t>(second.size() - 36);
			sameFrame(recorder.getSession(id)->frames[i].frame, expected);
		}
		roundTrip(recorder);
		std::cout << "PASS loopback " << (reset ? "RST" : "FIN") << " eligible=2 accepted=2 applied=2 reloaded=2 pending=0\n";
	}
}

int wmain(int argc, wchar_t** argv)
{
	try
	{
		if (argc == 2 && std::wstring_view(argv[1]) == L"--loopback") { loopback(false); loopback(true); }
		else if (argc == 3 && std::wstring_view(argv[1]) == L"--pcr") {
			std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
			PointCloudRecorder recorder;
			expect(recorder.deserializeFromStream(input), "legacy PCR load failed");
			roundTrip(recorder);
			std::cout << "Legacy PCR frame/point/target/delta roundtrip PASS sessions=" << recorder.getSessionCount() << '\n';
		}
		else if (argc == 1) testUnit();
		else throw std::runtime_error("invalid arguments");
		return 0;
	}
	catch (const std::system_error& e) { std::cerr << "FAIL recording capture native code=" << e.code().value() << " category=" << e.code().category().name() << " message=" << e.what() << '\n'; return 1; }
	catch (const std::exception& e) { std::cerr << "FAIL recording capture: " << e.what() << '\n'; return 1; }
}
