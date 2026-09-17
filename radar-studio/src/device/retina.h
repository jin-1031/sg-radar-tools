#pragma once

#include <common/pcr/pcr_type.h>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace retina
{
	using asio::ip::tcp;

	using pcr::TargetStatus;
	using pcr::Point;
	using pcr::Target;
	using pcr::Frame;
	using pcr::SensorSpec;

	struct DeviceInfo
	{
		std::string ip;
		std::string mac;
		std::string model;
		SensorSpec sensorSpec{};
	};

	static const char* to_string(retina::TargetStatus status)
	{
		switch (status)
		{
			case retina::TargetStatus::Standing: return "Standing";
			case retina::TargetStatus::Sitting: return "Sitting";
			case retina::TargetStatus::Lying: return "Lying";
			case retina::TargetStatus::Walking: return "Walking";
			default: return "Unknown";
		}
	}

	class DeviceFinder
	{
	public:
		enum class Result
		{
			Success,
			Canceled,
			Failed
		};

		DeviceFinder(std::ostream& log);

		// synchronous function which finds the first device in the local network and returns its IP address
		Result find(const std::string& local_ip, const std::string& subnet_mask, std::string& out_host);

		// async function which cancels the ongoing find operation
		void cancel();

	private:
		bool buildScanRange(const std::string& local_ip, const std::string& subnet_mask, uint32_t& out_network, uint32_t& out_first, uint32_t& out_last);
		bool probeHost(asio::io_context& io, const std::string& ip);
		bool probeTcpPort(asio::io_context& io, const std::string& ip, uint16_t port, std::chrono::milliseconds timeout);
		bool probeHttpPort(asio::io_context& io, const std::string& ip, std::chrono::milliseconds timeout);
		std::string formatAddress(uint32_t address) const;

	private:
		std::ostream& m_log;

		std::vector<asio::io_context*> m_ioContexts;
		std::mutex m_mutex;
		std::atomic_bool m_canceled;
	};

	class DeviceClient
	{
	public:
		static constexpr uint32_t kFrameCountLimitDefault = 60;

		DeviceClient(asio::io_context& io, const std::string& host, std::ostream& log_stream);
		~DeviceClient();

		void setOnConnected(std::function<void()> callback);
		void setOnDisconnected(std::function<void()> callback);
		void setOnFrame(std::function<void(const Frame&)> callback);

		void setFrameCountLimit(uint32_t limit);

		void getFrames(std::function<void(const std::deque<Frame>&)> callback) const;
		double getFrameRate() const;
		double getBandwidthMbps(uint64_t interval_ms = 1000) const;

		const DeviceInfo& getDeviceInfo() const;

		void shutdown();
	
	private:
		void connectAsync(tcp::resolver::results_type endpoints);
		void doRead();
		bool tryExtractPacket(std::vector<uint8_t>& stream_buf, std::vector<uint8_t>& out_packet_buf);
		bool parseSinglePacket(const std::vector<uint8_t>& packet_buf, Frame& out_frame);
		bool findPacketMagic(const std::vector<uint8_t>& buffer, size_t& out_offset);

	private:
		asio::io_context& m_io;
		std::ostream& m_log;
		tcp::resolver m_resolver;
		tcp::socket m_socket;

		uint32_t m_frameCountLimit;
		std::vector<uint8_t> m_readBuf {};
		std::vector<uint8_t> m_streamBuf;
		std::vector<uint8_t> m_packetBuf;
		mutable std::deque<Frame> m_frames;
		mutable std::mutex m_mutex;

		std::function<void()> m_onConnected;
		std::function<void()> m_onDisconnected;
		std::function<void(const Frame&)> m_onFrame;

		uint64_t m_lastFrameTimepoint = 0;
		uint64_t m_totalDurationUs = 0;
		size_t m_totalFrames = 0;

		mutable std::deque<std::pair<uint64_t, size_t>> m_bandwidthSamples;
		mutable size_t m_bandwidthTotalBytes = 0;

		DeviceInfo m_deviceInfo;
	};
}