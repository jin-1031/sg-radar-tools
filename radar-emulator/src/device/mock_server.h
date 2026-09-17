#pragma once

#include "../../../radar-studio/src/device/retina.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

class MockServer
{
public:
	struct Stats
	{
		bool running = false;
		std::string bindAddress = "0.0.0.0";
		std::uint16_t httpPort = 0;
		std::uint16_t devicePort = 0;
		int clientCount = 0;
		std::string lastError;
	};

	explicit MockServer(std::ostream& log_stream);
	~MockServer();

	bool start();
	void stop();
	void broadcastFrame(const retina::Frame& frame);

	Stats getStats() const;

private:
	class ClientSession;

	void startHttpAccept();
	void startDeviceAccept();
	void handleHttpClient(const std::shared_ptr<retina::tcp::socket>& socket);
	void onClientDisconnected(const std::shared_ptr<ClientSession>& session);
	void setLastError(const std::string& error);

	std::ostream& m_log;

	asio::io_context m_io;
	std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;
	std::unique_ptr<retina::tcp::acceptor> m_httpAcceptor;
	std::unique_ptr<retina::tcp::acceptor> m_deviceAcceptor;

	std::vector<std::shared_ptr<ClientSession>> m_clients;
	std::atomic_int m_clientCount = 0;

	std::thread m_thread;
	std::atomic_bool m_running = false;

	mutable std::mutex m_statusMutex;
	std::string m_lastError;
};
