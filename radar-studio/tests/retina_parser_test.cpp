#include "../src/device/retina.h"
#include "../../radar-emulator/src/device/mock_protocol.h"

#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace retina
{
	// Exercise the production parser without opening a TCP connection.
	struct DeviceClientParserTest
	{
		static bool parse(DeviceClient& client, const std::vector<uint8_t>& bytes, Frame& frame)
		{
			return client.parseSinglePacket(bytes, frame);
		}
		static bool extract(DeviceClient& client, std::vector<uint8_t>& stream, std::vector<uint8_t>& packet)
		{
			return client.tryExtractPacket(stream, packet);
		}
	};
}

namespace
{
	using Bytes = std::vector<uint8_t>;
	constexpr size_t networkHeader = 36;
	constexpr size_t frameHeader = 16;
	constexpr size_t targetOffset = 48056;

	void expect(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	void setU32(Bytes& bytes, size_t offset, uint32_t value)
	{
		expect(offset <= bytes.size() && bytes.size() - offset >= sizeof(value), "fixture write bounds");
		std::memcpy(bytes.data() + offset, &value, sizeof(value));
	}

	void updateLength(Bytes& bytes)
	{
		setU32(bytes, 16, static_cast<uint32_t>(bytes.size() - networkHeader));
	}

	retina::Frame sourceFrame()
	{
		retina::Frame frame{};
		frame.frameCount = 42;
		frame.points = { { -1, 2, 3, -0.25f, 18, 7 }, { 4, -5, 6, 0.5f, 20, 7 },
			{ 0.125f, 0.25f, -0.5f, 1, 30, -1 } };
		retina::Target target{};
		target.x = 1.5f;
		target.y = -2.25f;
		target.status = pcr::TargetStatus::Walking;
		target.targetId = 7;
		frame.targets.push_back(target);
		return frame;
	}

	void expectFloat(float actual, float expected)
	{
		expect(std::bit_cast<uint32_t>(actual) == std::bit_cast<uint32_t>(expected), "float32 bits changed");
	}

	void expectPoints(const retina::Frame& actual, const retina::Frame& source)
	{
		expect(actual.frameCount == source.frameCount, "frame count changed");
		expect(actual.points.size() == source.points.size(), "point count changed");
		for (size_t i = 0; i < source.points.size(); ++i)
		{
			const auto& a = actual.points[i];
			const auto& b = source.points[i];
			expectFloat(a.x, b.x); expectFloat(a.y, b.y); expectFloat(a.z, b.z);
			expectFloat(a.doppler, b.doppler); expectFloat(a.power, b.power);
			expect(a.targetId == b.targetId, "int32 target ID changed");
		}
	}

	void expectTarget(const retina::Frame& actual, const retina::Frame& source)
	{
		expectPoints(actual, source);
		expect(actual.targets.size() == 1, "contiguous/fixed target was skipped");
		const auto& a = actual.targets.front();
		const auto& b = source.targets.front();
		expectFloat(a.x, b.x); expectFloat(a.y, b.y);
		expect(a.status == b.status && a.targetId == b.targetId, "target metadata changed");
		expectFloat(a.minx, -1); expectFloat(a.maxx, 4);
		expectFloat(a.miny, -5); expectFloat(a.maxy, 2);
		expectFloat(a.minz, 3); expectFloat(a.maxz, 6);
	}

	struct Fixture
	{
		asio::io_context io;
		std::ostringstream log;
		retina::DeviceClient client{ io, "127.0.0.1", log };
		~Fixture()
		{
			client.shutdown();
			io.run(); // Drain the canceled numeric resolver while client is alive.
		}
		bool parse(const Bytes& bytes, retina::Frame& output)
		{
			return retina::DeviceClientParserTest::parse(client, bytes, output);
		}
		void reject(const Bytes& bytes, const char* diagnostic = nullptr)
		{
			log.str({}); log.clear();
			retina::Frame output{};
			expect(!parse(bytes, output), "malformed packet accepted");
			if (diagnostic) expect(log.str().find(diagnostic) != std::string::npos, "missing rejection diagnostic");
		}
		std::vector<retina::Frame> feed(const Bytes& bytes, size_t chunkSize)
		{
			Bytes stream, packet;
			std::vector<retina::Frame> frames;
			for (size_t offset = 0; offset < bytes.size();)
			{
				const auto count = (std::min)(chunkSize, bytes.size() - offset);
				stream.insert(stream.end(), bytes.begin() + offset, bytes.begin() + offset + count);
				offset += count;
				while (retina::DeviceClientParserTest::extract(client, stream, packet))
				{
					retina::Frame frame{};
					if (parse(packet, frame)) frames.push_back(std::move(frame));
				}
			}
			expect(stream.empty(), "complete stream left buffered bytes");
			return frames;
		}
	};
}

int main()
{
	std::string current;
	try
	{
		Fixture fixture;
		const auto source = sourceFrame();
		const auto packet = mock::encodeRetinaPacket(source);
		const size_t pointEnd = networkHeader + frameHeader + source.points.size() * 24;
		auto pointSource = source;
		pointSource.targets.clear();
		const auto pointOnly = mock::encodeRetinaPacket(pointSource);
		auto fixed = packet;
		fixed.insert(fixed.begin() + pointEnd, targetOffset - pointEnd, 0);
		updateLength(fixed);
		size_t passed = 0;
		const auto run = [&](const char* name, auto test)
		{
			current = name;
			test();
			++passed;
			std::cout << "PASS " << name << '\n';
		};
		run("point-only", [&] {
			retina::Frame output{};
			expect(fixture.parse(pointOnly, output), "point-only rejected");
			expectPoints(output, source);
			expect(output.targets.empty(), "invented target");
		});
		run("emulator-contiguous-target-values", [&] {
			retina::Frame output{};
			expect(fixture.parse(packet, output), "contiguous target rejected");
			expectTarget(output, source);
		});
		run("fixed-offset-target-values", [&] {
			retina::Frame output{};
			expect(fixture.parse(fixed, output), "fixed target rejected");
			expectTarget(output, source);
		});
		run("empty-point-only", [&] {
			retina::Frame empty{}, output{};
			expect(fixture.parse(mock::encodeRetinaPacket(empty), output), "empty point-only rejected");
			expect(output.points.empty() && output.targets.empty(), "empty values changed");
		});
		run("zero-target-count-header", [&] {
			auto bytes = packet; bytes.resize(pointEnd + frameHeader); setU32(bytes, pointEnd + 12, 0); updateLength(bytes);
			retina::Frame output{};
			expect(fixture.parse(bytes, output) && output.targets.empty(), "zero target count rejected");
		});
		run("unknown-trailing-bytes-short-offset", [&] {
			auto bytes = pointOnly; bytes.resize(bytes.size() + 16, 0); updateLength(bytes);
			fixture.reject(bytes, "target offset unavailable");
		});
		run("partial-contiguous-target-header", [&] {
			for (size_t n = 1; n < frameHeader; ++n) {
				auto bytes = packet; bytes.resize(pointEnd + n); updateLength(bytes); fixture.reject(bytes);
			}
		});
		run("partial-fixed-target-header", [&] {
			for (size_t n = 0; n < frameHeader; ++n) {
				auto bytes = fixed; bytes.resize(targetOffset + n); updateLength(bytes);
				fixture.reject(bytes, "truncated target header");
			}
		});
		run("truncated-target-array", [&] {
			auto bytes = packet; bytes.pop_back(); updateLength(bytes); fixture.reject(bytes, "truncated target array");
		});
		run("invalid-target-magic", [&] {
			auto bytes = fixed; setU32(bytes, targetOffset, 0); fixture.reject(bytes, "invalid frame magic for target section");
		});
		run("target-frame-count-mismatch", [&] {
			auto bytes = packet; setU32(bytes, pointEnd + 8, 43); fixture.reject(bytes, "target frame count mismatch");
		});
		run("point-count-overflow", [&] {
			auto bytes = packet; setU32(bytes, 48, (std::numeric_limits<uint32_t>::max)());
			fixture.reject(bytes, "truncated point/ID arrays");
		});
		run("target-count-overflow", [&] {
			auto bytes = packet; setU32(bytes, pointEnd + 12, (std::numeric_limits<uint32_t>::max)());
			fixture.reject(bytes, "truncated target array");
		});
		run("short-point-and-ID-arrays", [&] {
			for (size_t n : { networkHeader + frameHeader + 1, pointEnd - 1 }) {
				auto bytes = pointOnly; bytes.resize(n); updateLength(bytes); fixture.reject(bytes, "truncated point/ID arrays");
			}
		});
		run("all-truncated-packet-prefixes", [&] {
			for (size_t n = 0; n < packet.size(); ++n) fixture.reject(Bytes(packet.begin(), packet.begin() + n));
		});
		run("invalid-network-and-frame-magic", [&] {
			auto bytes = packet; setU32(bytes, 4, 0); fixture.reject(bytes, "invalid packet header");
			bytes = packet; setU32(bytes, 36, 0); fixture.reject(bytes, "invalid frame magic:");
		});
		run("declared-length-overflow", [&] {
			auto bytes = packet; setU32(bytes, 16, (std::numeric_limits<uint32_t>::max)()); fixture.reject(bytes, "invalid packet header");
		});
		run("no-backward-target-seek", [&] {
			auto large = pointSource; large.points.resize(2001);
			auto bytes = mock::encodeRetinaPacket(large); bytes.resize(bytes.size() + 16); updateLength(bytes);
			fixture.reject(bytes, "target offset unavailable");
		});
		run("fragmented-TCP-one-byte", [&] {
			const auto frames = fixture.feed(packet, 1); expect(frames.size() == 1, "fragment loss"); expectTarget(frames[0], source);
		});
		run("coalesced-TCP", [&] {
			auto bytes = packet; bytes.insert(bytes.end(), pointOnly.begin(), pointOnly.end());
			const auto frames = fixture.feed(bytes, bytes.size()); expect(frames.size() == 2, "coalesced loss");
			expectTarget(frames[0], source); expectPoints(frames[1], source);
		});
		run("garbage-and-unprefixed-magic", [&] {
			Bytes bytes{ 0x21, 0x43, 0xcd, 0xab, 0xff, 0xff, 0xff };
			bytes.insert(bytes.end(), packet.begin(), packet.end());
			const auto frames = fixture.feed(bytes, 3); expect(frames.size() == 1, "garbage resync failed"); expectTarget(frames[0], source);
		});
		run("valid-invalid-valid-stream", [&] {
			auto bad = packet; setU32(bad, pointEnd + 12, 99);
			auto bytes = packet; bytes.insert(bytes.end(), bad.begin(), bad.end()); bytes.insert(bytes.end(), packet.begin(), packet.end());
			const auto frames = fixture.feed(bytes, 17); expect(frames.size() == 2, "invalid packet affected next packet");
			for (const auto& frame : frames) expectTarget(frame, source);
		});
		run("invalid-size-header-resynchronization", [&] {
			for (uint32_t length : { 0u, 15u, (std::numeric_limits<uint32_t>::max)() }) {
				auto bytes = packet; bytes.resize(networkHeader); setU32(bytes, 16, length);
				bytes.insert(bytes.end(), packet.begin(), packet.end());
				const auto frames = fixture.feed(bytes, bytes.size()); expect(frames.size() == 1, "length resync failed"); expectTarget(frames[0], source);
			}
		});
		std::cout << "Retina parser tests PASS cases=" << passed << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL " << current << ": " << error.what() << '\n';
		return 1;
	}
}
