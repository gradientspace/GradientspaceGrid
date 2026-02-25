// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Math/GSIntVector3.h"
#include "Math/GSIntAxisBox3.h"

namespace GS
{


struct ModelGridVersions
{
	static constexpr uint32_t Version1 = 1;
	static constexpr uint32_t Version2 = 2;		// extended ModelGridCell.CellData to 64-bit, restructured RST data
	static constexpr uint32_t Version3 = 3;		// resized ModelGrid to be 16^3/16^3, instead of 32x32x16 / 32x32x32. changed how GridMaterial struct is intepreted.

	static constexpr uint32_t CurrentVersionNumber = Version3;
};




enum class EModelGridCellType
{
	Empty = 0,
	Filled = 1,
	Slab_Parametric = 4,
	Ramp_Parametric = 5,
	Corner_Parametric = 6,
	Pyramid_Parametric = 7,
	Peak_Parametric = 8,
	Cylinder_Parametric = 9,
	CutCorner_Parametric = 10,
	VariableCutCorner_Parametric = 11,
	VariableCutEdge_Parametric = 12,

	// TODO:
	// cone
	// quarter-arc (like cylinder)
	// quarter-hemisphere-section  (to join quarter-arcs at 90 degrees)
	// dome
	// L-shape
	// X-shape
	// diagonal connector(s)
	// square-tube, cylinder-tube


	Cubes8 = 32,

	MaxKnownCellType = 100,

	Max16BitCellType = 65500
};




struct CubeOrientation
{
	uint8_t Direction : 3;
	uint8_t Rotation : 2;

	CubeOrientation() {}
	CubeOrientation(uint8_t DirectionIn, uint8_t RotationIn)
	{
		Direction = DirectionIn;
		Rotation = RotationIn;
	}

	static CubeOrientation Identity() { return CubeOrientation(0, 0); }
};


struct GRADIENTSPACEGRID_API GridRegionHandle
{
	Vector3i BlockIndex;
	AxisBox3i CellIndexRange;

	// todo rename from 'Handle' to "Pointer" or "Reference" or something...
	void* GridHandle = nullptr;
	void* BlockHandle = nullptr;
	uint64_t MagicNumber = 0;

	bool operator==(const GridRegionHandle& OtherHandle) const
	{
		return BlockIndex == OtherHandle.BlockIndex
			&& CellIndexRange == OtherHandle.CellIndexRange
			&& GridHandle == OtherHandle.GridHandle
			&& BlockHandle == OtherHandle.BlockHandle
			&& MagicNumber == OtherHandle.MagicNumber;
	}
};







}
