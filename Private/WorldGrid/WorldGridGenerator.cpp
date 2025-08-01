// Copyright Gradientspace Corp. All Rights Reserved.
#include "WorldGrid/WorldGridGenerator.h"
#include "ModelGrid/ModelGridWorker.h"
#include "ModelGrid/ModelGridEditMachine.h"

#include "Core/gs_debug.h"
#include "Grid/GSGridUtil.h"
#include "GenericGrid/BoxIndexing.h"
#include "Sampling/GSNoise.h"

#include <mutex>

using namespace GS;


WorldGridGenerator::~WorldGridGenerator()
{

}


static bool find_adjacent_neighbours(bool neighbours[4], int& a, int& b)
{
	if (neighbours[0] == false && neighbours[1] == false) { a = 0; b = 1; return true; }
	if (neighbours[1] == false && neighbours[2] == false) { a = 1; b = 2; return true; }
	if (neighbours[2] == false && neighbours[3] == false) { a = 2; b = 3; return true; }
	if (neighbours[3] == false && neighbours[0] == false) { a = 3; b = 0; return true; }
	return false;
}


namespace GS
{
	template<typename ValType>
	int IndexOf(const ValType* Array, size_t Count, FunctionRef<bool(const ValType&)> Predicate, int StartOffset = 0)
	{
		for (size_t k = StartOffset; k < Count; ++k)
		{
			if (Predicate(Array[k]))
				return (int)k;
		}
		return -1;
	}

	template<typename ValType>
	int IndexOf(const ValType* Array, size_t Count, const ValType& Value, int StartOffset = 0)
	{
		for (size_t k = StartOffset; k < Count; ++k)
		{
			if (Array[k] == Value)
				return (int)k;
		}
		return -1;
	}
}


