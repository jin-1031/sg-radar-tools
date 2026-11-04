#include "../src/device/retina.h"
#include "../../radar-emulator/src/device/mock_protocol.h"

#include <common/util/utf8.h>
#include <chrono>
#include <iostream>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <memory>
#include <sstream>
#include <stdexcept>

namespace
{
	long long eventUnixMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	struct TestContext
	{
		long long startedUnixMs = eventUnixMs();
#ifdef _WIN32
		unsigned long pid = GetCurrentProcessId();
#else
		unsigned long pid = static_cast<unsigned long>(getpid());
#endif
		const char* subtest = "none";
		int iteration = 1;
		const char* stage = "test";
		bool hasListener = false;
	};

	void reportFailure(const TestContext& context, const std::error_code* error, const char* message)
	{
		std::cerr << "FAIL retina_connection eventUnixMs=" << eventUnixMs()
			<< " startedUnixMs=" << context.startedUnixMs << " pid=" << context.pid
			<< " subtest=" << context.subtest << " iteration=" << context.iteration
			<< " stage=" << context.stage
			<< " localAddress=" << (context.hasListener ? "127.0.0.1" : "none")
			<< " localPort=" << (context.hasListener ? mock::kDevicePort : 0)
			<< " backlog=" << (context.hasListener ? asio::socket_base::max_listen_connections : 0);
		if (error)
			std::cerr << " code=" << error->value() << " category=" << error->category().name()
				<< " message=" << os::text::errorMessageUtf8(*error);
		else
			std::cerr << " code=unavailable category=none message=" << message;
		std::cerr << '\n';
	}
}

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

// Reuse the existing emulator encoder instead of implementing another wire protocol.
void testReadEnd(bool reset, bool cancel, bool sendFrame, TestContext& context)
{
	context.hasListener = true;
	context.stage = "acceptor.create";
	asio::io_context io;
	retina::tcp::acceptor acceptor(io);
	context.stage = "open";
	acceptor.open(retina::tcp::v4());
	context.stage = "set_option(reuse_address)";
	acceptor.set_option(asio::socket_base::reuse_address(true));
	context.stage = "bind";
	acceptor.bind({ asio::ip::address_v4::loopback(), mock::kDevicePort });
	context.stage = "listen";
	acceptor.listen();
	context.stage = "fixture.prepare";
	retina::tcp::socket server(io);
	retina::Frame source{};
	source.frameCount = 42;
	source.points.push_back({});
	source.points.front().x = 1.25f;
	source.points.front().targetId = 255;
	source.targets.push_back({});
	source.targets.front().x = 1.5f;
	source.targets.front().y = -2.25f;
	source.targets.front().targetId = 255;
	source.targets.front().status = pcr::TargetStatus::Walking;
	const auto packet = mock::encodeRetinaPacket(source);
	std::ostringstream log;
	bool connected = false;
	bool received = false;
	int failures = 0;
	context.stage = "async_accept.initiate";
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
	context.stage = "client.create";
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
		expect(frame.targets.size() == 1, "emulator Target Section must be parsed");
		const auto& target = frame.targets.front();
		expect(target.x == 1.5f && target.y == -2.25f && target.targetId == 255 &&
			target.status == pcr::TargetStatus::Walking, "target values preserved over TCP");
		expect(target.minx == 1.25f && target.maxx == 1.25f, "target point bounds preserved over TCP");
		received = true;
	});
	client->setOnDisconnected([&]() { ++failures; });
	context.stage = "io.run_for";
	io.run_for(std::chrono::seconds(3));
	context.stage = "cleanup";
	client->shutdown();
	std::error_code ignored;
	acceptor.cancel(ignored);
	server.close(ignored);
	io.restart();
	io.run();
	context.stage = "assertions";
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
	TestContext context;
	std::cout << "[test] retina_connection startedUnixMs=" << context.startedUnixMs
		<< " pid=" << context.pid << '\n';
	try
	{
		context.subtest = "cancel-before-resolve";
		testCancelBeforeResolve();
		context.subtest = "cancel-before-discovery";
		testCancelDiscoveryBeforeStart();
		context.subtest = "fin-with-frame";
		for (int i = 0; i < 5; ++i)
		{
			context.iteration = i + 1;
			testReadEnd(false, false, true, context);
		}
		context.iteration = 1;
		context.subtest = "fin-without-frame";
		testReadEnd(false, false, false, context);
		context.subtest = "rst";
		testReadEnd(true, false, false, context);
		context.subtest = "explicit-cancel";
		testReadEnd(false, true, false, context);
		std::cout << "Retina connection tests PASS (loopback 29172)\n";
		return 0;
	}
	catch (const std::system_error& error)
	{
		// Asio saved the native error before stack unwinding and diagnostic formatting.
		reportFailure(context, &error.code(), error.what());
		return 1;
	}
	catch (const std::exception& error)
	{
		reportFailure(context, nullptr, error.what());
		return 1;
	}
}
