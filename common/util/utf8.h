#pragma once

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace os::text
{
#ifdef _WIN32
	inline std::wstring decodePathText(std::string_view input, unsigned int codePage)
	{
		if (input.find('\0') != std::string_view::npos ||
			input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
			throw std::invalid_argument("Invalid path text length");
		if (input.empty()) return {};
		const int length = static_cast<int>(input.size());
		const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, input.data(), length, nullptr, 0);
		if (size <= 0) throw std::invalid_argument("Invalid path text encoding");
		std::wstring wide(size, L'\0');
		if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, input.data(), length, wide.data(), size) != size)
			throw std::invalid_argument("Invalid path text encoding");
		return wide;
	}
#endif

	// Strict conversion for ImGui UTF-8 input; never route file I/O through ACP.
	inline std::filesystem::path pathFromUtf8(std::string_view input)
	{
#ifdef _WIN32
		return std::filesystem::path(decodePathText(input, CP_UTF8));
#else
		if (input.find('\0') != std::string_view::npos) throw std::invalid_argument("Invalid path text");
		return std::filesystem::u8path(input.begin(), input.end());
#endif
	}

	// The existing Windows dialogs explicitly return active-code-page bytes.
	inline std::filesystem::path dialogPathToNative(std::string_view input)
	{
#ifdef _WIN32
		return std::filesystem::path(decodePathText(input, CP_ACP));
#else
		return pathFromUtf8(input);
#endif
	}

#ifdef _WIN32
	inline std::string utf16ToUtf8(std::wstring_view input) noexcept
	{
		try
		{
			if (input.empty()) return {};
			if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return "[invalid text]";
			const int length = static_cast<int>(input.size());
			const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), length, nullptr, 0, nullptr, nullptr);
			if (size <= 0) return "[invalid text]";
			std::string output(size, '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), length, output.data(), size, nullptr, nullptr) != size)
				return "[invalid text]";
			return output;
		}
		catch (...) { return {}; }
	}
#endif

	// Only for inputs whose producer explicitly uses the Windows active code page.
	inline std::string acpToUtf8(std::string_view input) noexcept
	{
		try
		{
#ifdef _WIN32
			if (input.empty()) return {};
			if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return "[invalid text]";
			const int length = static_cast<int>(input.size());
			const int size = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, input.data(), length, nullptr, 0);
			if (size <= 0) return "[invalid text]";
			std::wstring wide(size, L'\0');
			if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, input.data(), length, wide.data(), size) != size)
				return "[invalid text]";
			return utf16ToUtf8(wide);
#else
			return std::string(input);
#endif
		}
		catch (...) { return {}; }
	}

	inline std::string pathToUtf8(const std::filesystem::path& path) noexcept
	{
		try
		{
			const auto bytes = path.u8string();
			return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		}
		catch (...) { return "[invalid path]"; }
	}

	// Preserve the native path used by file I/O; convert a copy for display only.
	inline std::string nativePathToUtf8(std::string_view path) noexcept
	{
		try { return pathToUtf8(std::filesystem::path(std::string(path))); }
		catch (...) { return "[invalid path]"; }
	}

	inline std::string errorMessageUtf8(const std::error_code& error) noexcept
	{
		try
		{
#ifdef _WIN32
			if (error.category() == std::system_category() || std::string_view(error.category().name()) == "asio.system")
			{
				// Obtain Unicode at the source, rather than losing characters through FormatMessageA.
				wchar_t* buffer = nullptr;
				const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
					FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(error.value()),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
				if (size != 0)
				{
					std::wstring_view message(buffer, size);
					while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.remove_suffix(1);
					auto output = utf16ToUtf8(message);
					LocalFree(buffer);
					return output;
				}
				return acpToUtf8(error.message());
			}
#endif
			// Custom categories (including existing UTF-8 messages) are not assumed to be ACP.
			return error.message();
		}
		catch (...) { return {}; }
	}
}