void WorldGridGenerator::PopulateRegionBlocks_Blocking(
	BlockInfo BlockInfo, 
	ModelGrid& RegionGrid, 
	const std::vector<GridRegionHandle>& PendingHandles,
	WorldRegionModelGridInfo& ExtendedInfo)
{
	std::mutex ModifiedLock;

	// TODO: need to somehow avoid processing empty blocks, do not want to allocate them...
	// but also need to mark them as empty in DB otherwise they will keep being re-processed...

	ModelGridWorkManager WorkManager;
	WorkManager.Initialize(RegionGrid);
	WorkManager.bParallelProcess = true;
	for (GridRegionHandle Handle : PendingHandles)
		WorkManager.AddRegionHandle(Handle);

	auto ComputeZFunc = [&](int WorldX, int WorldY)
	{
		Vector2i MountainCenter(100, 100);
		Vector2i MountainDelta = Vector2i(WorldX, WorldY) - MountainCenter;
		double DistXY = GS::Sqrt((double)MountainDelta.SquaredLength());
		double UnitOriginDist = GS::Clamp(DistXY / 125, 0.0, 1.0);
		double t = GS::Clamp((1 - UnitOriginDist * UnitOriginDist), 0.0, 1.0);
		t = t * t * t;
		int MountainZ = (int)(100 * t);

		double Frequency = 1.0 / 40.0;
		Vector2d SamplePos(Frequency * WorldX, Frequency * WorldY);
		// just taking a slice of 3D noise here
		double NoiseVal = GS::PerlinNoise3D(Vector3d(SamplePos.X, SamplePos.Y, 0));
		double Magnitude = 16;
		int PerlinZ = (int)(Magnitude * NoiseVal * (1.0 - t));

		int UseZ = PerlinZ + MountainZ;
		return (int)(BlockInfo.OriginCell.Z + UseZ);
	};

	auto ComputeNormalFunc = [&](int WorldX, int WorldY)
	{
		Vector3d XMin((double)(WorldX-1), (double)WorldY, ComputeZFunc(WorldX-1, WorldY));
		Vector3d XMax((double)(WorldX+1), (double)WorldY, ComputeZFunc(WorldX+1, WorldY));
		Vector3d YMin((double)WorldX, (double)(WorldY-1), ComputeZFunc(WorldX, WorldY-1));
		Vector3d YMax((double)WorldX, (double)(WorldY+1), ComputeZFunc(WorldX, WorldY+1));
		Vector3d Normal = UnitCross( Normalized(XMax-XMin), Normalized(YMax-YMin) );
		return Normal;
	};

	WorkManager.EditRegions_Immediate([&](const GridRegionHandle& RegionHandle)
	{
		// ugh gross
		int64_t UseSeed = GS::ToLinearGridIndex((Vector3i)BlockInfo.WorldRegionIndex, WorldGridDB::TypeRegionDimensions()) *
			GS::ToLinearGridIndex(RegionHandle.BlockIndex, ModelGrid::BlockDimensions());
		GS::RandomStream BlockRandom( (uint32_t)UseSeed );


		AxisBox3i IndexRange = RegionHandle.CellIndexRange;
		AxisBox3i WorldIndexRange(
			(IndexRange.Min - BlockInfo.BlockMinIndex) + BlockInfo.WorldCellIndexRange.Min,
			(IndexRange.Max - BlockInfo.BlockMinIndex) + BlockInfo.WorldCellIndexRange.Min);

		int GroundDepth = 3;

		int ZMin = Math32i::MaxValue(), ZMax = -Math32i::MaxValue();
		Vector2i MinXY(WorldIndexRange.Min.X, WorldIndexRange.Min.Y), MaxXY(WorldIndexRange.Max.X, WorldIndexRange.Max.Y);
		GS::EnumerateCellsInRangeInclusive(MinXY, MaxXY, [&](const Vector2i& CellIndex)
		{
			int Z = ComputeZFunc(CellIndex.X, CellIndex.Y);
			ZMin = GS::Min(ZMin, Z - GroundDepth); ZMax = GS::Max(ZMax, Z);
		});

		// sanity check...
		gs_debug_assert( ExtendedInfo.BlockStates.Get(RegionHandle.BlockIndex).IsGenerated == 0 );

		WorldRegionModelGridInfo::BlockInfo GeneratedBlockInfo;
		GeneratedBlockInfo.Fields = 0;
		GeneratedBlockInfo.IsGenerated = 1;

		// if no values in cell, mark it as empty
		if (ZMin > WorldIndexRange.Max.Z || ZMax < WorldIndexRange.Min.Z)
		{
			GeneratedBlockInfo.IsEmpty = 1;
			ExtendedInfo.BlockStates.Set(RegionHandle.BlockIndex, GeneratedBlockInfo);
			return;
		}

		// TODO: this block is now marked as non-empty, but it may actually be empty...
		ExtendedInfo.BlockStates.Set(RegionHandle.BlockIndex, GeneratedBlockInfo);

		RandomizeColorModifier ColorModifier;
		ColorModifier.HueRange = 0; ColorModifier.ValueRange = .05f; ColorModifier.SaturationRange = 0.05f;
		ColorModifier.RandomHelper.Initialize( (uint32_t)ExtendedInfo.BlockStates.ToLinearIndex(RegionHandle.BlockIndex) );

		bool bModifiedAnyCell = false;
		ModelGrid::UnsafeRawBlockEditor BlockEditor = RegionGrid.GetRawBlockEditor_Safe(RegionHandle);
		for (int yi = IndexRange.Min.Y; yi <= IndexRange.Max.Y; yi++)
		{
			for (int xi = IndexRange.Min.X; xi <= IndexRange.Max.X; xi++)
			{
				int WorldCellX = (xi - BlockInfo.BlockMinIndex.X) + BlockInfo.WorldCellIndexRange.Min.X;
				int WorldCellY = (yi - BlockInfo.BlockMinIndex.Y) + BlockInfo.WorldCellIndexRange.Min.Y;
				int WorldCellZ = ComputeZFunc(WorldCellX, WorldCellY);
				int BlockMaxZ = (WorldCellZ - BlockInfo.WorldCellIndexRange.Min.Z) + BlockInfo.BlockMinIndex.Z;
				int BlockMinZ = (BlockMaxZ - GroundDepth);

				// check if Z interval overlaps
				if (BlockMinZ > IndexRange.Max.Z || BlockMaxZ < IndexRange.Min.Z)
					continue;

				// compute Z interval intersection
				int UseMinZ = GS::Max(BlockMinZ, IndexRange.Min.Z), UseMaxZ = GS::Min(BlockMaxZ, IndexRange.Max.Z);

				for (int BlockZ = UseMinZ; BlockZ <= UseMaxZ; BlockZ++)
				{
					Vector3i RegionCellIndex(xi, yi, BlockZ);
					gs_debug_assert(IndexRange.Contains(RegionCellIndex));

					BlockEditor.SetCurrentCell(RegionCellIndex);
					ModelGridCell GridCell = BlockEditor.GetCellData();
					GridCell.CellType = EModelGridCellType::Filled;
					Color3b GreenSRGB = (Color3b)GS::LinearToSRGB( Vector3f(0, .168f, 0) );
					Color3b RandColor = ColorModifier.GetPaintColor(GreenSRGB, Color3b::Green(), GridCell);
					GridCell.CellMaterial = GridMaterial(Color4b(RandColor.R, RandColor.G, RandColor.B));
					BlockEditor.SetCellData(GridCell);
					bModifiedAnyCell = true;
				}
			}
		}

		// postprocesing hack
		if (bModifiedAnyCell)
		{
			Vector3d SideNormals[4] = { -Vector3d::UnitX(), Vector3d::UnitX(), -Vector3d::UnitY(), Vector3d::UnitY() };

			//GS::EnumerateCellsInRangeInclusive(IndexRange.Min, IndexRange.Max,
			//	[&](Vector3i RegionCellIndex)
			//{
			//	BlockEditor.SetCurrentCell(RegionCellIndex);
			//	ModelGridCell GridCell = BlockEditor.GetCellData();
			//	if (GridCell.CellType == EModelGridCellType::Filled)
			//	{
			//		int WorldCellX = (RegionCellIndex.X - BlockInfo.BlockMinIndex.X) + BlockInfo.WorldCellIndexRange.Min.X;
			//		int WorldCellY = (RegionCellIndex.Y - BlockInfo.BlockMinIndex.Y) + BlockInfo.WorldCellIndexRange.Min.Y;
			//		Vector3d WorldFieldNormal = ComputeNormalFunc(WorldCellX, WorldCellY);
			//		double UpDot = WorldFieldNormal.Dot(Vector3d::UnitZ());
			//		int BestSideNormalIndex = -1;
			//		double BestSideNormalDot = 0;
			//		for (int k = 0; k < 4; ++k)
			//		{
			//			double SideDot = SideNormals[k].Dot(WorldFieldNormal);
			//			if (SideDot > 0.25 && SideDot > BestSideNormalDot)
			//			{
			//				BestSideNormalDot = SideDot;
			//				BestSideNormalIndex = k;
			//			}
			//		}

			//		if (BestSideNormalIndex >= 0)
			//		{
			//			GridCell.CellType = EModelGridCellType::Ramp_Parametric;
			//			MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
			//			uint8_t Axis = 0, Angle = 0;
			//			MGCell_Ramp::DetermineOrientationFromAxes(Vector3d::UnitZ(), SideNormals[BestSideNormalIndex], Axis, Angle);
			//			RampCell.Params.AxisDirection = Axis;
			//			RampCell.Params.AxisRotation = Angle;
			//			UpdateGridCellParamsFromSubCell(GridCell, RampCell);
			//			BlockEditor.SetCellData(GridCell);
			//			bModifiedAnyCell = true;
			//		}
			//	}
			//});


			GS::EnumerateCellsInRangeInclusive(IndexRange.Min, IndexRange.Max,
				[&](Vector3i RegionCellIndex)
			{
				const int AxisOrderCW[4] = { 0, 2, 1, 3 };

				BlockEditor.SetCurrentCell(RegionCellIndex);
				ModelGridCell GridCell = BlockEditor.GetCellData();
				if (GridCell.CellType == EModelGridCellType::Filled)
				{
					int WorldCellX = (RegionCellIndex.X - BlockInfo.BlockMinIndex.X) + BlockInfo.WorldCellIndexRange.Min.X;
					int WorldCellY = (RegionCellIndex.Y - BlockInfo.BlockMinIndex.Y) + BlockInfo.WorldCellIndexRange.Min.Y;
					int WorldCellZ = (RegionCellIndex.Z - BlockInfo.BlockMinIndex.Z) + BlockInfo.WorldCellIndexRange.Min.Z;
					int ColumnMaxZ = ComputeZFunc(WorldCellX, WorldCellY);
					bool bAboveEmpty = (WorldCellZ >= ColumnMaxZ);
					if (!bAboveEmpty) return;		// only modify top cell in any given column

					int NbrDeltas[4] = { 0,0,0,0 };
					int NonZeros = 0, Positives = 0, Negatives = 0;
					for (int k = 0; k < 4; ++k)
					{
						Vector3i Offset = GS::FaceIndexToOffset(AxisOrderCW[k]);

						Vector3i NbrBlockIndex = RegionCellIndex + Offset;
						Vector3i WorldCellIdx = (NbrBlockIndex - BlockInfo.BlockMinIndex) + BlockInfo.WorldCellIndexRange.Min;
						int NbrColumnMaxZ = ComputeZFunc(WorldCellIdx.X, WorldCellIdx.Y);
						NbrDeltas[k] = NbrColumnMaxZ - WorldCellZ;
						if (NbrDeltas[k] < 0) { Negatives++; NonZeros++; }
						else if (NbrDeltas[k] > 0) { Positives++; NonZeros++; }
					}

					int DiagDeltas[4] = { 0,0,0,0 };
					int DiagEmpty = 0;
					for (int k = 0; k < 4; ++k)
					{
						Vector3i DiagOffset = GS::FaceIndexToOffset(AxisOrderCW[k]) + GS::FaceIndexToOffset(AxisOrderCW[(k+1)%4]);
						Vector3i NbrBlockIndex = RegionCellIndex + DiagOffset;
						Vector3i WorldCellIdx = (NbrBlockIndex - BlockInfo.BlockMinIndex) + BlockInfo.WorldCellIndexRange.Min;
						int NbrColumnMaxZ = ComputeZFunc(WorldCellIdx.X, WorldCellIdx.Y);
						DiagDeltas[k] = NbrColumnMaxZ - WorldCellZ;
						if (DiagDeltas[k] < 0) { DiagEmpty++; }
					}

					//if (WorldCellX == 85 && WorldCellY == 98 && WorldCellZ == 95)
					//{
					//	DebugBreak();
					//}

					// this really is kind of arbitrary and perhaps it would be better to consider 
					// delta-x and delta-y across a cell, ie do we have a down slope in either direction.
					// This might simplify many of the cases...

					
					if (NonZeros == 1 && Negatives == 1)		// simple down-ramp case, 3 flat nbrs and one down
					{
						int SideAxis = GS::IndexOf<int>(NbrDeltas, 4, -1);
						if (SideAxis >= 0)
						{
							GridCell.CellType = EModelGridCellType::Ramp_Parametric;
							MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							MGCell_Ramp::DetermineOrientationFromAxes(Vector3d::UnitZ(), GS::FaceIndexToNormal<double>(AxisOrderCW[SideAxis]), Axis, Angle);
							RampCell.Params.AxisDirection = Axis;
							RampCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, RampCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
					}
					else if (NonZeros == 2 && Negatives == 1 && Positives == 1)		// opposing up/down nbrs, other nbrs level => ramp
					{
						int DownAxis = GS::IndexOf<int>(NbrDeltas, 4, -1);
						if ( DownAxis >= 0 && NbrDeltas[(DownAxis+2)%4] == 1 )
						{
							GridCell.CellType = EModelGridCellType::Ramp_Parametric;
							MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							MGCell_Ramp::DetermineOrientationFromAxes(Vector3d::UnitZ(), GS::FaceIndexToNormal<double>(AxisOrderCW[DownAxis]), Axis, Angle);
							RampCell.Params.AxisDirection = Axis;
							RampCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, RampCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
					}
					else if (NonZeros == 2 && Negatives == 2)	// => 2 down, either adjacent (corner) or opposite (peak)
					{
						int FirstAxis = GS::IndexOf<int>(NbrDeltas, 4, -1);
						if ( FirstAxis >= 0 && ( (NbrDeltas[(FirstAxis+1)%4] == -1) || FirstAxis == 0 && NbrDeltas[1] == 0 && NbrDeltas[3] == -1 ) )
						{
							int SecondAxis = (FirstAxis == 0 && NbrDeltas[1] == 0 && NbrDeltas[3] == -1) ? 3 : ((FirstAxis + 1) % 4);
							GridCell.CellType = EModelGridCellType::Corner_Parametric;
							MGCell_Corner CornerCell = MGCell_Corner::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							Vector3d Diagonal = Normalized(GS::FaceIndexToNormal<double>(AxisOrderCW[FirstAxis]) + GS::FaceIndexToNormal<double>(AxisOrderCW[SecondAxis]) + Vector3d::UnitZ());
							MGCell_Corner::DetermineOrientationFromDiagonal(Diagonal, Axis, Angle);
							CornerCell.Params.AxisDirection = Axis;
							CornerCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, CornerCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
						else  // if not sequential then it must be a peak
						{
							// todo make this better...
							//GridCell.CellType = EModelGridCellType::Peak_Parametric;
							//MGCell_Peak PeakCell = MGCell_Peak::GetDefaultCellParams();
							////uint8_t Axis = 0, Angle = 0;
							////Vector3d Diagonal = Normalized(GS::FaceIndexToNormal<double>(AxisOrderCW[FirstAxis]) + GS::FaceIndexToNormal<double>(AxisOrderCW[SecondAxis]) + Vector3d::UnitZ());
							////MGCell_Peak::DetermineOrientationFromDiagonal(Diagonal, Axis, Angle);
							////CornerCell.Params.AxisDirection = Axis;
							////CornerCell.Params.AxisRotation = Angle;
							//UpdateGridCellParamsFromSubCell(GridCell, PeakCell);
							//BlockEditor.SetCellData(GridCell);
							//bModifiedAnyCell = true;
						}
					}
					else if (NonZeros == 4 && Negatives == 2 && Positives == 2)		// 2-up/2-down, if two adjacent negatives, apply corner again
					{
						int FirstAxis = GS::IndexOf<int>(NbrDeltas, 4, -1);
						if (FirstAxis >= 0 && ((NbrDeltas[(FirstAxis + 1) % 4] == -1) || FirstAxis == 0 && NbrDeltas[1] == 0 && NbrDeltas[3] == -1))
						{
							int SecondAxis = (FirstAxis == 0 && NbrDeltas[1] == 0 && NbrDeltas[3] == -1) ? 3 : ((FirstAxis + 1) % 4);
							GridCell.CellType = EModelGridCellType::Corner_Parametric;
							MGCell_Corner CornerCell = MGCell_Corner::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							Vector3d Diagonal = Normalized(GS::FaceIndexToNormal<double>(AxisOrderCW[FirstAxis]) + GS::FaceIndexToNormal<double>(AxisOrderCW[SecondAxis]) + Vector3d::UnitZ());
							MGCell_Corner::DetermineOrientationFromDiagonal(Diagonal, Axis, Angle);
							CornerCell.Params.AxisDirection = Axis;
							CornerCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, CornerCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
					}
					else if (NonZeros == 4 && Negatives == 4 && bAboveEmpty)
					{
						GridCell.CellType = EModelGridCellType::Pyramid_Parametric;
						MGCell_Pyramid PyramidCell = MGCell_Pyramid::GetDefaultCellParams();
						PyramidCell.Params.DimensionZ = MGCell_Pyramid::MaxDimension / 2;
						UpdateGridCellParamsFromSubCell(GridCell, PyramidCell);
						BlockEditor.SetCellData(GridCell);
						bModifiedAnyCell = true;
					}
					else if (Negatives == 0 && bAboveEmpty && DiagEmpty == 1)
					{
						int DiagIdx = GS::IndexOf<int>(DiagDeltas, 4, [&](int delta) { return delta < 0; } );
						int NextIdx = (DiagIdx + 1) % 4;
						GridCell.CellType = EModelGridCellType::CutCorner_Parametric;
						MGCell_CutCorner CutCornerCell = MGCell_CutCorner::GetDefaultCellParams();
						uint8_t Axis = 0, Angle = 0;
						Vector3d Diagonal = Normalized(GS::FaceIndexToNormal<double>(AxisOrderCW[DiagIdx]) + GS::FaceIndexToNormal<double>(AxisOrderCW[NextIdx]) + Vector3d::UnitZ());
						MGCell_Corner::DetermineOrientationFromDiagonal(Diagonal, Axis, Angle);
						CutCornerCell.Params.AxisDirection = Axis;
						CutCornerCell.Params.AxisRotation = Angle;
						UpdateGridCellParamsFromSubCell(GridCell, CutCornerCell);
						BlockEditor.SetCellData(GridCell);
						bModifiedAnyCell = true;
					}
					else if (Negatives > 0 && Positives == 1)
					{
						int UpAxis = GS::IndexOf<int>(NbrDeltas, 4, 1);
						if (UpAxis >= 0 && (NbrDeltas[(UpAxis + 2) % 4] < 0))
						{
							int UseDownAxis = (UpAxis + 2) % 4;
							GridCell.CellType = EModelGridCellType::Ramp_Parametric;
							MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							MGCell_Ramp::DetermineOrientationFromAxes(Vector3d::UnitZ(), GS::FaceIndexToNormal<double>(AxisOrderCW[UseDownAxis]), Axis, Angle);
							RampCell.Params.AxisDirection = Axis;
							RampCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, RampCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
					}


				}
			});


#if 0
			GS::EnumerateCellsInRangeInclusive(IndexRange.Min, IndexRange.Max,
				[&](Vector3i RegionCellIndex)
			{
				BlockEditor.SetCurrentCell(RegionCellIndex);
				ModelGridCell GridCell = BlockEditor.GetCellData();
				if (GridCell.CellType == EModelGridCellType::Filled)
				{
					bool Neighbours[6] = { false,false,false,false,false,false };
					int OccupiedCount = 0;
					for (int k = 0; k < 6; ++k)
					{
						ModelGridCell NbrCell;
						Vector3i Offset = GS::FaceIndexToOffset(k);

						int xi = RegionCellIndex.X + Offset.X;
						int yi = RegionCellIndex.Y + Offset.Y;
						int zi = RegionCellIndex.Z + Offset.Z;
						int NbrWorldCellX = (xi - BlockInfo.BlockMinIndex.X) + BlockInfo.WorldCellIndexRange.Min.X;
						int NbrWorldCellY = (yi - BlockInfo.BlockMinIndex.Y) + BlockInfo.WorldCellIndexRange.Min.Y;
						int NbrWorldCellZ = (zi - BlockInfo.BlockMinIndex.Z) + BlockInfo.WorldCellIndexRange.Min.Z;
						int NbrColumnMaxZ = ComputeZFunc(NbrWorldCellX, NbrWorldCellY);
						bool bNeighbourOccupied = NbrWorldCellZ <= NbrColumnMaxZ;

						if (bNeighbourOccupied) {
							Neighbours[k] = true;
							OccupiedCount++;
						}
					}
					if (OccupiedCount == 1 && Neighbours[5] == true)
					{
						GridCell.CellType = EModelGridCellType::Pyramid_Parametric;
						MGCell_Pyramid PyramidCell = MGCell_Pyramid::GetDefaultCellParams();
						PyramidCell.Params.DimensionZ = MGCell_Pyramid::MaxDimension / 2;
						UpdateGridCellParamsFromSubCell(GridCell, PyramidCell);
						BlockEditor.SetCellData(GridCell);
						bModifiedAnyCell = true;
					}
					else if (OccupiedCount == 4 && Neighbours[5] == true && Neighbours[4] == false)
					{
						GridCell.CellType = EModelGridCellType::Ramp_Parametric;
						MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
						int SideAxis = 0;
						for (int j = 0; j < 4; ++j) if (Neighbours[j] == false) SideAxis = j;
						uint8_t Axis = 0, Angle = 0;
						MGCell_Ramp::DetermineOrientationFromAxes(Vector3d::UnitZ(), GS::FaceIndexToNormal<double>(SideAxis), Axis, Angle);
						RampCell.Params.AxisDirection = Axis;
						RampCell.Params.AxisRotation = Angle;
						UpdateGridCellParamsFromSubCell(GridCell, RampCell);
						BlockEditor.SetCellData(GridCell);
						bModifiedAnyCell = true;
					}
					else if (OccupiedCount == 3 && Neighbours[5] == true && Neighbours[4] == false)
					{
						int a, b;
						if (find_adjacent_neighbours(Neighbours, a, b))
						{
							GridCell.CellType = EModelGridCellType::Corner_Parametric;
							MGCell_Corner CornerCell = MGCell_Corner::GetDefaultCellParams();
							uint8_t Axis = 0, Angle = 0;
							Vector3d Diagonal = Normalized(GS::FaceIndexToNormal<double>(a) + GS::FaceIndexToNormal<double>(b) + Vector3d::UnitZ());
							MGCell_Corner::DetermineOrientationFromDiagonal(Diagonal, Axis, Angle);
							CornerCell.Params.AxisDirection = Axis;
							CornerCell.Params.AxisRotation = Angle;
							UpdateGridCellParamsFromSubCell(GridCell, CornerCell);
							BlockEditor.SetCellData(GridCell);
							bModifiedAnyCell = true;
						}
					}
				}
			});
#endif

#if 0
			GS::EnumerateCellsInRangeInclusive(IndexRange.Min, IndexRange.Max,
				[&](Vector3i RegionCellIndex)
			{
				BlockEditor.SetCurrentCell(RegionCellIndex);
				ModelGridCell GridCell = BlockEditor.GetCellData();
				if (GridCell.CellType == EModelGridCellType::Filled)
				{
					bool Neighbours[6] = { false,false,false,false,false,false };
					int OccupiedCount = 0;
					for (int k = 0; k < 6; ++k)
					{
						ModelGridCell NbrCell;
						Vector3i Offset = GS::FaceIndexToOffset(k);

						int xi = RegionCellIndex.X + Offset.X;
						int yi = RegionCellIndex.Y + Offset.Y;
						int zi = RegionCellIndex.Z + Offset.Z;
						int NbrWorldCellX = (xi - BlockInfo.BlockMinIndex.X) + BlockInfo.WorldCellIndexRange.Min.X;
						int NbrWorldCellY = (yi - BlockInfo.BlockMinIndex.Y) + BlockInfo.WorldCellIndexRange.Min.Y;
						int NbrWorldCellZ = (zi - BlockInfo.BlockMinIndex.Z) + BlockInfo.WorldCellIndexRange.Min.Z;
						int NbrColumnMaxZ = ComputeZFunc(NbrWorldCellX, NbrWorldCellY);
						bool bNeighbourOccupied = NbrWorldCellZ <= NbrColumnMaxZ;

						if (bNeighbourOccupied) {
							Neighbours[k] = true;
							OccupiedCount++;
						}
					}
					if (OccupiedCount == 3 && Neighbours[5] == true && Neighbours[4] == false)
					{
						GridCell.CellType = EModelGridCellType::Ramp_Parametric;
						MGCell_Ramp RampCell = MGCell_Ramp::GetDefaultCellParams();
						bool bUpdated = false;
						for (int j = 0; j < 4 && bUpdated == false; ++j)
						{
							if (Neighbours[j] == false) continue;
							ModelGridCell SolidNbrCell;
							BlockEditor.GetCurrentCellNeighbourInBlock(GS::FaceIndexToOffset(j), SolidNbrCell);
							if (SolidNbrCell.CellType == EModelGridCellType::Ramp_Parametric)
							{
								MGCell_Ramp NbrRampCell;
								InitializeSubCellFromGridCell(SolidNbrCell, NbrRampCell);
								GridCell.CellType = EModelGridCellType::Ramp_Parametric;
								UpdateGridCellParamsFromSubCell(GridCell, NbrRampCell);
								BlockEditor.SetCellData(GridCell);
								bModifiedAnyCell = true; bUpdated = true;
							}
						}
					}
				}
			});
#endif

		}


		if (bModifiedAnyCell)
		{
			ModifiedLock.lock();
			ModifiedModelBlocksOut.push_back(RegionHandle.BlockIndex);
			ModifiedLock.unlock();
		}
		else
		{
			// TODO: we should deallocate cell and mark it as empty in empty flags...
		}

	});
}
