#pragma once

#include <common/util/utf8.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace recording_path
{
	inline void validateBufferText(std::string_view text, std::size_t capacity)
	{
		if (text.size() >= capacity) throw std::length_error("Path exceeds the UI buffer capacity (255 UTF-8 bytes)");
		os::text::pathFromUtf8(text);
	}

	template<std::size_t N>
	void copyUtf8(char (&buffer)[N], std::string_view text)
	{
		validateBufferText(text, N);
		if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
		buffer[text.size()] = '\0';
	}

	inline std::string timestampName()
	{
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm local{};
#ifdef _WIN32
		localtime_s(&local, &now);
#else
		local = *std::localtime(&now);
#endif
		std::ostringstream output;
		output << std::put_time(&local, "%Y%m%d_%H%M%S");
		return output.str();
	}

	inline void validateComponent(std::string_view text)
	{
		if (text.empty() || text.back() == '.' || text.back() == ' ' ||
			text.find_first_of("<>:\"/\\|?*") != std::string_view::npos)
			throw std::invalid_argument("Invalid file or folder name");
		for (unsigned char c : text)
			if (c < 32) throw std::invalid_argument("Control characters are not valid in paths");
		auto stem = std::string(text.substr(0, text.find('.')));
		for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
			(stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") &&
				stem[3] >= '1' && stem[3] <= '9'))
			throw std::invalid_argument("Reserved Windows device name");
	}

	inline void validatePath(const std::filesystem::path& path)
	{
		for (const auto& component : path.relative_path())
		{
			const auto text = os::text::pathToUtf8(component);
			if (text.empty() || text == "." || text == "..") continue;
			validateComponent(text);
		}
#ifdef _WIN32
		// This UI uses legacy Windows dialogs, not an extended-length-path workflow.
		if (std::filesystem::absolute(path).native().size() >= MAX_PATH)
			throw std::length_error("Path exceeds the supported Windows path length (259 UTF-16 units)");
#endif
	}

	inline std::filesystem::path outputPath(std::string_view directory, std::string_view name)
	{
		const auto folder = os::text::pathFromUtf8(directory.empty() ? "." : directory);
		const auto filename = name.empty() ? timestampName() : std::string(name);
		validateComponent(filename);
		const auto base = folder / os::text::pathFromUtf8(filename);
		auto candidate = base;
		candidate += ".pcr";
		std::size_t suffix = 0;
		for (;;)
		{
			validatePath(candidate);
			std::error_code error;
			const bool present = std::filesystem::exists(candidate, error);
			if (error) throw std::filesystem::filesystem_error("Cannot inspect output path", candidate, error);
			if (!present) return candidate;
			if (suffix == (std::numeric_limits<std::size_t>::max)()) throw std::overflow_error("Filename suffix exhausted");
			candidate = base;
			candidate += "(" + std::to_string(++suffix) + ").pcr";
		}
	}

	inline std::string errorMessage(const std::exception& error)
	{
		if (const auto* fsError = dynamic_cast<const std::filesystem::filesystem_error*>(&error))
			return os::text::errorMessageUtf8(fsError->code()) + " path=" + os::text::pathToUtf8(fsError->path1());
		return error.what();
	}

	// Preview failures are UI feedback, not file-operation errors or Console events.
	inline std::filesystem::path previewOutputPath(std::string_view directory, std::string_view name, std::string& error)
	{
		error.clear();
		try { return outputPath(directory, name); }
		catch (const std::exception& failure) { error = errorMessage(failure); }
		return {};
	}
}
