#include "mock_server.h"

#include "mock_protocol.h"

#include <algorithm>
#include <array>
#include <deque>

namespace
{
	const char* httpResponse =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 2\r\n"
		"Connection: close\r\n"
		"\r\n"
		"OK";
}

class MockServer::ClientSession : public std::enable_shared_from_this<MockServer::ClientSession>
{
public:
	ClientSession(asio::io_context& io, MockServer& owner) :
		socket(io),
		m_owner(owner)
	{
	}

	void start()
	{
		doRead();
	}

	void enqueue(const std::shared_ptr<const std::vector<std::uint8_t>>& packet)
	{
		const bool was_idle = m_writeQueue.empty();
		m_writeQueue.push_back(packet);
		if (was_idle)
			doWrite();
	}

public:
	retina::tcp::socket socket;

private:
	void doRead()
	{
		auto self = shared_from_this();
		socket.async_read_some(asio::buffer(m_readBuffer), [this, self](const std::error_code& ec, std::size_t)
		{
			if (ec)
			{
				m_owner.onClientDisconnected(self);
				return;
			}

			doRead();
		});
	}

	void doWrite()
	{
		if (m_writeQueue.empty())
			return;

		auto self = shared_from_this();
		auto packet = m_writeQueue.front();
		asio::async_write(socket, asio::buffer(*packet), [this, self](const std::error_code& ec, std::size_t)
		{
			if (ec)
			{
				m_owner.onClientDisconnected(self);
				return;
			}

			m_writeQueue.pop_front();
			if (!m_writeQueue.empty())
				doWrite();
		});
	}

private:
	MockServer& m_owner;
	std::array<char, 64> m_readBuffer {};
	std::deque<std::shared_ptr<const std::vector<std::uint8_t>>> m_writeQueue;
};

MockServer::MockServer(std::ostream& log_stream) :
	m_log(log_stream)
{
}

MockServer::~MockServer()
{
	stop();
}

bool MockServer::start()
{
	stop();

	m_io.restart();
	m_workGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(m_io));

	std::error_code ec;
	m_httpAcceptor = std::make_unique<retina::tcp::acceptor>(m_io);
	m_httpAcceptor->open(retina::tcp::v4(), ec);
	if (ec)
	{
		setLastError("Failed to open HTTP acceptor.");
		stop();
		return false;
	}
	m_httpAcceptor->set_option(asio::socket_base::reuse_address(true), ec);
	m_httpAcceptor->bind(retina::tcp::endpoint(retina::tcp::v4(), mock::kHttpPort), ec);
	if (ec)
	{
		setLastError("Failed to bind HTTP port 8000.");
		stop();
		return false;
	}
	m_httpAcceptor->listen(asio::socket_base::max_listen_connections, ec);
	if (ec)
	{
		setLastError("Failed to listen on HTTP port 8000.");
		stop();
		return false;
	}

	m_deviceAcceptor = std::make_unique<retina::tcp::acceptor>(m_io);
	m_deviceAcceptor->open(retina::tcp::v4(), ec);
	if (ec)
	{
		setLastError("Failed to open device acceptor.");
		stop();
		return false;
	}
	m_deviceAcceptor->set_option(asio::socket_base::reuse_address(true), ec);
	m_deviceAcceptor->bind(retina::tcp::endpoint(retina::tcp::v4(), mock::kDevicePort), ec);
	if (ec)
	{
		setLastError("Failed to bind device port 29172.");
		stop();
		return false;
	}
	m_deviceAcceptor->listen(asio::socket_base::max_listen_connections, ec);
	if (ec)
	{
		setLastError("Failed to listen on device port 29172.");
		stop();
		return false;
	}

	startHttpAccept();
	startDeviceAccept();

	m_running = true;
	setLastError({});
	m_thread = std::thread([this]()
	{
		try
		{
			m_io.run();
		}
		catch (const std::exception& exception)
		{
			setLastError(exception.what());
		}
	});

	m_log << "[info] mock server: listening on 0.0.0.0:" << mock::kHttpPort
		<< " (HTTP), 0.0.0.0:" << mock::kDevicePort << " (stream)\n";
	return true;
}

