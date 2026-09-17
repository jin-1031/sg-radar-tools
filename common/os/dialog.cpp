#include "dialog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
	using os::DialogButtonFlags;
	using os::FileDialogResult;

	constexpr std::uint32_t toBits(DialogButtonFlags flags)
	{
		return static_cast<std::uint32_t>(flags);
	}

	constexpr bool hasAll(DialogButtonFlags value, DialogButtonFlags mask)
	{
		return (toBits(value) & toBits(mask)) == toBits(mask);
	}

	std::string shellQuote(const char* text)
	{
		std::string result = "'";
		if (text != nullptr)
		{
			for (const char* cursor = text; *cursor != '\0'; ++cursor)
			{
				if (*cursor == '\'')
				{
					result += "'\\''";
				}
				else
				{
					result.push_back(*cursor);
				}
			}
		}
		result.push_back('\'');
		return result;
	}

	std::filesystem::path resolveFolderPath(const char* path)
	{
		namespace fs = std::filesystem;

		std::error_code ec;
		fs::path folder = (path != nullptr && path[0] != '\0') ? fs::path(path) : fs::current_path(ec);
		if (folder.empty())
			folder = fs::current_path(ec);

		const fs::path absolute = fs::absolute(folder, ec);
		if (!ec)
			folder = absolute;

		if (fs::is_regular_file(folder, ec))
			folder = folder.parent_path();

		if (!fs::exists(folder, ec))
			fs::create_directories(folder, ec);

		return folder;
	}

	std::string escapeAppleScriptString(const char* text)
	{
		std::string result;
		if (text == nullptr)
		{
			return result;
		}

		for (const char* cursor = text; *cursor != '\0'; ++cursor)
		{
			if (*cursor == '\\' || *cursor == '"')
			{
				result.push_back('\\');
			}
			result.push_back(*cursor);
		}

		return result;
	}

	std::string appleScriptQuoted(const char* text)
	{
		return std::string("\"") + escapeAppleScriptString(text) + "\"";
	}

	bool tryReadDialogLine(FILE* pipe, char* out_path, size_t out_path_size)
	{
		if (fgets(out_path, static_cast<int>(out_path_size), pipe) == nullptr)
		{
			return false;
		}

		size_t len = std::strlen(out_path);
		if (len > 0 && out_path[len - 1] == '\n')
		{
			out_path[len - 1] = '\0';
		}

		return len != 0 && !(len == 1 && out_path[0] == '\n');
	}

	struct DialogButtonSpec
	{
		DialogButtonFlags flag;
		const char* label;
		int exitCode;
	};

	const char* buttonLabel(DialogButtonFlags flag)
	{
		switch (flag)
		{
		case DialogButtonFlags::Ok:
			return "Ok";
		case DialogButtonFlags::Cancel:
			return "Cancel";
		case DialogButtonFlags::Retry:
			return "Retry";
		case DialogButtonFlags::Abort:
			return "Abort";
		case DialogButtonFlags::Ignore:
			return "Ignore";
		case DialogButtonFlags::Yes:
			return "Yes";
		case DialogButtonFlags::No:
			return "No";
		case DialogButtonFlags::Help:
			return "Help";
		case DialogButtonFlags::Continue:
			return "Continue";
		default:
			return "Ok";
		}
	}

	int buttonExitCode(DialogButtonFlags flag)
	{
		switch (flag)
		{
		case DialogButtonFlags::Ok:
			return 1;
		case DialogButtonFlags::Cancel:
			return 2;
		case DialogButtonFlags::Retry:
			return 3;
		case DialogButtonFlags::Abort:
			return 4;
		case DialogButtonFlags::Ignore:
			return 5;
		case DialogButtonFlags::Yes:
			return 6;
		case DialogButtonFlags::No:
			return 7;
		case DialogButtonFlags::Help:
			return 8;
		case DialogButtonFlags::Continue:
			return 9;
		default:
			return 0;
		}
	}

	DialogButtonFlags flagFromExitCode(int exitCode)
	{
		switch (exitCode)
		{
		case 1:
			return DialogButtonFlags::Ok;
		case 2:
			return DialogButtonFlags::Cancel;
		case 3:
			return DialogButtonFlags::Retry;
		case 4:
			return DialogButtonFlags::Abort;
		case 5:
			return DialogButtonFlags::Ignore;
		case 6:
			return DialogButtonFlags::Yes;
		case 7:
			return DialogButtonFlags::No;
		case 8:
			return DialogButtonFlags::Help;
		case 9:
			return DialogButtonFlags::Continue;
		default:
			return DialogButtonFlags::None;
		}
	}

	std::string buildButtonList(const DialogButtonSpec* buttons, size_t buttonCount)
	{
		std::string result;
		for (size_t index = 0; index < buttonCount; ++index)
		{
			if (index != 0)
			{
				result += ", ";
			}
			result += appleScriptQuoted(buttons[index].label);
		}
		return result;
	}

	size_t buildDialogButtons(DialogButtonFlags flags, DialogButtonSpec* buttons, DialogButtonFlags& defaultButton, DialogButtonFlags& cancelButton)
	{
		defaultButton = DialogButtonFlags::Ok;
		cancelButton = DialogButtonFlags::None;

		if (hasAll(flags, DialogButtonFlags::AbortRetryIgnore))
		{
			buttons[0] = { DialogButtonFlags::Abort, buttonLabel(DialogButtonFlags::Abort), buttonExitCode(DialogButtonFlags::Abort) };
			buttons[1] = { DialogButtonFlags::Retry, buttonLabel(DialogButtonFlags::Retry), buttonExitCode(DialogButtonFlags::Retry) };
			buttons[2] = { DialogButtonFlags::Ignore, buttonLabel(DialogButtonFlags::Ignore), buttonExitCode(DialogButtonFlags::Ignore) };
			defaultButton = DialogButtonFlags::Retry;
			return 3;
		}

		if (hasAll(flags, DialogButtonFlags::YesNoCancel))
		{
			buttons[0] = { DialogButtonFlags::Yes, buttonLabel(DialogButtonFlags::Yes), buttonExitCode(DialogButtonFlags::Yes) };
			buttons[1] = { DialogButtonFlags::No, buttonLabel(DialogButtonFlags::No), buttonExitCode(DialogButtonFlags::No) };
			buttons[2] = { DialogButtonFlags::Cancel, buttonLabel(DialogButtonFlags::Cancel), buttonExitCode(DialogButtonFlags::Cancel) };
			defaultButton = DialogButtonFlags::Yes;
			cancelButton = DialogButtonFlags::Cancel;
			return 3;
		}

		if (hasAll(flags, DialogButtonFlags::OkRetryCancel))
		{
			buttons[0] = { DialogButtonFlags::Ok, buttonLabel(DialogButtonFlags::Ok), buttonExitCode(DialogButtonFlags::Ok) };
			buttons[1] = { DialogButtonFlags::Retry, buttonLabel(DialogButtonFlags::Retry), buttonExitCode(DialogButtonFlags::Retry) };
			buttons[2] = { DialogButtonFlags::Cancel, buttonLabel(DialogButtonFlags::Cancel), buttonExitCode(DialogButtonFlags::Cancel) };
			defaultButton = DialogButtonFlags::Ok;
			cancelButton = DialogButtonFlags::Cancel;
			return 3;
		}

		if (hasAll(flags, DialogButtonFlags::YesNo))
		{
			buttons[0] = { DialogButtonFlags::Yes, buttonLabel(DialogButtonFlags::Yes), buttonExitCode(DialogButtonFlags::Yes) };
			buttons[1] = { DialogButtonFlags::No, buttonLabel(DialogButtonFlags::No), buttonExitCode(DialogButtonFlags::No) };
			defaultButton = DialogButtonFlags::Yes;
			return 2;
		}

		if (hasAll(flags, DialogButtonFlags::RetryCancel))
		{
			buttons[0] = { DialogButtonFlags::Retry, buttonLabel(DialogButtonFlags::Retry), buttonExitCode(DialogButtonFlags::Retry) };
			buttons[1] = { DialogButtonFlags::Cancel, buttonLabel(DialogButtonFlags::Cancel), buttonExitCode(DialogButtonFlags::Cancel) };
			defaultButton = DialogButtonFlags::Retry;
			cancelButton = DialogButtonFlags::Cancel;
			return 2;
		}

		if (hasAll(flags, DialogButtonFlags::OkCancel))
		{
			buttons[0] = { DialogButtonFlags::Ok, buttonLabel(DialogButtonFlags::Ok), buttonExitCode(DialogButtonFlags::Ok) };
			buttons[1] = { DialogButtonFlags::Cancel, buttonLabel(DialogButtonFlags::Cancel), buttonExitCode(DialogButtonFlags::Cancel) };
			defaultButton = DialogButtonFlags::Ok;
			cancelButton = DialogButtonFlags::Cancel;
			return 2;
		}

		if (hasAll(flags, DialogButtonFlags::Ok))
		{
			buttons[0] = { DialogButtonFlags::Ok, buttonLabel(DialogButtonFlags::Ok), buttonExitCode(DialogButtonFlags::Ok) };
			defaultButton = DialogButtonFlags::Ok;
			return 1;
		}

		if (hasAll(flags, DialogButtonFlags::Cancel))
		{
			buttons[0] = { DialogButtonFlags::Cancel, buttonLabel(DialogButtonFlags::Cancel), buttonExitCode(DialogButtonFlags::Cancel) };
			cancelButton = DialogButtonFlags::Cancel;
			return 1;
		}

		if (hasAll(flags, DialogButtonFlags::Retry))
		{
			buttons[0] = { DialogButtonFlags::Retry, buttonLabel(DialogButtonFlags::Retry), buttonExitCode(DialogButtonFlags::Retry) };
			defaultButton = DialogButtonFlags::Retry;
			return 1;
		}

		if (hasAll(flags, DialogButtonFlags::Abort))
		{
			buttons[0] = { DialogButtonFlags::Abort, buttonLabel(DialogButtonFlags::Abort), buttonExitCode(DialogButtonFlags::Abort) };
			defaultButton = DialogButtonFlags::Abort;
			return 1;
		}

		buttons[0] = { DialogButtonFlags::Ok, buttonLabel(DialogButtonFlags::Ok), buttonExitCode(DialogButtonFlags::Ok) };
		defaultButton = DialogButtonFlags::Ok;
		return 1;
	}

	std::string buildAppleScriptText(const char* msg, const char* title, DialogButtonFlags flags)
	{
		DialogButtonSpec buttons[3] = {};
		DialogButtonFlags defaultButton = DialogButtonFlags::Ok;
		DialogButtonFlags cancelButton = DialogButtonFlags::None;
		const size_t buttonCount = buildDialogButtons(flags, buttons, defaultButton, cancelButton);

		std::string script = "try\n";
		script += "set response to display dialog ";
		script += appleScriptQuoted(msg != nullptr ? msg : "");
		if (title != nullptr && title[0] != '\0')
		{
			script += " with title ";
			script += appleScriptQuoted(title);
		}
		script += " buttons {";
		script += buildButtonList(buttons, buttonCount);
		script += "} default button ";
		script += appleScriptQuoted(buttonLabel(defaultButton));
		if (cancelButton != DialogButtonFlags::None)
		{
			script += " cancel button ";
			script += appleScriptQuoted(buttonLabel(cancelButton));
		}
		script += "\nreturn button returned of response\non error number -128\nreturn \"\"\non error\nreturn \"\"\nend try";
		return script;
	}

	DialogButtonFlags dialogFlagFromLabel(const std::string& label)
	{
		if (label == "Ok")
		{
			return DialogButtonFlags::Ok;
		}
		if (label == "Cancel")
		{
			return DialogButtonFlags::Cancel;
		}
		if (label == "Retry")
		{
			return DialogButtonFlags::Retry;
		}
		if (label == "Abort")
		{
			return DialogButtonFlags::Abort;
		}
		if (label == "Ignore")
		{
			return DialogButtonFlags::Ignore;
		}
		if (label == "Yes")
		{
			return DialogButtonFlags::Yes;
		}
		if (label == "No")
		{
			return DialogButtonFlags::No;
		}
		if (label == "Help")
		{
			return DialogButtonFlags::Help;
		}
		if (label == "Continue")
		{
			return DialogButtonFlags::Continue;
		}
		return DialogButtonFlags::None;
	}
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NO_MIN_MAX
#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

