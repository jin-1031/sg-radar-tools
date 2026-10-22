#pragma once

#include <common/pcr/point_cloud_recorder.h>
#include <common/ui/window/docking_window.h>

#include <string>
#include <filesystem>
#include <exception>

class RecordingWindow : public DockingWindow
{
	friend class PointCloudWindow;

public:
	enum
	{
		kFolderNameSize = 256,
		kFileNameSize = 256,
		kDescriptionSize = 512,
		kTagSize = 256,
		kRenameBufSize = 256,
	};

	RecordingWindow();

	PointCloudRecorder& getRecorder();
	const PointCloudRecorder::RecordSession* getCurrentSession() const;

private:
	void onUpdate() override;
	void onDraw(const ImVec2& size) override;

	void onFolderOpen(const char* dir);
	void onFileOpen(const std::filesystem::path& path);
	void onImportFile(const std::filesystem::path& path);
	void onFileSave(const std::filesystem::path& path);
	void reportPathError(const std::exception& error);

	void onAddSessionButtonClicked();
	void onDeleteSessionButtonClicked();
	void onCopySessionButtonClicked();
	void onSessionSelected(int idx);

	void drawFileInfo(float content_width);
	void drawSessionList(float content_width);
	void drawSessionInfo(float content_width);

	PointCloudRecorder m_recorder;
	PointCloudRecorder::RecordSession* m_currSession = nullptr;
	int m_currSessionIdx = -1;
	int m_currFrameIdx = -1;

	std::filesystem::path m_pendingOpenPath;
	std::filesystem::path m_pendingImportPath;
	bool m_hasPendingDelete = false;
	bool m_fileIsOpen = false;

	bool m_isRenaming = false;
	bool m_renamePendingFocus = false;

	// ImGui input buffers contain UTF-8, not Windows ACP text.
	char m_folderNameBuf[kFolderNameSize]{};
	char m_fileNameBuf[kFileNameSize]{};
	char m_descriptionBuf[kDescriptionSize]{};
	char m_tagBuf[kTagSize]{};
	char m_renameBuf[kRenameBufSize]{};
};
