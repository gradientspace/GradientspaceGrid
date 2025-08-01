// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "GenericGrid/GridAdapter.h"
#include "Math/GSIntAxisBox3.h"
#include "Math/GSIntVector2.h"
#include "Color/GSIntColor4.h"
#include "Core/FunctionRef.h"

namespace GS
{

// TODO: need some concept of a configurable GenericGridCellState mutator that can externalize the set filled/empty type/etc


class GRADIENTSPACEGRID_API ImageToGrid
{
public:
	void SetGrid(UniformGridAdapter* Adapter);

	bool Rasterize(
		const Vector2i& ImageCoordMin, 
		const Vector2i& ImageCoordMax,
		const Vector3i& GridMinCoord,
		const Vector2i& AxisMapping,
		FunctionRef<bool(const Vector2i& PixelCoord, Color4b& PixelColorOut )> ImageSampleFunc);

protected:
	UniformGridAdapter* Adapter;
	AxisBox3i ModifiedCellIndexBounds;
};


} // end namespace GS