namespace os
{
	namespace
	{
		std::wstring toWide(const char* text)
		{
			if (text == nullptr || text[0] == '\0')
			{
				return std::wstring();
			}

			const int wide_length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
			if (wide_length <= 0)
			{
				return std::wstring();
			}

			std::wstring wide(static_cast<size_t>(wide_length), L'\0');
			MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], wide_length);
			if (!wide.empty() && wide.back() == L'\0')
			{
				wide.pop_back();
			}
			return wide;
		}

		bool wideToAnsi(const wchar_t* wide_text, char* out_path, size_t out_path_size)
		{
			const int result_length = WideCharToMultiByte(CP_ACP, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
			if (result_length <= 0 || static_cast<size_t>(result_length) > out_path_size)
			{
				return false;
			}

			WideCharToMultiByte(CP_ACP, 0, wide_text, -1, out_path, static_cast<int>(out_path_size), nullptr, nullptr);
			return true;
		}

		FileDialogResult openFolderDialogImpl(const char* title, char* out_path, size_t out_path_size)
		{
			HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			const bool did_initialize = SUCCEEDED(init_result);
			if (FAILED(init_result) && init_result != RPC_E_CHANGED_MODE)
			{
				return FileDialogResult::ErrorUnknown;
			}

			IFileOpenDialog* dialog = nullptr;
			HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog));
			if (FAILED(hr))
			{
				if (did_initialize)
				{
					CoUninitialize();
				}
				return FileDialogResult::ErrorUnknown;
			}

			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

			std::wstring titleW = toWide(title);
			if (!titleW.empty())
			{
				dialog->SetTitle(titleW.c_str());
			}

			hr = dialog->Show(nullptr);
			if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			{
				dialog->Release();
				if (did_initialize)
				{
					CoUninitialize();
				}
				return FileDialogResult::Canceled;
			}

			if (FAILED(hr))
			{
				dialog->Release();
				if (did_initialize)
				{
					CoUninitialize();
				}
				return FileDialogResult::ErrorUnknown;
			}

			IShellItem* item = nullptr;
			hr = dialog->GetResult(&item);
			if (FAILED(hr))
			{
				dialog->Release();
				if (did_initialize)
				{
					CoUninitialize();
				}
				return FileDialogResult::ErrorUnknown;
			}

			PWSTR folder_path = nullptr;
			hr = item->GetDisplayName(SIGDN_FILESYSPATH, &folder_path);
			item->Release();
			dialog->Release();
			if (did_initialize)
			{
				CoUninitialize();
			}

			if (FAILED(hr))
			{
				return FileDialogResult::ErrorUnknown;
			}

			const bool copied = wideToAnsi(folder_path, out_path, out_path_size);
			CoTaskMemFree(folder_path);
			return copied ? FileDialogResult::OK : FileDialogResult::ErrorOutPathTooShort;
		}

		FileDialogResult openFileDialogImpl(const char* title, const char* filter, char* out_path, size_t out_path_size)
		{
			wchar_t file_name[MAX_PATH] = L"";
			wchar_t filterW[256] = L"";
			wchar_t titleW[256] = L"";

			if (filter != nullptr)
			{
				MultiByteToWideChar(CP_ACP, 0, filter, -1, filterW, 256);
			}
			if (title != nullptr)
			{
				MultiByteToWideChar(CP_ACP, 0, title, -1, titleW, 256);
			}

			OPENFILENAMEW ofn = {};
			ofn.lStructSize = sizeof(OPENFILENAMEW);
			ofn.hwndOwner = nullptr;
			ofn.lpstrFile = file_name;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrFilter = filterW[0] != L'\0' ? filterW : L"All Files\0*.*\0";
			ofn.lpstrTitle = titleW[0] != L'\0' ? titleW : L"Open File";
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

			if (GetOpenFileNameW(&ofn))
			{
				if (!wideToAnsi(file_name, out_path, out_path_size))
				{
					return FileDialogResult::ErrorOutPathTooShort;
				}
				return FileDialogResult::OK;
			}

			DWORD error = CommDlgExtendedError();
			if (error == 0)
			{
				return FileDialogResult::Canceled;
			}

			return FileDialogResult::ErrorUnknown;
		}

		FileDialogResult saveFileDialogImpl(const char* title, const char* filter, char* out_path, size_t out_path_size)
		{
			wchar_t file_name[MAX_PATH] = L"";
			wchar_t filterW[256] = L"";
			wchar_t titleW[256] = L"";

			if (filter != nullptr)
			{
				MultiByteToWideChar(CP_ACP, 0, filter, -1, filterW, 256);
			}
			if (title != nullptr)
			{
				MultiByteToWideChar(CP_ACP, 0, title, -1, titleW, 256);
			}

			OPENFILENAMEW ofn = {};
			ofn.lStructSize = sizeof(OPENFILENAMEW);
			ofn.hwndOwner = nullptr;
			ofn.lpstrFile = file_name;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrFilter = filterW[0] != L'\0' ? filterW : L"All Files\0*.*\0";
			ofn.lpstrTitle = titleW[0] != L'\0' ? titleW : L"Save File";
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

			if (GetSaveFileNameW(&ofn))
			{
				if (!wideToAnsi(file_name, out_path, out_path_size))
				{
					return FileDialogResult::ErrorOutPathTooShort;
				}
				return FileDialogResult::OK;
			}

			DWORD error = CommDlgExtendedError();
			if (error == 0)
			{
				return FileDialogResult::Canceled;
			}

			return FileDialogResult::ErrorUnknown;
		}

		DialogButtonFlags mapMessageBoxResult(int result)
		{
			switch (result)
			{
			case IDOK:
				return DialogButtonFlags::Ok;
			case IDCANCEL:
				return DialogButtonFlags::Cancel;
			case IDABORT:
				return DialogButtonFlags::Abort;
			case IDRETRY:
				return DialogButtonFlags::Retry;
			case IDIGNORE:
				return DialogButtonFlags::Ignore;
			case IDYES:
				return DialogButtonFlags::Yes;
			case IDNO:
				return DialogButtonFlags::No;
			default:
				return DialogButtonFlags::None;
			}
		}

		UINT messageBoxStyleForFlags(DialogButtonFlags flags)
		{
			if (hasAll(flags, DialogButtonFlags::AbortRetryIgnore))
			{
				return MB_ABORTRETRYIGNORE;
			}
			if (hasAll(flags, DialogButtonFlags::YesNoCancel))
			{
				return MB_YESNOCANCEL;
			}
			if (hasAll(flags, DialogButtonFlags::YesNo))
			{
				return MB_YESNO;
			}
			if (hasAll(flags, DialogButtonFlags::RetryCancel) || hasAll(flags, DialogButtonFlags::OkRetryCancel))
			{
				return MB_RETRYCANCEL;
			}
			if (hasAll(flags, DialogButtonFlags::OkCancel) || hasAll(flags, DialogButtonFlags::Cancel))
			{
				return MB_OKCANCEL;
			}
			return MB_OK;
		}
	}

	FileDialogResult openFolderDialog(const char* title, char* out_path, size_t out_path_size)
	{
		return openFolderDialogImpl(title, out_path, out_path_size);
	}

	FileDialogResult openFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		if (out_path == nullptr || out_path_size == 0)
		{
			return FileDialogResult::ErrorNullPointer;
		}

		return openFileDialogImpl(title, filter, out_path, out_path_size);
	}

	FileDialogResult saveFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		if (out_path == nullptr || out_path_size == 0)
		{
			return FileDialogResult::ErrorNullPointer;
		}

		return saveFileDialogImpl(title, filter, out_path, out_path_size);
	}

	bool openDialog(const char* title, const char* msg, DialogButtonFlags flags, DialogButtonFlags* out_flag)
	{
		if (out_flag == nullptr)
		{
			return false;
		}

		const std::wstring titleW = toWide(title);
		const std::wstring msgW = toWide(msg != nullptr ? msg : "");
		const UINT style = messageBoxStyleForFlags(flags) | MB_ICONINFORMATION | MB_SETFOREGROUND;
		const int result = MessageBoxW(nullptr, msgW.empty() ? L"" : msgW.c_str(), titleW.empty() ? L"Message" : titleW.c_str(), style);
		*out_flag = mapMessageBoxResult(result);
		return *out_flag != DialogButtonFlags::None;
	}

	void openFolderInExplorer(const char* path)
	{
		const std::filesystem::path folder = resolveFolderPath(path);
		const std::wstring pathW = folder.wstring();
		if (pathW.empty())
		{
			return;
		}

		ShellExecuteW(nullptr, L"open", pathW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
}

#elif defined(__APPLE__)

#include <sys/wait.h>

namespace os
{
	namespace
	{
		bool readDialogLine(FILE* pipe, std::string& result)
		{
			char buffer[1024] = {};
			if (fgets(buffer, sizeof(buffer), pipe) == nullptr)
			{
				return false;
			}

			result = buffer;
			while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
			{
				result.pop_back();
			}
			return !result.empty();
		}

		std::string normalizeAppleScriptButtonResult(std::string result)
		{
			const size_t colonPos = result.find_last_of(':');
			if (colonPos != std::string::npos)
			{
				result.erase(0, colonPos + 1);
			}

			while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
			{
				result.erase(result.begin());
			}

			while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ' || result.back() == '\t'))
			{
				result.pop_back();
			}

			return result;
		}

		std::string runAppleScript(const std::string& script)
		{
			std::string command = "osascript -e ";
			command += shellQuote(script.c_str());
			return command;
		}

		FileDialogResult runAppleFileDialog(const char* dialogName, const char* title, char* out_path, size_t out_path_size)
		{
			std::string script = "try\nset filePath to POSIX path of (";
			script += dialogName;
			if (title != nullptr && title[0] != '\0')
			{
				script += " with prompt ";
				script += appleScriptQuoted(title);
			}
			script += ")\nreturn filePath\non error number -128\nreturn \"\"\non error\nreturn \"\"\nend try";

			std::string command = runAppleScript(script);
			FILE* pipe = popen(command.c_str(), "r");
			if (pipe == nullptr)
			{
				return FileDialogResult::ErrorUnknown;
			}

			if (!tryReadDialogLine(pipe, out_path, out_path_size))
			{
				pclose(pipe);
				return FileDialogResult::Canceled;
			}

			pclose(pipe);
			return FileDialogResult::OK;
		}

		bool runAppleMessageDialog(const char* title, const char* msg, DialogButtonFlags flags, DialogButtonFlags* out_flag)
		{
			DialogButtonSpec buttons[3] = {};
			DialogButtonFlags defaultButton = DialogButtonFlags::Ok;
			DialogButtonFlags cancelButton = DialogButtonFlags::None;
			const size_t buttonCount = buildDialogButtons(flags, buttons, defaultButton, cancelButton);

			std::string script = "try\nset response to display dialog ";
			script += appleScriptQuoted(msg != nullptr ? msg : "");
			if (title != nullptr && title[0] != '\0')
			{
				script += " with title ";
				script += appleScriptQuoted(title);
			}
			script += " buttons {";
			script += buildButtonList(buttons, buttonCount);
			script += "} default button ";
			script += appleScriptQuoted(buttonLabel(defaultButton));
			if (cancelButton != DialogButtonFlags::None)
			{
				script += " cancel button ";
				script += appleScriptQuoted(buttonLabel(cancelButton));
			}
			script += "\nreturn button returned of response\non error number -128\nreturn \"\"\non error\nreturn \"\"\nend try";

			std::string command = runAppleScript(script);
			FILE* pipe = popen(command.c_str(), "r");
			if (pipe == nullptr)
			{
				return false;
			}

			std::string result;
			const bool received = readDialogLine(pipe, result);
			pclose(pipe);
			if (!received)
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			*out_flag = dialogFlagFromLabel(normalizeAppleScriptButtonResult(result));
			return *out_flag != DialogButtonFlags::None;
		}

		void openFolderInExplorerImpl(const char* path)
		{
			const std::string folder = resolveFolderPath(path).string();
			std::string command = "open ";
			command += shellQuote(folder.c_str());
			system(command.c_str());
		}
	}

	FileDialogResult openFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		(void)filter;
		return runAppleFileDialog("choose file", title, out_path, out_path_size);
	}

	FileDialogResult saveFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		(void)filter;
		return runAppleFileDialog("choose file name", title, out_path, out_path_size);
	}

	FileDialogResult openFolderDialog(const char* title, char* out_path, size_t out_path_size)
	{
		return runAppleFileDialog("choose folder", title, out_path, out_path_size);
	}

	bool openDialog(const char* title, const char* msg, DialogButtonFlags flags, DialogButtonFlags* out_flag)
	{
		if (out_flag == nullptr)
		{
			return false;
		}

		return runAppleMessageDialog(title, msg, flags, out_flag);
	}

	void openFolderInExplorer(const char* path)
	{
		if (path == nullptr)
		{
			return;
		}

		openFolderInExplorerImpl(path);
	}
}

