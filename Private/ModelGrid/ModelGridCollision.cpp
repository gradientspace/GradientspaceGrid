// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridCollision.h"
#include "Core/ParallelFor.h"
#include "Core/gs_debug.h"
#include "GenericGrid/BoxIndexing.h"
#include "Intersection/GSRayBoxIntersection.h"

using namespace GS;

ModelGridCollider::~ModelGridCollider()
{
	for (auto Pair : ActiveChunks)
	{
		GridChunkCollider* collider = Pair.second;
		if (collider != nullptr)
			delete collider;
	}
	ActiveChunks.clear();
}

void ModelGridCollider::Initialize(const ModelGrid& TargetGridIn)
{
	GridConstants = ModelGridConstants(TargetGridIn);

	gs_debug_assert(ActiveChunks.size() == 0);		// otherwise need to delete
}


void ModelGridCollider::UpdateInBounds(const ModelGrid& TargetGrid, const AxisBox3d& LocalBounds)
{
	AxisBox3i ChunkIdxRange = TargetGrid.GetAllocatedChunkRangeBounds(LocalBounds);
	if (ChunkIdxRange.IsValid() == false) return;
	Vector3i Dims = (Vector3i)ChunkIdxRange.AxisCounts();

	std::vector<Vector3i> UpdateChunks;
	UpdateChunks.reserve((Dims.X + 1) * (Dims.Y + 1) * (Dims.Z + 1));
	for (int zi = ChunkIdxRange.Min.Z; zi <= ChunkIdxRange.Max.Z; zi++)
	{
		for (int yi = ChunkIdxRange.Min.Y; yi <= ChunkIdxRange.Max.Y; yi++)
		{
			for (int xi = ChunkIdxRange.Min.X; xi <= ChunkIdxRange.Max.X; xi++)
			{
				Vector3i ChunkIndex(xi, yi, zi);
				if (TargetGrid.IsChunkIndexAllocated(ChunkIndex))
				{
					UpdateChunks.push_back(ChunkIndex);

					if (ActiveChunks.contains(ChunkIndex) == false)
					{
						GridChunkCollider* ChunkCollider = new GridChunkCollider();
						ChunkCollider->ChunkIndex = ChunkIndex;
						ChunkCollider->ChunkBounds = GridConstants.GetChunkBounds(ChunkIndex);
						ActiveChunks.insert({ ChunkIndex, ChunkCollider });
					}
				}
			}
		}
	}

	GS::ParallelFor((uint32_t)UpdateChunks.size(), [&](int Index)
	{
		Vector3i ChunkIndex = UpdateChunks[Index];
		auto found_itr = ActiveChunks.find(ChunkIndex);
		if (found_itr != ActiveChunks.end())
		{
			GridChunkCollider* FoundCollider = found_itr->second;
			UpdateChunkCells(TargetGrid, *FoundCollider);
		}
	});

}


void ModelGridCollider::UpdateChunkCells(const ModelGrid& TargetGrid, GridChunkCollider& ChunkCollider)
{
	ChunkCollider.CellBounds.clear();

	TargetGrid.EnumerateFilledChunkCells(ChunkCollider.ChunkIndex,
		[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo, const AxisBox3d& LocalBounds)
	{

		bool bFoundEmptyNeighbour = false;
		for (int k = 0; k < 6; ++k)
		{
			Vector3i NeighbourKey = Key + GS::FaceIndexToOffset(k);
			if (TargetGrid.IsCellEmpty(NeighbourKey))
			{
				bFoundEmptyNeighbour = true;
				break;
			}
		}
		if (bFoundEmptyNeighbour)
			ChunkCollider.CellBounds.add(LocalBounds);
	});
}


bool ModelGridCollider::FindNearestHitCell(const Ray3d& Ray, double& RayParameterOut, Vector3d& CellFaceNormal, Vector3i& CellKey) const
{
	// todo obvs should find nearest chunk first...which can be done based on grid DDA...
	// todo could test all hit chunks in parallel...

	double MinHitT = Mathd::SafeMaxValue();
	AxisBox3d MinHitBox;
	for (const auto& Pair : ActiveChunks)
	{
		const GridChunkCollider& Chunk = *Pair.second;

		double ChunkBoxHitT = GS::TestRayBoxIntersection(Ray, Chunk.ChunkBounds);
		if (ChunkBoxHitT < MinHitT)
		{
			for (const AxisBox3d& CellBox : Chunk.CellBounds)
			{
				double BoxHitT = GS::TestRayBoxIntersection(Ray, CellBox);
				if (BoxHitT < MinHitT)
				{
					MinHitT = BoxHitT;
					MinHitBox = CellBox;

					bool bIsInGrid = false;
					CellKey = GridConstants.GetCellAtPosition(CellBox.Center(), bIsInGrid);
					gs_debug_assert(bIsInGrid);
				}
			}
		}
	}

	if (MinHitT < Mathd::SafeMaxValue())
	{
		RayParameterOut = MinHitT;
		CellFaceNormal = Vector3d::UnitZ();

		// calculate normal. This is dumb and should come from ray/box intersection code.
		Vector3d RayHitPos = Ray.PointAt(RayParameterOut);
		Vector3d BoxCenter = MinHitBox.Center();
		Vector3d Extents = MinHitBox.Extents();
		double MaxDist = Extents.Dot(Extents);
		for (int j = 0; j < 6; ++j)
		{
			Vector3d FaceCenter(BoxCenter), FaceNormal(0,0,0);
			int Axis = j / 2;
			double Dir = (j == 0 || j == 2 || j == 4) ? 1.0 : -1.0;
			FaceCenter[Axis] += Dir * Extents[Axis];
			FaceNormal[Axis] += Dir;
			double HitPosDist = GS::Abs((RayHitPos - FaceCenter).Dot(FaceNormal));
			if (HitPosDist < MaxDist)
			{
				MaxDist = HitPosDist;
				CellFaceNormal = FaceNormal;
			}
		}

		return true;
	}

	return false;
}
