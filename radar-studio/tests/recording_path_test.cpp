#include "../src/ui/recording_path.h"
#include <common/pcr/point_cloud_recorder.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

void expect(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

template<class Action>
void expectRejected(Action action, const char* message)
{
	bool rejected = false;
	try { action(); }
	catch (const std::exception&) { rejected = true; }
	expect(rejected, message);
}

std::string bytes(std::u8string_view text)
{
	return { reinterpret_cast<const char*>(text.data()), text.size() };
}

void testInput()
{
	const std::vector<std::string> names = {
		"ab", bytes(u8"\uD55C"), bytes(u8"\uD55C\uAE00"), bytes(u8"\uD55C\uAE00\uBA85"),
		bytes(u8"\uD55Ca\uAE00"), bytes(u8"a\uD55Cb\uAE00"), bytes(u8"\uD55C12\uAE00"), bytes(u8"\uD55C \uAE00")
	};
	for (const auto& name : names)
	{
		char buffer[256]{};
		recording_path::copyUtf8(buffer, name);
		expect(std::string(buffer) == name, "input is copied without changing UTF-8 bytes");
		const auto output = recording_path::outputPath(".", name);
		expect(os::text::pathToUtf8(output.stem()) == name, "preview/native filename match before Save");
	}
	char buffer[256]{};
	recording_path::copyUtf8(buffer, {});
	expect(buffer[0] == '\0', "empty UTF-8 input is NUL terminated");
	const auto korean = bytes(u8"\uD55C");
	std::string boundary;
	for (int i = 0; i < 85; ++i) boundary += korean;
	recording_path::copyUtf8(buffer, boundary);
	expect(std::strlen(buffer) == 255 && buffer[255] == '\0', "255-byte UTF-8 buffer boundary fits");
	expectRejected([&] { recording_path::copyUtf8(buffer, boundary + "a"); }, "256 bytes rejected");
	expect(std::string(buffer) == boundary, "oversized copy leaves existing buffer unchanged");
	expectRejected([&] { recording_path::copyUtf8(buffer, std::string("\xED\x95", 2)); }, "partial UTF-8 rejected");
	expectRejected([&] { recording_path::outputPath(".", std::string("\xC0\xAF", 2)); }, "overlong UTF-8 rejected");
	expectRejected([&] { os::text::pathFromUtf8(std::string("a\0b", 3)); }, "embedded NUL rejected");
	for (const auto* name : { "a?b", "a/b", "..", "CON", "nul.txt", "a:", "a.", "a ", "a\\b", "a\nb" })
		expectRejected([&] { recording_path::outputPath(".", name); }, "invalid filename rejected before Save");
	expectRejected([&] { recording_path::outputPath("a?b", "valid"); }, "invalid folder rejected");
	expectRejected([&] { recording_path::outputPath(".", std::string(300, 'a')); }, "unsupported long path rejected safely");
}

void testPreviewTransitions()
{
	const auto ga = bytes(u8"\uAC00");
	const auto gaNa = bytes(u8"\uAC00 \uB098");
	const std::vector<std::pair<std::string, bool>> steps = {
		{ ga, true }, { ga + " ", false }, { gaNa, true },
		{ ga + " ", false }, { ga, true },
		{ "test", true }, { "test ", false }, { "test file", true },
		{ bytes(u8"\uB808\uC774\uB354 \uB370\uC774\uD130"), true },
		{ "", true }, { "   ", false }, { "test.", false }, { "test?", false },
		{ "CON", false }, { std::string("\xED\x95", 2), false }, { gaNa, true }
	};
	std::string error = "previous operation error";
	for (const auto& [name, valid] : steps)
	{
		const auto path = recording_path::previewOutputPath(".", name, error);
		expect(!path.empty() == valid, "preview path controls Save availability for every editing step");
		expect(error.empty() == valid, "preview error follows current input, not previous failure");
		if (valid && !name.empty())
			expect(os::text::pathToUtf8(path.stem()) == name, "preview never trims or rewrites the input");
	}
	for (int i = 0; i < 10; ++i)
	{
		expect(recording_path::previewOutputPath(".", "test ", error).empty(), "repeated invalid preview stays disabled");
		expect(error == "Invalid file or folder name", "repeated preview feedback is stable");
	}
	expect(recording_path::previewOutputPath("bad?folder", "test file", error).empty() && !error.empty(),
		"invalid Folder Name also disables Save");
	expect(!recording_path::previewOutputPath(".", "test file", error).empty() && error.empty(),
		"correcting Folder Name clears preview feedback");
}

void testOperationErrorMessages()
{
	const std::runtime_error serializerError("Failed to serialize recording");
	expect(recording_path::errorMessage(serializerError) == "Failed to serialize recording",
		"actual Serializer failure text is preserved for Console logging");
	const auto path = os::text::pathFromUtf8(bytes(u8"\uD55C\uAE00 file.pcr"));
	const auto code = std::make_error_code(std::errc::permission_denied);
	const std::filesystem::filesystem_error accessError("Cannot create output", path, code);
	const auto expected = os::text::errorMessageUtf8(code) + " path=" + os::text::pathToUtf8(path);
	expect(recording_path::errorMessage(accessError) == expected,
		"actual filesystem failure retains localized message and Unicode path");
	std::string previewError;
	(void)recording_path::previewOutputPath(".", "test ", previewError);
	expect(recording_path::errorMessage(accessError) == expected, "preview does not suppress an actual operation error");
	(void)recording_path::previewOutputPath(".", "test file", previewError);
	expect(previewError.empty() && recording_path::errorMessage(accessError) == expected,
		"preview recovery does not change operation error text");
}

void testNativeIo()
{
	namespace fs = std::filesystem;
	const auto folder = fs::temp_directory_path() / (L"rp_" + std::to_wstring(GetCurrentProcessId()) +
		L"_" + std::to_wstring(GetTickCount64())) / L"\uD55C\uAE00 folder";
	fs::create_directories(folder);
	struct Cleanup
	{
		fs::path folder;
		std::vector<fs::path> files;
		~Cleanup()
		{
			std::error_code error;
			for (const auto& file : files) fs::remove(file, error);
			fs::remove(folder, error);
			fs::remove(folder.parent_path(), error);
		}
	} cleanup{ folder, {} };
	const auto utf8Folder = os::text::pathToUtf8(folder);
	char selected[256]{};
	const auto nativeDialogBytes = folder.string();
	const auto dialogPath = os::text::dialogPathToNative(nativeDialogBytes);
	recording_path::copyUtf8(selected, os::text::pathToUtf8(dialogPath));
	expect(selected == utf8Folder, "ACP dialog selection becomes UTF-8 UI folder");
	expect(os::text::pathFromUtf8(selected) == folder, "selected and typed folder refer to same native path");
	const auto name = bytes(u8"\uD55Ca\uAE00 12");
	const auto first = recording_path::outputPath(utf8Folder, name);
	expect(first == folder / L"\uD55Ca\uAE00 12.pcr", "actual native filename matches UTF-8 preview");
	PointCloudRecorder recorder;
	const auto id = recorder.addSession()->id;
	pcr::Frame frame{};
	frame.frameCount = 123;
	frame.points.push_back({});
	frame.points.front().x = 1.25f;
	frame.points.front().targetId = 255;
	recorder.appendFrame(id, { frame });
	cleanup.files.push_back(first);
	{
		std::ofstream output(first, std::ios::binary);
		expect(output && recorder.serializeToStream(output), "native Unicode PCR Save succeeds");
		output.flush();
		expect(output.good(), "PCR write completed");
	}
	PointCloudRecorder loaded;
	{
		std::ifstream input(first, std::ios::binary);
		expect(input && loaded.deserializeFromStream(input), "native Unicode PCR Open succeeds");
	}
	expect(loaded.getSessionCount() == 1 && loaded.getSessionByIndex(0)->frames.size() == 1,
		"session and frame count preserved");
	const auto& restored = loaded.getSessionByIndex(0)->frames.front().frame;
	expect(restored.frameCount == 123 && restored.points.size() == 1 && restored.points.front().x == 1.25f &&
		restored.points.front().targetId == 255, "PCR data preserved");
	recorder.importFrom(std::move(loaded));
	expect(recorder.getSessionCount() == 2, "existing Import preserves both sessions");
	const auto second = recording_path::outputPath(utf8Folder, name);
	expect(os::text::pathToUtf8(second.filename()) == name + "(1).pcr", "first collision suffix preserved");
	cleanup.files.push_back(second);
	{ std::ofstream output(second, std::ios::binary); expect(output.good(), "collision fixture written"); }
	expect(os::text::pathToUtf8(recording_path::outputPath(utf8Folder, name).filename()) == name + "(2).pcr",
		"second collision suffix preserved");
	const auto automatic = os::text::pathToUtf8(recording_path::outputPath(utf8Folder, "").stem());
	expect(automatic.size() == 15 && automatic[8] == '_', "empty filename keeps timestamp naming");
}

int main()
{
	try
	{
		testInput();
		testPreviewTransitions();
		testOperationErrorMessages();
		testNativeIo();
		std::cout << "Recording UTF-8 path tests PASS\n";
		return 0;
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