#else

#include <sys/wait.h>

namespace os
{
	namespace
	{
		FileDialogResult runLinuxFileDialog(const char* command, char* out_path, size_t out_path_size)
		{
			FILE* pipe = popen(command, "r");
			if (pipe == nullptr)
			{
				return FileDialogResult::ErrorUnknown;
			}

			if (!tryReadDialogLine(pipe, out_path, out_path_size))
			{
				pclose(pipe);
				return FileDialogResult::Canceled;
			}

			pclose(pipe);
			return FileDialogResult::OK;
		}

		bool runLinuxMessageDialog(const std::string& command, DialogButtonFlags* out_flag)
		{
			const int status = system(command.c_str());
			if (status == -1 || !WIFEXITED(status))
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			*out_flag = flagFromExitCode(WEXITSTATUS(status));
			return *out_flag != DialogButtonFlags::None;
		}

		std::string buildXMessageCommand(const char* title, const char* msg, DialogButtonFlags flags)
		{
			DialogButtonSpec buttons[3] = {};
			DialogButtonFlags defaultButton = DialogButtonFlags::Ok;
			DialogButtonFlags cancelButton = DialogButtonFlags::None;
			const size_t buttonCount = buildDialogButtons(flags, buttons, defaultButton, cancelButton);

			std::string buttonList;
			for (size_t index = 0; index < buttonCount; ++index)
			{
				if (index != 0)
				{
					buttonList += ",";
				}
				buttonList += buttons[index].label;
				buttonList += ":";
				buttonList += std::to_string(buttons[index].exitCode);
			}

			std::string command = "xmessage -center -buttons ";
			command += shellQuote(buttonList.c_str());
			command += " -default ";
			command += shellQuote(buttonLabel(defaultButton));
			if (title != nullptr && title[0] != '\0')
			{
				command += " -title ";
				command += shellQuote(title);
			}
			command += " ";
			command += shellQuote(msg != nullptr ? msg : "");
			return command;
		}
	}

