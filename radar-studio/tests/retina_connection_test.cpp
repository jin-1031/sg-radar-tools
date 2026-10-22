#include "../src/device/retina.h"
#include "../../radar-emulator/src/device/mock_protocol.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

// Reuse the existing emulator encoder instead of implementing another wire protocol.
void testReadEnd(bool reset, bool cancel, bool sendFrame)
{
	asio::io_context io;
	retina::tcp::acceptor acceptor(io);
	acceptor.open(retina::tcp::v4());
	acceptor.set_option(asio::socket_base::reuse_address(true));
	acceptor.bind({ asio::ip::address_v4::loopback(), mock::kDevicePort });
	acceptor.listen();
	retina::tcp::socket server(io);
	retina::Frame source{};
	source.frameCount = 42;
	source.points.push_back({});
	source.points.front().x = 1.25f;
	source.points.front().targetId = 255;
	source.targets.push_back({});
	const auto packet = mock::encodeRetinaPacket(source);
	std::ostringstream log;
	bool connected = false;
	bool received = false;
	int failures = 0;
	acceptor.async_accept(server, [&](const std::error_code& ec)
	{
		if (ec) return;
		auto close = [&]()
		{
			std::error_code ignored;
			if (reset) server.set_option(asio::socket_base::linger(true, 0), ignored);
			else server.shutdown(retina::tcp::socket::shutdown_send, ignored);
			server.close(ignored);
		};
		if (sendFrame)
			asio::async_write(server, asio::buffer(packet), [&, close](const std::error_code&, std::size_t) { close(); });
		else close();
	});
	auto client = std::make_unique<retina::DeviceClient>(io, "127.0.0.1", log);
	client->setOnConnected([&]()
	{
		connected = true;
		if (cancel) asio::post(io, [&]() { client->shutdown(); });
	});
	client->setOnFrame([&](const retina::Frame& frame)
	{
		expect(frame.frameCount == 42 && frame.points.size() == 1, "emulator packet must parse");
		expect(frame.points.front().x == 1.25f && frame.points.front().targetId == 255, "point preserved");
		received = true;
	});
	client->setOnDisconnected([&]() { ++failures; });
	io.run_for(std::chrono::seconds(3));
	client->shutdown();
	std::error_code ignored;
	acceptor.cancel(ignored);
	server.close(ignored);
	io.restart();
	io.run();
	expect(connected, "TCP connection must succeed");
		expect(failures == (cancel ? 0 : 1), "only unexpected transport end signals recovery");
	if (sendFrame && !cancel) expect(received, "valid frame delivered before EOF");
	if (!sendFrame) expect(!received, "no false recovery without valid frame");
	if (!cancel) expect(log.str().find("category=") != std::string::npos, "diagnostic fields preserved");
	if (cancel) expect(log.str().find("[error] retina: read failed") == std::string::npos, "explicit cancel is not a network failure");
#ifdef _WIN32
	if (reset && !cancel) expect(log.str().find("code=10054") != std::string::npos, "Windows RST code must be 10054");
#endif
	client.reset();
}

void testCancelBeforeResolve()
{
	asio::io_context io;
	std::ostringstream log;
	retina::DeviceClient client(io, "127.0.0.1", log);
	bool notified = false;
	client.setOnConnected([&]() { notified = true; });
	client.setOnDisconnected([&]() { notified = true; });
	client.shutdown();
	io.run();
	expect(!notified, "intentional early cancellation must not connect or request recovery");
}

void testCancelDiscoveryBeforeStart()
{
	std::ostringstream log;
	retina::DeviceFinder finder(log);
	finder.cancel();
	std::string host;
	expect(finder.find("127.0.0.1", "255.255.255.252", host) == retina::DeviceFinder::Result::Canceled,
		"cancellation must survive discovery worker startup");
	expect(host.empty(), "canceled discovery must not select a device");
}

int main()
{
	try
	{
		testCancelBeforeResolve();
		testCancelDiscoveryBeforeStart();
		for (int i = 0; i < 5; ++i) testReadEnd(false, false, true);
		testReadEnd(false, false, false);
		testReadEnd(true, false, false);
		testReadEnd(false, true, false);
		std::cout << "Retina connection tests PASS (loopback 29172)\n";
		return 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
