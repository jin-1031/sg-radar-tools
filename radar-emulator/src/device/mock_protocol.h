#pragma once

#include "../../../radar-studio/src/device/retina.h"

#include <cstdint>
#include <vector>

namespace mock
{
	inline constexpr std::uint16_t kHttpPort = 8000;
	inline constexpr std::uint16_t kDevicePort = 29172;

	std::vector<std::uint8_t> encodeRetinaPacket(const retina::Frame& frame);
}
