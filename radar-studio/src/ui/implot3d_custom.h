#pragma once

#include <implot3d.h>

// Getter callback for custom scatter data stored in arbitrary user memory.
// #user points to caller context, #point points to one element in the data buffer.
typedef void (*ImPlot3DScatterGetter)(void* user, const void* point, ImPlot3DPoint* out_point, ImU32* out_color);

typedef int ImPlot3DViewGizmoFlags;

enum ImPlot3DViewGizmoFlags_
{
	ImPlot3DViewGizmoFlags_None = 0,
	ImPlot3DViewGizmoFlags_Reset = 1 << 0,
	ImPlot3DViewGizmoFlags_RotationChanged = 1 << 1,
	ImPlot3DViewGizmoFlags_BoxChanged = 1 << 2,
	ImPlot3DViewGizmoFlags_ScaleChanged = 1 << 3,
};

namespace MyPlot3D
{
	void applyTheme();

	// content_key identifies point payload + color settings. Non-zero enables a
	// per-plot vertex cache when the 3D view has not changed (P3). Pass 0 to
	// always rebuild.
	void PlotScatter(
		const char* label,
		void* user,
		const void* data,
		int count,
		int stride,
		ImPlot3DScatterGetter getter,
		const ImPlot3DSpec& spec = ImPlot3DSpec(),
		ImU64 content_key = 0);

	void PlotBox(
		const char* label,
		double minx,
		double maxx,
		double miny,
		double maxy,
		double minz,
		double maxz,
		const ImPlot3DSpec& spec);

	ImPlot3DViewGizmoFlags ShowViewGizmo(
		const char* label,
		float size = 96.0f,
		ImPlot3DQuat* rotation = nullptr,
		ImPlot3DBox* axes_box = nullptr,
		double* zoom = nullptr);

	void SetViewGizmoInitialRotation(const char* label, const ImPlot3DQuat& rotation);
	void SetViewGizmoInitialAxesBox(const char* label, const ImPlot3DBox& axes_box);
	void SetViewGizmoInitialZoom(const char* label, double zoom);

}
