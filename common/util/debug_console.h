#pragma once

#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <vector>

class DebugConsole
{
public:
	DebugConsole();

	std::ostream& getOutput();
	void clear();
	void snapshot(std::vector<std::string>& out);
	bool takeScrollToBottom();

private:
	class ConsoleOutputBuf : public std::streambuf
	{
	public:
		explicit ConsoleOutputBuf(DebugConsole& owner);

	protected:
		std::streamsize xsputn(const char* text, std::streamsize count) override;
		int overflow(int ch) override;
		int sync() override;

	private:
		DebugConsole& m_owner;
	};

	void append(const char* text, size_t length);
	void flushPendingLineUnlocked();
	void appendLineUnlocked(const std::string& text);

	std::vector<std::string> m_items;
	std::string m_pendingLine;
	bool m_scrollToBottom = false;

	ConsoleOutputBuf m_outputBuf;
	std::ostream m_stream;
	std::mutex m_mutex;
};
