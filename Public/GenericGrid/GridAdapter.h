// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"

#include "Math/GSVector3.h"
#include "Math/GSIntVector3.h"
#include "Color/GSColor3b.h"

#include <functional>

namespace GS
{

struct GenericGridCellState
{
	bool bFilled;
	uint64_t TypeValue;
	Vector3u64 IntValues;
	Vector3d FloatValues;
	Color3b Color;
};



struct UniformGridAdapter
{
public:
	std::function<Vector3d()> GetGridCellDimension;
	std::function<bool(const Vector3i& CellIndex)> IsValidIndex;
	std::function<Vector3i(const Vector3d& Position, bool& bIsValidIndexOut)> GetGridIndexForPosition;

	std::function<bool(const Vector3i& CellIndex, GenericGridCellState& StateOut)> GetCellState;

	std::function<bool(const Vector3i& CellIndex, const GenericGridCellState& NewState, bool& bCellWasModifiedOut)> SetCellState;
};



} // end namespace GS
