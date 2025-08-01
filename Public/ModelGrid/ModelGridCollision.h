// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridConstants.h"
#include "Math/GSAxisBox3.h"
#include "Math/GSRay3.h"

#include <unordered_map>

namespace GS
{

class GRADIENTSPACEGRID_API ModelGridCollider
{
protected:
	ModelGridConstants GridConstants;

public:
	~ModelGridCollider();

	void Initialize(const ModelGrid& TargetGrid);

	void UpdateInBounds(const ModelGrid& TargetGrid, const AxisBox3d& LocalBounds);

	bool FindNearestHitCell(const Ray3d& Ray, double& RayParameterOut, Vector3d& CellFaceNormal, Vector3i& CellKey) const;

protected:
	struct GridChunkCollider
	{
		Vector3i ChunkIndex;
		AxisBox3d ChunkBounds;

		// todo dumb, we do not need to store actual boxes, only the Vec3i's! boxes can be constructed!
		unsafe_vector<AxisBox3d> CellBounds;
	};

	// todo could probably use a grid that mirrors model chunkgrid here?
	std::unordered_map<Vector3i, GridChunkCollider*> ActiveChunks;

	void UpdateChunkCells(const ModelGrid& TargetGrid, GridChunkCollider& Chunk);
};


};