	FileDialogResult openFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		if (out_path == nullptr || out_path_size == 0)
		{
			return FileDialogResult::ErrorNullPointer;
		}

		std::string script = "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --title=";
		script += shellQuote(title != nullptr ? title : "Open File");
		script += " 2>/dev/null; elif command -v kdialog >/dev/null 2>&1; then kdialog --getopenfilename . --title ";
		script += shellQuote(title != nullptr ? title : "Open File");
		script += " 2>/dev/null; else exit 127; fi";
		std::string command = "sh -c ";
		command += shellQuote(script.c_str());
		(void)filter;
		return runLinuxFileDialog(command.c_str(), out_path, out_path_size);
	}

	FileDialogResult saveFileDialog(const char* title, const char* filter, char* out_path, size_t out_path_size)
	{
		if (out_path == nullptr || out_path_size == 0)
		{
			return FileDialogResult::ErrorNullPointer;
		}

		std::string script = "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --save --confirm-overwrite --title=";
		script += shellQuote(title != nullptr ? title : "Save File");
		script += " 2>/dev/null; elif command -v kdialog >/dev/null 2>&1; then kdialog --getsavefilename . --title ";
		script += shellQuote(title != nullptr ? title : "Save File");
		script += " 2>/dev/null; else exit 127; fi";
		std::string command = "sh -c ";
		command += shellQuote(script.c_str());
		(void)filter;
		return runLinuxFileDialog(command.c_str(), out_path, out_path_size);
	}

	FileDialogResult openFolderDialog(const char* title, char* out_path, size_t out_path_size)
	{
		if (out_path == nullptr || out_path_size == 0)
		{
			return FileDialogResult::ErrorNullPointer;
		}

		std::string script = "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --directory --title=";
		script += shellQuote(title != nullptr ? title : "Select Folder");
		script += " 2>/dev/null; elif command -v kdialog >/dev/null 2>&1; then kdialog --getexistingdirectory . --title ";
		script += shellQuote(title != nullptr ? title : "Select Folder");
		script += " 2>/dev/null; else exit 127; fi";
		std::string command = "sh -c ";
		command += shellQuote(script.c_str());
		return runLinuxFileDialog(command.c_str(), out_path, out_path_size);
	}

	bool openDialog(const char* title, const char* msg, DialogButtonFlags flags, DialogButtonFlags* out_flag)
	{
		if (out_flag == nullptr)
		{
			return false;
		}

		std::string command;
		if (hasAll(flags, DialogButtonFlags::YesNoCancel))
		{
			command = "kdialog --yesnocancel ";
			command += shellQuote(msg != nullptr ? msg : "");
			if (title != nullptr && title[0] != '\0')
			{
				command += " --title ";
				command += shellQuote(title);
			}
			command += " 2>/dev/null";
			const int status = system(command.c_str());
			if (status == -1 || !WIFEXITED(status))
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			switch (WEXITSTATUS(status))
			{
			case 0:
				*out_flag = DialogButtonFlags::Yes;
				return true;
			case 1:
				*out_flag = DialogButtonFlags::No;
				return true;
			case 2:
				*out_flag = DialogButtonFlags::Cancel;
				return true;
			default:
				*out_flag = DialogButtonFlags::None;
				return false;
			}
		}

		if (hasAll(flags, DialogButtonFlags::YesNo))
		{
			command = "kdialog --yesno ";
			command += shellQuote(msg != nullptr ? msg : "");
			if (title != nullptr && title[0] != '\0')
			{
				command += " --title ";
				command += shellQuote(title);
			}
			command += " 2>/dev/null";
			const int status = system(command.c_str());
			if (status == -1 || !WIFEXITED(status))
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			*out_flag = WEXITSTATUS(status) == 0 ? DialogButtonFlags::Yes : DialogButtonFlags::No;
			return true;
		}

		if (hasAll(flags, DialogButtonFlags::AbortRetryIgnore) || hasAll(flags, DialogButtonFlags::OkRetryCancel))
		{
			command = buildXMessageCommand(title, msg, flags);
			return runLinuxMessageDialog(command, out_flag);
		}

		if (hasAll(flags, DialogButtonFlags::RetryCancel))
		{
			command = "zenity --question --ok-label=";
			command += shellQuote("Retry");
			command += " --cancel-label=";
			command += shellQuote("Cancel");
			command += " --text=";
			command += shellQuote(msg != nullptr ? msg : "");
			if (title != nullptr && title[0] != '\0')
			{
				command += " --title ";
				command += shellQuote(title);
			}
			command += " 2>/dev/null";
			const int status = system(command.c_str());
			if (status == -1 || !WIFEXITED(status))
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			*out_flag = WEXITSTATUS(status) == 0 ? DialogButtonFlags::Retry : DialogButtonFlags::Cancel;
			return true;
		}

		if (hasAll(flags, DialogButtonFlags::OkCancel))
		{
			command = "zenity --question --ok-label=";
			command += shellQuote("Ok");
			command += " --cancel-label=";
			command += shellQuote("Cancel");
			command += " --text=";
			command += shellQuote(msg != nullptr ? msg : "");
			if (title != nullptr && title[0] != '\0')
			{
				command += " --title ";
				command += shellQuote(title);
			}
			command += " 2>/dev/null";
			const int status = system(command.c_str());
			if (status == -1 || !WIFEXITED(status))
			{
				*out_flag = DialogButtonFlags::None;
				return false;
			}

			*out_flag = WEXITSTATUS(status) == 0 ? DialogButtonFlags::Ok : DialogButtonFlags::Cancel;
			return true;
		}

		command = buildXMessageCommand(title, msg, flags);
		return runLinuxMessageDialog(command, out_flag);
	}

	void openFolderInExplorer(const char* path)
	{
		const std::string folder = resolveFolderPath(path).string();
		if (folder.empty())
		{
			return;
		}

		std::string command = "xdg-open ";
		command += shellQuote(folder.c_str());
		command += " 2>/dev/null";
		system(command.c_str());
	}
}

#endif
