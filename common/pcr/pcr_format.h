#pragma once

#include <cstdint>
#include <cstring>

namespace pcr
{
	using std::int32_t;
	using std::uint8_t;
	using std::uint32_t;
	using std::uint64_t;
	using std::memcmp;
	using std::memcpy;

	inline constexpr char kFileMagic[4] = { 'P', 'C', 'R', '1' };
	inline constexpr char kFileVersion[4] = { '1', '0', '0', '0' };
	inline constexpr uint64_t kPayloadMagic = 0x0807060504030201ULL;

	enum TargetStatus : uint32_t
	{
		Standing = 0,
		Sitting = 1,
		Lying = 2,
		Walking = 4,
	};

	struct Point
	{
		float x;
		float y;
		float z;
		float doppler;
		float power;
		int32_t targetId;
	};

	struct Target
	{
		float x;
		float y;
		TargetStatus status;
		uint32_t targetId;
		float minx;
		float maxx;
		float miny;
		float maxy;
		float minz;
		float maxz;
	};

	struct SensorSpec
	{
		float hfovDeg = 90.0f;
		float vfovDeg = 90.0f;
		float sensorWidth = 0.13f;
		float sensorHeight = 0.13f;
		float posX = 0.0f;
		float posY = 0.0f;
		float posZ = 0.0f;
		float yawDeg = 0.0f;
		float pitchDeg = 0.0f;
		float rangeMinX = -2.3f;
		float rangeMinY = -5.0f;
		float rangeMinZ = -1.9f;
		float rangeMaxX = 2.3f;
		float rangeMaxY = 5.0f;
		float rangeMaxZ = 2.1f;
		float range = 10.0f;
	};

	// Disk layout, little-endian. Written at offset 0, followed by a zstd payload.
	//
	//   [FileHeader]            magic "PCR1", version "1000" (PCR 1.0)
	//   [zstd payload]
	//
	// Decompressed payload:
	//   uint64      payloadMagic (= kPayloadMagic)
	//   session[sessionCount]:
	//     uint32    id
	//     uint64    timestamp, lengthUs, bytes
	//     string    name, description, tag     (uint64 length + bytes)
	//     uint64    frameCount
	//     recordFrame[frameCount]:
	//       frame:
	//         uint32 packetSize, frameCount
	//         uint64 deltaUs
	//         uint64 pointCount + Point[pointCount]
	//         uint64 targetCount + Target[targetCount]
	//       uint64 bytes
	//     uint64    bookmarkCount
	//     bookmark[bookmarkCount]:
	//       string description
	//       uint64 frameIndex
#pragma pack(push, 1)
	struct FileHeader
	{
		char magic[4];
		char version[4];
		uint64_t timestamp;
		SensorSpec sensorSpec;
		uint64_t sessionCount;
		uint64_t totalFrameCount;
		uint8_t reserved[32];
		uint64_t uncompressedSize;
		uint64_t compressedSize;
	};
#pragma pack(pop)

	static_assert(sizeof(Point) == 24, "pcr::Point disk size");
	static_assert(sizeof(Target) == 40, "pcr::Target disk size");
	static_assert(sizeof(SensorSpec) == 64, "pcr::SensorSpec disk size");
	static_assert(sizeof(FileHeader) == 144, "pcr::FileHeader disk size");

	inline bool isValidFileHeader(const FileHeader& header)
	{
		return memcmp(header.magic, kFileMagic, sizeof(kFileMagic)) == 0
			&& memcmp(header.version, kFileVersion, sizeof(kFileVersion)) == 0;
	}

	inline FileHeader makeFileHeader()
	{
		FileHeader header{};
		memcpy(header.magic, kFileMagic, sizeof(kFileMagic));
		memcpy(header.version, kFileVersion, sizeof(kFileVersion));
		return header;
	}
}