void MockServer::stop()
{
	const bool was_running = m_running.exchange(false);

	if (m_httpAcceptor || m_deviceAcceptor || !m_clients.empty())
	{
		asio::post(m_io, [this]()
		{
			std::error_code ec;
			if (m_httpAcceptor)
				m_httpAcceptor->close(ec);
			if (m_deviceAcceptor)
				m_deviceAcceptor->close(ec);

			for (const auto& client : m_clients)
			{
				if (!client)
					continue;
				client->socket.shutdown(retina::tcp::socket::shutdown_both, ec);
				client->socket.close(ec);
			}
			m_clients.clear();
			m_clientCount = 0;
		});
	}

	if (m_workGuard)
		m_workGuard.reset();

	m_io.stop();

	if (m_thread.joinable())
		m_thread.join();

	m_httpAcceptor.reset();
	m_deviceAcceptor.reset();
	m_clients.clear();
	m_clientCount = 0;

	if (was_running)
	{
		m_log << "[info] mock server: stopped\n";
	}
}

void MockServer::broadcastFrame(const retina::Frame& frame)
{
	if (!m_running.load())
		return;

	auto packet = std::make_shared<const std::vector<std::uint8_t>>(mock::encodeRetinaPacket(frame));
	asio::post(m_io, [this, packet]()
	{
		for (const auto& client : m_clients)
		{
			if (client)
				client->enqueue(packet);
		}
	});
}

MockServer::Stats MockServer::getStats() const
{
	Stats stats;
	stats.running = m_running.load();
	stats.httpPort = mock::kHttpPort;
	stats.devicePort = mock::kDevicePort;
	stats.clientCount = m_clientCount.load();

	std::lock_guard<std::mutex> lock(m_statusMutex);
	stats.lastError = m_lastError;
	return stats;
}

void MockServer::startHttpAccept()
{
	if (!m_httpAcceptor)
		return;

	auto socket = std::make_shared<retina::tcp::socket>(m_io);
	m_httpAcceptor->async_accept(*socket, [this, socket](const std::error_code& ec)
	{
		if (!ec)
			handleHttpClient(socket);

		if (m_running.load())
			startHttpAccept();
	});
}

void MockServer::startDeviceAccept()
{
	if (!m_deviceAcceptor)
		return;

	auto session = std::make_shared<ClientSession>(m_io, *this);
	m_deviceAcceptor->async_accept(session->socket, [this, session](const std::error_code& ec)
	{
		if (!ec)
		{
			m_clients.push_back(session);
			m_clientCount = static_cast<int>(m_clients.size());
			m_log << "[info] mock server: device client connected\n";
			session->start();
		}

		if (m_running.load())
			startDeviceAccept();
	});
}

void MockServer::handleHttpClient(const std::shared_ptr<retina::tcp::socket>& socket)
{
	auto response = std::make_shared<std::string>(httpResponse);
	asio::async_write(*socket, asio::buffer(*response), [socket, response](const std::error_code&, std::size_t)
	{
		std::error_code ec;
		socket->shutdown(retina::tcp::socket::shutdown_both, ec);
		socket->close(ec);
	});
}

void MockServer::onClientDisconnected(const std::shared_ptr<ClientSession>& session)
{
	std::error_code ec;
	session->socket.shutdown(retina::tcp::socket::shutdown_both, ec);
	session->socket.close(ec);

	std::erase_if(m_clients, [&](const std::shared_ptr<ClientSession>& current)
	{
		return current == session;
	});

	m_clientCount = static_cast<int>(m_clients.size());
	m_log << "[info] mock server: device client disconnected\n";
}

void MockServer::setLastError(const std::string& error)
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_lastError = error;
	if (!error.empty())
		m_log << "[error] mock server: " << error << "\n";
}
