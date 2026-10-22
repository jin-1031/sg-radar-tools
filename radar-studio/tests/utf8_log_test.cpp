#include <common/util/utf8.h>
#include <common/util/debug_console.h>
#include <common/pcr/point_cloud_recorder.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

std::string bytes(std::u8string_view input)
{
	return { reinterpret_cast<const char*>(input.data()), input.size() };
}

class Utf8Category : public std::error_category
{
public:
	const char* name() const noexcept override { return "test.utf8"; }
	std::string message(int) const override { return bytes(u8"UTF-8 \uD55C\uAE00 message"); }
};

void testKoreanPathIo()
{
	const auto directory = std::filesystem::temp_directory_path() /
		(L"radar_utf8_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
	expect(std::filesystem::create_directory(directory), "unique temporary directory");
	const auto path = directory / L"\uD55C\uAE00 file.pcr";
	struct Cleanup
	{
		std::filesystem::path file, directory;
		~Cleanup() { std::error_code ec; std::filesystem::remove(file, ec); std::filesystem::remove(directory, ec); }
	} cleanup{ path, directory };
	// These are the same native narrow-string fstream operations used by the existing UI.
	const auto native = path.string();
	PointCloudRecorder recorder;
	const auto id = recorder.addSession()->id;
	pcr::Frame frame{};
	frame.frameCount = 123;
	frame.points.push_back({});
	frame.points.front().targetId = 255;
	recorder.appendFrame(id, { frame });
	{
		std::ofstream output(native, std::ios::binary);
		expect(output && recorder.serializeToStream(output), "existing writer accepts Korean/spaced path");
		output.flush();
		expect(output.good(), "temporary recording write succeeds");
	}
	std::ifstream input(native, std::ios::binary);
	PointCloudRecorder loaded;
	expect(loaded.deserializeFromStream(input), "existing reader accepts Korean/spaced path");
	expect(loaded.getSessionCount() == 1 && loaded.getSessionByIndex(0)->frames.size() == 1 &&
		loaded.getSessionByIndex(0)->frames.front().frame.frameCount == 123, "PCR roundtrip unchanged");
	expect(os::text::nativePathToUtf8(native) == os::text::pathToUtf8(path), "Save/Open log path is exact UTF-8");
}

int main()
{
	try
	{
		const std::wstring wide = L"C:\\\uC790\uB8CC \uD3F4\uB354\\\uD55C\uAE00 file.pcr";
		const auto utf8 = bytes(u8"C:\\\uC790\uB8CC \uD3F4\uB354\\\uD55C\uAE00 file.pcr");
		expect(os::text::utf16ToUtf8(wide) == utf8, "UTF-16 Korean path converts exactly");
		expect(os::text::pathToUtf8(std::filesystem::path(wide)) == utf8, "native Unicode path display");
		expect(os::text::acpToUtf8("English 10054") == "English 10054", "ASCII unchanged");
		expect(os::text::utf16ToUtf8({}).empty() && os::text::acpToUtf8({}).empty(), "empty strings");
		const wchar_t invalid[] = { 0xD800 };
		expect(os::text::utf16ToUtf8({ invalid, 1 }) == "[invalid text]", "invalid UTF-16 is safe");

		BOOL replaced = FALSE;
		const UINT cp = GetACP();
		BOOL* usedDefault = cp == CP_UTF8 ? nullptr : &replaced;
		const int size = WideCharToMultiByte(cp, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, usedDefault);
		std::string native(size, '\0');
		expect(size > 0 && WideCharToMultiByte(cp, 0, wide.data(), static_cast<int>(wide.size()), native.data(), size,
			nullptr, usedDefault) == size, "make actual ACP fixture");
		if (!replaced)
		{
			expect(os::text::acpToUtf8(native) == utf8, "actual ACP converts exactly");
			expect(os::text::nativePathToUtf8(native) == utf8, "existing native file path remains displayable");
			testKoreanPathIo();
		}
		else std::cout << "ACP cannot represent this Korean fixture: pre-existing dialog limitation detected\n";
		if (cp != CP_UTF8)
		{
			const wchar_t nonAcp[] = { 0xD83D, 0xDE80 };
			BOOL lost = FALSE;
			char encoded[16]{};
			const int length = WideCharToMultiByte(cp, 0, nonAcp, 2, encoded, 16, nullptr, &lost);
			expect(length > 0, "non-ACP fixture conversion");
			if (lost)
			{
				expect(os::text::acpToUtf8({ encoded, static_cast<std::size_t>(length) }) != bytes(u8"\U0001F680"),
					"loss before logging cannot be recovered");
				std::cout << "ACP substitution limitation verified (not changed by log conversion)\n";
			}
		}

		const std::error_code error(10061, std::system_category());
		const auto formatted = os::text::errorMessageUtf8(error);
		wchar_t expected[2048]{};
		DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
			10061, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), expected, 2048, nullptr);
		expect(count > 0, "Windows system error fixture exists");
		while (count && (expected[count - 1] == L'\r' || expected[count - 1] == L'\n')) --count;
		const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, formatted.data(),
			static_cast<int>(formatted.size()), nullptr, 0);
		expect(chars > 0, "Windows error output is valid UTF-8");
		std::wstring decoded(chars, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, formatted.data(), static_cast<int>(formatted.size()), decoded.data(), chars);
		expect(decoded == std::wstring(expected, count), "system error preserves localized Unicode text");
		expect(error.value() == 10061 && error.category() == std::system_category(), "numeric code and category preserved");
		(void)os::text::errorMessageUtf8({ -1, std::system_category() });
		Utf8Category category;
		const std::error_code custom(10054, category);
		expect(os::text::errorMessageUtf8(custom) == category.message(10054), "existing UTF-8 category is not converted twice");
		DebugConsole console;
		console.getOutput() << utf8 << '\n';
		std::vector<std::string> lines;
		console.snapshot(lines);
		expect(lines.size() == 1 && lines.front() == utf8, "Console preserves UTF-8 bytes");
		std::cout << "UTF-8 log tests PASS; ACP=" << cp << '\n';
		return 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
