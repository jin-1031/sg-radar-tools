#include "mock_protocol.h"

#include <cstring>

namespace
{
	constexpr std::uint32_t packetMagic = 0xABCD4321u;
	constexpr std::uint64_t frameMagic = 0x0807060504030201ull;

	struct RetinaPacketHeaderRaw
	{
		std::uint32_t reserved0 = 0;
		std::uint32_t magic = packetMagic;
		std::uint32_t reserved1 = 0;
		std::uint32_t reserved2 = 0;
		std::uint32_t packageSize = 0;
		std::uint32_t reserved3 = 0;
		std::uint32_t reserved4 = 0;
		std::uint32_t reserved5 = 0;
		std::uint32_t reserved6 = 0;
	};

	struct RetinaFrameHeaderRaw
	{
		std::uint64_t magic = frameMagic;
		std::uint32_t frameCount = 0;
		std::uint32_t targetNumber = 0;
	};

	struct RetinaTargetRaw
	{
		float x = 0.0f;
		float y = 0.0f;
		retina::TargetStatus status = retina::TargetStatus::Standing;
		std::uint32_t targetId = 0;
		float reserved0 = 0.0f;
		float reserved1 = 0.0f;
		float reserved2 = 0.0f;
	};

	template <typename T>
	void appendBytes(std::vector<std::uint8_t>& bytes, const T& value)
	{
		const std::size_t offset = bytes.size();
		bytes.resize(offset + sizeof(T));
		std::memcpy(bytes.data() + offset, &value, sizeof(T));
	}
}

std::vector<std::uint8_t> mock::encodeRetinaPacket(const retina::Frame& frame)
{
	std::vector<std::uint8_t> payload;
	payload.reserve(
		sizeof(RetinaFrameHeaderRaw) * 2 +
		frame.points.size() * (sizeof(float) * 5 + sizeof(std::int32_t)) +
		frame.targets.size() * sizeof(RetinaTargetRaw));

	RetinaFrameHeaderRaw pointHeader;
	pointHeader.frameCount = frame.frameCount;
	pointHeader.targetNumber = static_cast<std::uint32_t>(frame.points.size());
	appendBytes(payload, pointHeader);

	for (const retina::Point& point : frame.points)
	{
		appendBytes(payload, point.x);
		appendBytes(payload, point.y);
		appendBytes(payload, point.z);
		appendBytes(payload, point.doppler);
		appendBytes(payload, point.power);
	}

	for (const retina::Point& point : frame.points)
	{
		appendBytes(payload, point.targetId);
	}

	if (!frame.targets.empty())
	{
		RetinaFrameHeaderRaw targetHeader;
		targetHeader.frameCount = frame.frameCount;
		targetHeader.targetNumber = static_cast<std::uint32_t>(frame.targets.size());
		appendBytes(payload, targetHeader);

		for (const retina::Target& target : frame.targets)
		{
			RetinaTargetRaw raw;
			raw.x = target.x;
			raw.y = target.y;
			raw.status = target.status;
			raw.targetId = target.targetId;
			appendBytes(payload, raw);
		}
	}

	RetinaPacketHeaderRaw packetHeader;
	packetHeader.packageSize = static_cast<std::uint32_t>(payload.size());

	std::vector<std::uint8_t> packet;
	packet.reserve(sizeof(RetinaPacketHeaderRaw) + payload.size());
	appendBytes(packet, packetHeader);
	packet.insert(packet.end(), payload.begin(), payload.end());
	return packet;
}
