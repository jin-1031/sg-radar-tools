#include "debug_console.h"

DebugConsole::ConsoleOutputBuf::ConsoleOutputBuf(DebugConsole& owner) :
	m_owner(owner)
{
}

std::streamsize DebugConsole::ConsoleOutputBuf::xsputn(const char* text, std::streamsize count)
{
	m_owner.append(text, static_cast<size_t>(count));
	return count;
}

int DebugConsole::ConsoleOutputBuf::overflow(int ch)
{
	if (ch != traits_type::eof())
	{
		const char value = static_cast<char>(ch);
		m_owner.append(&value, 1);
	}
	return ch;
}

int DebugConsole::ConsoleOutputBuf::sync()
{
	std::lock_guard<std::mutex> lock(m_owner.m_mutex);
	m_owner.flushPendingLineUnlocked();
	return 0;
}

DebugConsole::DebugConsole() :
	m_outputBuf(*this),
	m_stream(&m_outputBuf)
{
}

std::ostream& DebugConsole::getOutput()
{
	return m_stream;
}

void DebugConsole::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_items.clear();
	m_pendingLine.clear();
}

void DebugConsole::snapshot(std::vector<std::string>& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	flushPendingLineUnlocked();
	out = m_items;
}

bool DebugConsole::takeScrollToBottom()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const bool scroll = m_scrollToBottom;
	m_scrollToBottom = false;
	return scroll;
}

void DebugConsole::append(const char* text, size_t length)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (size_t i = 0; i < length; ++i)
	{
		const char ch = text[i];
		if (ch == '\r')
			continue;
		if (ch == '\n')
		{
			flushPendingLineUnlocked();
			continue;
		}
		m_pendingLine.push_back(ch);
	}
}

void DebugConsole::flushPendingLineUnlocked()
{
	if (m_pendingLine.empty())
		return;
	appendLineUnlocked(m_pendingLine);
	m_pendingLine.clear();
}

void DebugConsole::appendLineUnlocked(const std::string& text)
{
	m_items.push_back(text);
	m_scrollToBottom = true;
}
