#pragma once

#include <cstddef>
#include <cstdint>

namespace os
{
	enum class FileDialogResult
	{
		OK = 0,
		Canceled = 1,
		ErrorUnknown = 2,
		ErrorNullPointer = 4,
		ErrorOutPathTooShort = 3,
	};

	enum class DialogButtonFlags : std::uint32_t
	{
		None = 0,
		Ok = 1u << 0,
		Cancel = 1u << 1,
		Retry = 1u << 2,
		Abort = 1u << 3,
		Ignore = 1u << 4,
		Yes = 1u << 5,
		No = 1u << 6,
		Help = 1u << 7,
		Continue = 1u << 8,

		OkCancel = Ok | Cancel,
		RetryCancel = Retry | Cancel,
		AbortRetryIgnore = Abort | Retry | Ignore,
		YesNo = Yes | No,
		YesNoCancel = Yes | No | Cancel,
		OkRetryCancel = Ok | Retry | Cancel,
	};

	constexpr DialogButtonFlags operator|(DialogButtonFlags lhs, DialogButtonFlags rhs)
	{
		return static_cast<DialogButtonFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
	}

	constexpr DialogButtonFlags operator&(DialogButtonFlags lhs, DialogButtonFlags rhs)
	{
		return static_cast<DialogButtonFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
	}

	constexpr DialogButtonFlags& operator|=(DialogButtonFlags& lhs, DialogButtonFlags rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	constexpr bool hasDialogButtonFlag(DialogButtonFlags value, DialogButtonFlags flag)
	{
		return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
	}

	FileDialogResult openFolderDialog(const char* title, char* out_path, size_t out_path_size);
	FileDialogResult openFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size);
	FileDialogResult saveFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size);

	bool openDialog(const char* title, const char* msg, DialogButtonFlags flags, DialogButtonFlags* out_flag);

	void openFolderInExplorer(const char* path);
}
