// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"

#include "Math/GSIntVector2.h"
#include "Math/GSIntVector3.h"
#include "Math/GSFrame3.h"
#include "Math/GSAxisBox3.h"
#include "Math/GSIntAxisBox3.h"
#include "Grid/GSFixedGrid3.h"
#include "Intersection/GSRayBoxIntersection.h"

#include "ModelGrid/ModelGrid.h"


namespace GS
{

/**
 * Constant functions based on parameters of a ModelGrid instance
 */
class GRADIENTSPACEGRID_API ModelGridConstants
{
public:
	ModelGridConstants() {
		CellDimensions = Vector3d::One();
		CellIndexBounds = AxisBox3i::Empty();
		MinCoordCorner = Vector3i::Zero();
	}

	ModelGridConstants(const ModelGrid& Grid) {
		CellDimensions = Grid.CellDimensions;
		CellIndexBounds = Grid.CellIndexBounds;
		MinCoordCorner = Grid.MinCoordCorner;
	}

public:
	Vector3d CellDimensions;
	AxisBox3i CellIndexBounds;
	Vector3i MinCoordCorner;
	// todo include these? 
	// AxisBox3i AllocatedChunkBounds;
	// AxisBox3i ModifiedKeyBounds;

	using Block_CellType = FixedGrid3<uint16_t, ModelGrid::BlockSize_XY, ModelGrid::BlockSize_XY, ModelGrid::BlockSize_Z>;		// 16-bit indexable
	using Block_CellData = FixedGrid3<uint64_t, ModelGrid::BlockSize_XY, ModelGrid::BlockSize_XY, ModelGrid::BlockSize_Z>;

public:

	inline void ToGlobalLocal(const ModelGrid::CellKey& Key, Vector3i& Global, Vector3i& Local) const
	{
		Vector3i ShiftKey = Key - MinCoordCorner;
		Global = ShiftKey / Block_CellType::TypeDimensions();
		Local = ShiftKey - Global * Block_CellType::TypeDimensions();
	}

	inline ModelGrid::CellKey ToKey(const Vector3i& GlobalIndex, const Vector3i& LocalIndex) const
	{
		return GlobalIndex * Block_CellType::TypeDimensions() + LocalIndex + MinCoordCorner;
	}

	inline const Vector3d& CellSize() const { return this->CellDimensions; }
	inline const Vector3d& GetCellDimensions() const { return this->CellDimensions; }

	inline AxisBox3i GetCellIndexRange() const { return CellIndexBounds; }

	inline ModelGrid::CellKey GetCellAtPosition(const Vector3d& LocalPosition, bool& bIsInGrid) const
	{
		Vector3i CellIndex(
			(int)GS::Floor(LocalPosition.X / CellDimensions.X),
			(int)GS::Floor(LocalPosition.Y / CellDimensions.Y),
			(int)GS::Floor(LocalPosition.Z / CellDimensions.Z));
		bIsInGrid = CellIndexBounds.Contains(CellIndex);
		return ModelGrid::CellKey(CellIndex);
	}

	inline bool IsValidCell(ModelGrid::CellKey Key) const {
		return CellIndexBounds.Contains(Key);
	}

	inline AxisBox3d GetCellLocalBounds(ModelGrid::CellKey Key) const
	{
		Vector3d MinCorner(Key.X * CellDimensions.X, Key.Y * CellDimensions.Y, Key.Z * CellDimensions.Z);
		return AxisBox3d(MinCorner, MinCorner + CellDimensions);
	}

	inline Frame3d GetCellFrame(ModelGrid::CellKey Key) const
	{
		Vector3d MinCorner(Key.X * CellDimensions.X, Key.Y * CellDimensions.Y, Key.Z * CellDimensions.Z);
		return Frame3d(MinCorner);
	}

	inline Frame3d GetCellFrameWorld(ModelGrid::CellKey Key, const Frame3d& WorldFrame) const
	{
		Vector3d MinCorner(Key.X * CellDimensions.X, Key.Y * CellDimensions.Y, Key.Z * CellDimensions.Z);
		return Frame3d(WorldFrame.ToWorldPoint(MinCorner), WorldFrame.Rotation);
	}


	inline Vector3i GetChunkIndexForKey(ModelGrid::CellKey Key) const
	{
		Vector3i ShiftKey = Key - MinCoordCorner;
		return ShiftKey / Block_CellType::TypeDimensions();
	}
	inline AxisBox3i GetKeyRangeForChunk(const Vector3i& BlockIndex) const
	{
		Vector3i MinCorner = BlockIndex * Block_CellType::TypeDimensions();
		MinCorner += MinCoordCorner;
		Vector3i MaxCorner = MinCorner + Block_CellType::TypeDimensions() - Vector3i::One();
		return AxisBox3i(MinCorner, MaxCorner);
	}
	inline AxisBox3d GetChunkBounds(const Vector3i& BlockIndex) const
	{
		AxisBox3i ChunkCells = GetKeyRangeForChunk(BlockIndex);
		Vector3d MinCorner = (Vector3d)ChunkCells.Min * CellDimensions;
		Vector3d MaxCorner = (Vector3d)ChunkCells.Max * CellDimensions;
		return AxisBox3d(MinCorner, MaxCorner + CellDimensions);
	}

	inline bool AreCellsInSameBlock(ModelGrid::CellKey A, ModelGrid::CellKey B) const
	{
		return GetChunkIndexForKey(A) == GetChunkIndexForKey(B);
	}


	inline bool ComputeCellBoxIntersection(const Ray3d& Ray, const Vector3i& CellKey, double& RayParameterOut, Vector3d& HitPositionOut, Vector3d& CellFaceNormalOut) const
	{
		GS::AxisBox3d CellBounds = GetCellLocalBounds(CellKey);
		return GS::ComputeRayBoxIntersection(Ray, CellBounds, RayParameterOut, HitPositionOut, CellFaceNormalOut);
	}



};

}