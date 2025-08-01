// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "GenericGrid/GridAdapter.h"
#include "Math/GSFrame3.h"
#include "Math/GSAxisBox3.h"
#include "Math/GSIntAxisBox3.h"
#include "Color/GSColor3b.h"
#include "Core/FunctionRef.h"

#include <functional>

namespace GS
{

// TODO: need some concept of a configurable GenericGridCellState mutator that can externalize the set filled/empty type/etc


class GRADIENTSPACEGRID_API RasterizeToGrid
{
public:
	void SetGrid(UniformGridAdapter* Adapter);

	void SetBounds(const Frame3d& GridWorldFrameIn, const AxisBox3d& FrameRelativeBounds);

	bool BinaryRasterize(
		FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc,
		bool bIsThreadSafe
	);

	Color3b DefaultColor = Color3b::White();
	std::function<bool(const Vector3d& WorldPosition, Color3b& Color)> ColorSampleFunc;

protected:
	UniformGridAdapter* Adapter;
	Vector3d CellDimensions;

	Frame3d GridFrameInWorld;
	AxisBox3d FrameBounds;

	AxisBox3i ModifiedCellIndexBounds;

	bool BinaryRasterize_SingleThread(
		FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc);
	bool BinaryRasterize_Parallel(
		FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc);
};



} // end namespace GS
