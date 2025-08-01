// Copyright Gradientspace Corp. All Rights Reserved.
#include "GenericGrid/RasterizeToGrid.h"
#include "Core/ParallelFor.h"
#include "Core/unsafe_vector.h"

#include <mutex>

using namespace GS;

void RasterizeToGrid::SetGrid(UniformGridAdapter* AdapterIn)
{
	Adapter = AdapterIn;
	CellDimensions = AdapterIn->GetGridCellDimension();
}

void RasterizeToGrid::SetBounds(const Frame3d& GridWorldFrameIn, const AxisBox3d& FrameRelativeBounds)
{
	GridFrameInWorld = GridWorldFrameIn;
	FrameBounds = FrameRelativeBounds;
}


bool RasterizeToGrid::BinaryRasterize(
	FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc,
	bool bIsThreadSafe)
{
	if (bIsThreadSafe)
		return BinaryRasterize_Parallel(IndicatorFunc);
	else
		return BinaryRasterize_SingleThread(IndicatorFunc);
}

bool RasterizeToGrid::BinaryRasterize_Parallel(
	FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc)
{
	// compute index bounds in grid frame
	AxisBox3i IndexBounds = AxisBox3i::Empty();
	bool bIsInGrid = false;
	IndexBounds.Contain( Adapter->GetGridIndexForPosition(FrameBounds.Min, bIsInGrid) );
	IndexBounds.Contain( Adapter->GetGridIndexForPosition(FrameBounds.Max, bIsInGrid) );

	bool bFilledAnyCells = false;

	struct FilledCell
	{
		Vector3i CellIndex;
		Color3b FillColor;
	};

	// For now we will assume that it is not safe to do multiple simutaneous writes to the grid adapter.
	// This is currently the case for the ModelGrid adapter (although it could be upgraded to do locking)
	std::mutex adapter_lock;

	int NumZ = IndexBounds.Max.Z - IndexBounds.Min.Z + 1;
	std::vector<AxisBox3i> SlabBounds;
	SlabBounds.resize(NumZ);
	GS::ParallelFor(NumZ, [&](int ZSlabIndex)
	{
		// find all the cells to fill for this 
		unsafe_vector<FilledCell> SlabCells;
		int zi = IndexBounds.Min.Z + ZSlabIndex;
		for (int yi = IndexBounds.Min.Y; yi <= IndexBounds.Max.Y; yi++)
		{
			for (int xi = IndexBounds.Min.X; xi <= IndexBounds.Max.X; xi++)
			{
				Vector3i CellIndex(xi, yi, zi);
				if (Adapter->IsValidIndex(CellIndex) == false) continue;

				Vector3d CellMinCorner = ((Vector3d)CellIndex) * CellDimensions;
				Vector3d CellMaxCorner = CellMinCorner + CellDimensions;
				Vector3d CellCenter = 0.5 * (CellMinCorner + CellMaxCorner);

				Vector3d CellCenterWorld = GridFrameInWorld.ToWorldPoint(CellCenter);

				bool bInside = IndicatorFunc(CellCenterWorld);
				if (bInside)
				{
					Color3b FillColor = DefaultColor;
					Color3b FoundColor;
					if (ColorSampleFunc && ColorSampleFunc(CellCenterWorld, FoundColor)) {
						FillColor = FoundColor;
					}
					SlabCells.add(FilledCell{ CellIndex, FillColor });
				}
			}
		}

		if ( SlabCells.size() == 0 ) return;

		AxisBox3i ModifiedSlabBounds = AxisBox3i::Empty();
		adapter_lock.lock();
		for (FilledCell cell : SlabCells)
		{
			GenericGridCellState CellState;
			if (Adapter->GetCellState(cell.CellIndex, CellState))
			{
				CellState.bFilled = true;
				CellState.Color = cell.FillColor;
				bool bModified = false;
				if (Adapter->SetCellState(cell.CellIndex, CellState, bModified))
				{
					if (bModified) 
						ModifiedSlabBounds.Contain(cell.CellIndex);
				}
			}
		}
		adapter_lock.unlock();
		SlabBounds[ZSlabIndex] = ModifiedSlabBounds;
	});

	ModifiedCellIndexBounds = AxisBox3i::Empty();
	for (int zi = 0; zi < NumZ; ++zi)
		ModifiedCellIndexBounds.Contain(SlabBounds[zi]);

	return ModifiedCellIndexBounds.VolumeCount() > 0;
}



bool RasterizeToGrid::BinaryRasterize_SingleThread(
	FunctionRef<bool(const Vector3d& WorldPosition)> IndicatorFunc)
{
	// compute index bounds in grid frame
	AxisBox3i IndexBounds = AxisBox3i::Empty();
	bool bIsInGrid = false;
	IndexBounds.Contain(Adapter->GetGridIndexForPosition(FrameBounds.Min, bIsInGrid));
	IndexBounds.Contain(Adapter->GetGridIndexForPosition(FrameBounds.Max, bIsInGrid));

	bool bFilledAnyCells = false;
	AxisBox3i ModifiedBounds = AxisBox3i::Empty();

	for (int zi = IndexBounds.Min.Z; zi <= IndexBounds.Max.Z; zi++)
	{
		for (int yi = IndexBounds.Min.Y; yi <= IndexBounds.Max.Y; yi++)
		{
			for (int xi = IndexBounds.Min.X; xi <= IndexBounds.Max.X; xi++)
			{
				Vector3i CellIndex(xi, yi, zi);
				if (Adapter->IsValidIndex(CellIndex) == false) continue;

				Vector3d CellMinCorner = ((Vector3d)CellIndex) * CellDimensions;
				Vector3d CellMaxCorner = CellMinCorner + CellDimensions;
				Vector3d CellCenter = 0.5 * (CellMinCorner + CellMaxCorner);

				Vector3d CellCenterWorld = GridFrameInWorld.ToWorldPoint(CellCenter);

				bool bInside = IndicatorFunc(CellCenterWorld);
				if (bInside)
				{
					GenericGridCellState CellState;
					if ( Adapter->GetCellState(CellIndex, CellState) )
					{
						CellState.Color = DefaultColor;
						Color3b FoundColor;
						if (ColorSampleFunc && ColorSampleFunc(CellCenterWorld, FoundColor))
						{
							CellState.Color = FoundColor;
						}

						CellState.bFilled = true;
						bool bModified = false;
						if (Adapter->SetCellState(CellIndex, CellState, bModified))
						{
							if (bModified)
							{
								bFilledAnyCells = true;
								ModifiedBounds.Contain(CellIndex);
							}
						}
					}
				}
			}
		}
	}

	ModifiedCellIndexBounds = ModifiedBounds;

	return bFilledAnyCells;
}

