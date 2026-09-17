#pragma once

#include "../window_font.h"
#include "../../core/point_cloud_dataset.h"

#include <common/ui/window/docking_window.h>

#include <optional>
#include <string>

class PlaybackController;

class LaunchpadWindow : public DockingWindow
{
public:
	LaunchpadWindow();

private:
	void onDraw(const ImVec2& size) override;

	WindowFont m_fonts = {};
	PointCloudDataset* m_dataset = nullptr;
	PlaybackController* m_controller = nullptr;

	std::optional<std::string> m_heldRecordName;
};
