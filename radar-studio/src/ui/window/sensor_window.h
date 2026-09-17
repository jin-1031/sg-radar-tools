#pragma once

#include "../../device/retina.h"

#include <common/ui/window/docking_window.h>

class SensorWindow : public DockingWindow
{
public:
	SensorWindow();

	void syncDraftFromDevice();

private:
	void onDraw(const ImVec2& size) override;

	void drawConnection();
	void drawSensorInfo();
	void drawSensorSpec();
	void applySensorSpec();
	void cancelSensorSpec();
	void resetSensorSpec();

	retina::SensorSpec m_draftSensorSpec{};
};
