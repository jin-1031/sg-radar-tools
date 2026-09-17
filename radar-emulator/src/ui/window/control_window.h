#pragma once

#include "../window_font.h"
#include "../../core/point_cloud_dataset.h"

#include <common/ui/window/docking_window.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class MockServer;
class PlaybackController;

class ControlWindow : public DockingWindow
{
public:
	static constexpr int kFolderBufferSize = 512;

	ControlWindow();
	~ControlWindow() override;

private:
	void onUpdate() override;
	void onDraw(const ImVec2& size) override;

	void drawTopPanel();
	void drawLoadingBanner();
	void drawServerSection();
	void drawDatasetSection();
	void drawPlaybackSection();
	void drawStatusSection();

	bool startLoadingFolder(const char* folder_path);
	void applyPendingLoadResult();
	void joinLoaderIfReady();
	bool isLoading() const;

	struct LoadResult
	{
		PointCloudDataset dataset;
		std::filesystem::path folderPath;
		std::string error;
		bool success = false;
	};

	WindowFont m_fonts = {};
	PointCloudDataset* m_dataset = nullptr;
	PlaybackController* m_controller = nullptr;
	MockServer* m_server = nullptr;

	char m_folderBuffer[kFolderBufferSize] = {};

	std::thread m_loaderThread;
	std::atomic_bool m_loadingActive = false;
	std::atomic_bool m_loadingReady = false;
	mutable std::mutex m_loadingMutex;
	std::unique_ptr<LoadResult> m_pendingLoadResult;
	std::string m_loadingStatus = "Idle";
};
