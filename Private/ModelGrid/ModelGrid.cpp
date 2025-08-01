// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGrid.h"
#include "GenericGrid/BoxIndexing.h"
#include "Core/gs_debug.h"
#include "Intersection/GSRayBoxIntersection.h"

using namespace GS;
using namespace GS::ModelGridInternal;

ModelGrid::ModelGrid()
{
	PackedColor4b DefaultColor(255, 255, 255, 255);
	for (int k = 0; k < 8; ++k)
		DefaultMaterials.FaceMaterials[k] = DefaultColor.PackedFields;
}

void ModelGrid::Initialize(Vector3d CellDimensionsIn)
{
	CellDimensions = CellDimensionsIn;

	IndexGrid.Initialize(UNALLOCATED);
	Vector3i MaxWorldDimensions = ModelGrid::ModelGridDimensions(); // IndexGrid.Dimensions()* Block_CellType::TypeDimensions();
	MinCoordCorner = -MaxWorldDimensions/2;

	// The index of cells is in the bottom-left, so the (0,0,0) cell is part of the 'positive extents',
	// and that means (eg) for a 4x4x4 grid, the valid integer X coordinates are [-2,-1,0,1]. So we have
	// to subtract one from the dimensions here.
	CellIndexBounds = AxisBox3i(MinCoordCorner, MinCoordCorner + MaxWorldDimensions-Vector3i::One());
		
	AllocatedChunkBounds = AxisBox3i::Empty();
	ModifiedKeyBounds = AxisBox3i::Empty();
}

void ModelGrid::SetNewCellDimensions(Vector3d CellDimensionsIn)
{
	CellDimensions = CellDimensionsIn;
}

void ModelGrid::RebuildAfterRestore()
{
	// Currently this is called after AllocatedBlocks and all POD members have been restored.
	// So all we have to do is rebuild the IndexGrid

	IndexGrid.Initialize(UNALLOCATED);
	int NumBlocks = (int)AllocatedBlocks.size();
	for (int k = 0; k < NumBlocks; ++k )
	{
		BlockContainer& BlockInfo = AllocatedBlocks[k];
		IndexGrid.Set(BlockInfo.BlockIndex, (uint16_t)k);
	}
}


void ModelGrid::InitBlockData(BlockData& NewBlockData)
{
	NewBlockData.CellType.Initialize( (uint16_t)EmptyCell.CellType );
	NewBlockData.CellData.Initialize(EmptyCell.CellData);

	PackedMaterialInfoV1 PackedInfo(EmptyCell.CellMaterial.AsColor4b());
	NewBlockData.Material.Initialize(PackedInfo.Data);

	NewBlockData.BlockFaceMaterials.resize(0);
}

void ModelGrid::CopyBlockData(BlockData& ToBlockData, const BlockData& FromBlockData)
{
	ToBlockData.CellType = FromBlockData.CellType;
	ToBlockData.CellData = FromBlockData.CellData;
	ToBlockData.Material = FromBlockData.Material;
	ToBlockData.BlockFaceMaterials = FromBlockData.BlockFaceMaterials;
}


ModelGrid& ModelGrid::operator=(const ModelGrid& copy)
{
	CellDimensions = copy.CellDimensions;
	EmptyCell = copy.EmptyCell;
	CellIndexBounds = copy.CellIndexBounds;

	IndexGrid = copy.IndexGrid;
	MinCoordCorner = copy.MinCoordCorner;
	AllocatedChunkBounds = copy.AllocatedChunkBounds;
	ModifiedKeyBounds = copy.ModifiedKeyBounds;

	int N = (int)copy.AllocatedBlocks.size();
	AllocatedBlocks.resize(N);
	for (int k = 0; k < N; ++k)
	{
		if (copy.AllocatedBlocks[k].Data == nullptr)
		{
			AllocatedBlocks[k].Data = nullptr;
		}
		else
		{
			BlockContainer NewChunk;
			NewChunk.BlockIndex = copy.AllocatedBlocks[k].BlockIndex;
			NewChunk.Data =  new BlockData();
			InitBlockData(*NewChunk.Data);
			CopyBlockData(*NewChunk.Data, *copy.AllocatedBlocks[k].Data);
			AllocatedBlocks[k] = NewChunk;
		}
	}
	return *this;
}

ModelGrid::ModelGrid(const ModelGrid& Other)
{
	*this = Other;
}

ModelGrid::~ModelGrid()
{
	for (int k = 0; k < AllocatedBlocks.size(); ++k)
	{
		delete AllocatedBlocks[k].Data;
		AllocatedBlocks[k].Data = nullptr;
	}
	AllocatedBlocks.clear(true);
}




ModelGridCell ModelGrid::UnpackToCell(const BlockData& BlockData, uint64_t LinearIndex) const
{
	return ModelGridInternal::UnpackCellFromPackedDataV1(
		BlockData.CellType[LinearIndex],
		BlockData.CellData[LinearIndex],
		BlockData.Material[LinearIndex],
		BlockData.BlockFaceMaterials.get_view(), ModelGridVersions::CurrentVersionNumber);
}
ModelGridCell ModelGrid::UnpackToCell(const BlockData& BlockData, Vector3i LocalIndex) const
{
	int64_t LinearIndex = BlockData.CellType.ToLinearIndex(LocalIndex);
	return UnpackToCell(BlockData, LinearIndex);
}



ModelGrid::BlockData* ModelGrid::GetOrAllocateChunk(Vector3i BlockIndex)
{
	uint16_t StorageIndex = IndexGrid[BlockIndex];
	if (StorageIndex != UNALLOCATED)
	{
		return AllocatedBlocks[StorageIndex].Data;
	}

	uint16_t NewStorageIndex = (uint16_t)AllocatedBlocks.size();
	AllocatedBlocks.resize(NewStorageIndex + 1, false);

	BlockContainer NewChunk;
	NewChunk.BlockIndex = BlockIndex;
	NewChunk.Data = new BlockData();
	InitBlockData(*NewChunk.Data);
	AllocatedBlocks.set_move(NewStorageIndex, std::move(NewChunk));

	IndexGrid.Set(BlockIndex, NewStorageIndex);
	AllocatedChunkBounds.Contain(BlockIndex);

	return AllocatedBlocks[NewStorageIndex].Data;
}

ModelGrid::EditableCellRef ModelGrid::GetEditableCellRef(CellKey Key)
{
	Vector3i ShiftKey = Key - MinCoordCorner;
	Vector3i BlockIndex = ShiftKey / Block_CellType::TypeDimensions();
	Vector3i LocalIndex = ShiftKey - BlockIndex * Block_CellType::TypeDimensions();

	// assume this cell is edited now
	ModifiedKeyBounds.Contain(Key);

	BlockData* Data = GetOrAllocateChunk(BlockIndex);
	return EditableCellRef{ BlockIndex, Data, LocalIndex };

	//uint16_t StorageIndex = IndexGrid[BlockIndex];
	//if (StorageIndex != UNALLOCATED)
	//{
	//	return EditableCellRef{ BlockIndex, AllocatedBlocks[StorageIndex].Data, LocalIndex };
	//}

	//uint16_t NewStorageIndex = AllocatedBlocks.size();
	//AllocatedBlocks.resize(NewStorageIndex + 1, false);

	//BlockContainer NewChunk;
	//NewChunk.BlockIndex = BlockIndex;
	//NewChunk.Data = new BlockData();
	//InitBlockData(*NewChunk.Data);
	//AllocatedBlocks.set_move(NewStorageIndex, std::move(NewChunk));

	//IndexGrid.Set(BlockIndex, NewStorageIndex);
	//AllocatedChunkBounds.Contain(BlockIndex);

	//return EditableCellRef{ BlockIndex, AllocatedBlocks[NewStorageIndex].Data, LocalIndex };
}



AxisBox3i ModelGrid::GetModifiedRegionBounds(int PadExtent) const
{
	if (ModifiedKeyBounds.IsValid() == false)
	{
		return (PadExtent == 0) ? ModifiedKeyBounds : AxisBox3i( Vector3i(-PadExtent), Vector3i(PadExtent) );
	}
	AxisBox3i Result = ModifiedKeyBounds;
	Result.Min -= Vector3i(PadExtent);
	Result.Max += Vector3i(PadExtent);
	return Result;
}

AxisBox3i ModelGrid::GetOccupiedRegionBounds(int PadExtent) const
{
	if (ModifiedKeyBounds.IsValid() == false || ModifiedKeyBounds.IsValid() == false)
	{
		return (PadExtent == 0) ? ModifiedKeyBounds : AxisBox3i(Vector3i(-PadExtent), Vector3i(PadExtent));
	}

	AxisBox3i Result = AxisBox3i::Empty();

	Vector3i MinIndex = ModifiedKeyBounds.Min;
	Vector3i MaxIndex = ModifiedKeyBounds.Max;
	for (int zi = MinIndex.Z; zi <= MaxIndex.Z; ++zi) {
		for (int yi = MinIndex.Y; yi <= MaxIndex.Y; ++yi) {
			for (int xi = MinIndex.X; xi <= MaxIndex.X; ++xi) {
				Vector3i CellIndex(xi, yi, zi);
				if (!IsCellEmpty(CellIndex))
					Result.Contain(CellIndex);
			}
		}
	}

	if (Result.IsValid() == false)
		return (PadExtent == 0) ? ModifiedKeyBounds : AxisBox3i(Vector3i(-PadExtent), Vector3i(PadExtent));

	Result.Min -= Vector3i(PadExtent);
	Result.Max += Vector3i(PadExtent);
	return Result;
}


EModelGridCellType ModelGrid::GetCellType(CellKey CellIndex, bool& bIsInGrid) const
{
	bIsInGrid = CellIndexBounds.Contains(CellIndex);
	if (!bIsInGrid)
		return EModelGridCellType::Empty;
	return GetCellTypeInternal(CellIndex);
}


ModelGridCell ModelGrid::GetCellInfo(CellKey Key, bool& bIsInGrid) const
{
	Vector3i CellIndex(Key);
	bIsInGrid = CellIndexBounds.Contains(CellIndex);
	if (!bIsInGrid) 
		return EmptyCell;

	return GetCellInternal(Key);
}
bool ModelGrid::GetCellInfoIfValid(CellKey Key, ModelGridCell& CellInOut) const
{
	Vector3i CellIndex(Key);
	if (!CellIndexBounds.Contains(CellIndex)) 
		return false;
	CellInOut = GetCellInternal(Key);
	return true;
}

bool ModelGrid::IsValidCell(CellKey Key) const
{
	Vector3i CellIndex(Key);
	return CellIndexBounds.Contains(CellIndex);
}

bool ModelGrid::IsCellSolid(CellKey Key) const
{
	Vector3i CellIndex(Key);
	if (CellIndexBounds.Contains(CellIndex))
	{
		return GetCellTypeInternal(Key) == EModelGridCellType::Filled;
	}
	return false;
}

bool ModelGrid::IsCellEmpty(CellKey Key) const
{
	Vector3i CellIndex(Key);
	if (CellIndexBounds.Contains(CellIndex))
	{
		return GetCellTypeInternal(Key) == EModelGridCellType::Empty;
	}
	return false;
}

AxisBox3d ModelGrid::GetCellLocalBounds(CellKey Key) const
{
	Vector3i CellIndex(Key);
	Vector3d MinCorner(CellIndex.X, CellIndex.Y, CellIndex.Z);
	MinCorner *= CellDimensions;
	return AxisBox3d(MinCorner, MinCorner + CellDimensions);
}

Frame3d ModelGrid::GetCellFrame(CellKey Key) const
{
	Vector3i CellIndex(Key);
	Vector3d MinCorner(CellIndex.X, CellIndex.Y, CellIndex.Z);
	MinCorner *= CellDimensions;
	return Frame3d(MinCorner);
}


Frame3d ModelGrid::GetCellFrameWorld(CellKey Key, const Frame3d& WorldFrame) const
{
	Vector3i CellIndex(Key);
	Vector3d MinCorner(CellIndex.X, CellIndex.Y, CellIndex.Z);
	MinCorner *= CellDimensions;
	Frame3d CellFrame = WorldFrame;
	CellFrame.Origin = WorldFrame.ToWorldPoint(MinCorner);
	return CellFrame;
}


void ModelGrid::ReinitializeCell_Internal(BlockData& GridBlockData, int64_t LinearIndex, const ModelGridCell& CopyFromCell, ModelGridCell* PrevCell)
{
	if (PrevCell != nullptr)
	{
		*PrevCell = UnpackToCell(GridBlockData, LinearIndex);
	}
	GridBlockData.CellType.Set(LinearIndex, (uint16_t)CopyFromCell.CellType);
	GridBlockData.CellData.Set(LinearIndex, CopyFromCell.CellData);

	PackedMaterialInfoV1 CurMatInfo(GridBlockData.Material[LinearIndex] );
	PackedMaterialInfoV1 NewMatInfo = CurMatInfo;

	bool bCurIsPerFaceType = ((int)CurMatInfo.MaterialType >= (int)EGridCellMaterialType::BeginPerFaceTypes);

	// if we are switching from a per-face material to a solid cell material, we 
	// need to deallocate the extended per-face material data
	bool bNewIsPerFaceType = ((int)CopyFromCell.MaterialType >= (int)EGridCellMaterialType::BeginPerFaceTypes);
	if (bNewIsPerFaceType == false && bCurIsPerFaceType == true && CurMatInfo.ExtendedIndex < GridBlockData.BlockFaceMaterials.size() )
	{
		int64_t SwappedIndex = -1;
		bool bRemoved = GridBlockData.BlockFaceMaterials.swap_remove(CurMatInfo.ExtendedIndex, SwappedIndex);
		gs_debug_assert(bRemoved);
		if (SwappedIndex >= 0)
		{
			PackedFaceMaterialsV1& SwappedMaterials = GridBlockData.BlockFaceMaterials[CurMatInfo.ExtendedIndex];
			uint16_t SwappedLinearIndex = SwappedMaterials.ParentCellIndex;
			PackedMaterialInfoV1 FixUpMatInfo(GridBlockData.Material[SwappedLinearIndex]);
			FixUpMatInfo.ExtendedIndex = CurMatInfo.ExtendedIndex;
			GridBlockData.Material.Set(SwappedLinearIndex, FixUpMatInfo.Data);
		}
		NewMatInfo.ExtendedIndex = 0xFFFF;
	}

	if (CopyFromCell.MaterialType == EGridCellMaterialType::FaceColors)
	{
		NewMatInfo.MaterialType = (uint8_t)EGridCellMaterialType::FaceColors;
		if (bCurIsPerFaceType == false)
		{
			PackedFaceMaterialsV1 NewMaterials;
			gs_debug_assert(LinearIndex < 0xFFFF);
			NewMaterials.ParentCellIndex = (uint16_t)LinearIndex;		//...?
			for (int j = 0; j < 8; ++j)
				NewMaterials[j] = GridMaterial(CurMatInfo.CellColor3b());		// should this be CellColor4b here??
			int64_t NewIndex = GridBlockData.BlockFaceMaterials.add_move(std::move(NewMaterials));
			gs_debug_assert(NewIndex < 0xFFFF);
			NewMatInfo.ExtendedIndex = (uint16_t)NewIndex;
		}
		else
			gs_debug_assert(CurMatInfo.ExtendedIndex < GridBlockData.BlockFaceMaterials.size());		// this should only ever be the case...

		PackedFaceMaterialsV1& PackedMaterials = GridBlockData.BlockFaceMaterials[NewMatInfo.ExtendedIndex];
		for (int j = 0; j < 8; ++j) {
			PackedMaterials.FaceMaterials[j] = CopyFromCell.FaceMaterials[j].PackedValue();
		}

	}
	else if (CopyFromCell.MaterialType == EGridCellMaterialType::SolidRGBIndex)
	{
		NewMatInfo.SetFromRGBIndex(CopyFromCell.CellMaterial);
	}
	else // nothing else supported yet...
	{
		NewMatInfo.SetFromRGBA(CopyFromCell.CellMaterial);
	}

	GridBlockData.Material.Set(LinearIndex, NewMatInfo.Data);
}


bool ModelGrid::ReinitializeCell(CellKey Cell, const ModelGridCell& CopyFromCell, ModelGridCell* PrevCell)
{
	Vector3i CellIndex(Cell);
	if (CellIndexBounds.Contains(CellIndex) == false) return false;

	EditableCellRef CellRef = GetEditableCellRef(Cell);		// GetEditableCellRef updates ModifiedKeyBounds!
	int64_t LinearIndex = CellRef.Grid->CellType.ToLinearIndex(CellRef.LocalIndex);
	BlockData* GridBlockData = CellRef.Grid;

	ReinitializeCell_Internal(*CellRef.Grid, LinearIndex, CopyFromCell, PrevCell);
	return true;
}


void ModelGrid::EnumerateAdjacentConnectedChunks(CellKey Cell, FunctionRef<void(Vector3i, CellKey)> ProcessFunc) const
{
	Vector3i CellChunkIndex = GetChunkIndexForKey(Cell);
	for (uint32_t k = 0; k < 6; ++k)
	{
		Vector3i Offset = GS::FaceIndexToOffset(k);
		CellKey NeighbourCell = Cell + Offset;
		if (CellIndexBounds.Contains(NeighbourCell) )
		{
			Vector3i NeighbourChunkIdx = GetChunkIndexForKey(NeighbourCell);
			if (NeighbourChunkIdx != CellChunkIndex)
				ProcessFunc(NeighbourChunkIdx, NeighbourCell);
		}
	}
}

void ModelGrid::EnumerateOccupiedColumnBlocks(const Vector2i& ColumnIndex,
	FunctionRef<void(Vector3i)> BlockFunc) const
{
	std::scoped_lock lock(const_cast<ModelGrid*>(this)->BlockDataLock);

	Vector3i BlockGridDims = BlockIndexGrid::TypeDimensions();
	if (ColumnIndex.X >= 0 && ColumnIndex.X < BlockGridDims.X && ColumnIndex.Y >= 0 && ColumnIndex.Y < BlockGridDims.Y)
	{
		for (int zi = 0; zi < BlockGridDims.Z; ++zi)
		{
			uint16_t BlockIndex = IndexGrid.Get(ColumnIndex.X, ColumnIndex.Y, zi);
			if (BlockIndex != UNALLOCATED)
			{
				// want to know if this is ever not the case...
				gs_debug_assert(AllocatedBlocks[BlockIndex].Data != nullptr);
				BlockFunc(Vector3i(ColumnIndex.X, ColumnIndex.Y, zi));
			}
		}

	}
}



AxisBox3i ModelGrid::GetAllocatedChunkRangeBounds(const AxisBox3d& LocalBounds) const
{
	if (ModifiedKeyBounds.IsValid() == false) return ModifiedKeyBounds;

	bool bIsInGrid = false;
	CellKey MinKey = GetCellAtPosition(LocalBounds.Min, bIsInGrid);
	MinKey.X = GS::Max(MinKey.X, ModifiedKeyBounds.Min.X);
	MinKey.Y = GS::Max(MinKey.Y, ModifiedKeyBounds.Min.Y);
	MinKey.Z = GS::Max(MinKey.Z, ModifiedKeyBounds.Min.Z);
	CellKey MaxKey = GetCellAtPosition(LocalBounds.Max, bIsInGrid);
	MaxKey.X = GS::Min(MaxKey.X, ModifiedKeyBounds.Max.X);
	MaxKey.Y = GS::Min(MaxKey.Y, ModifiedKeyBounds.Max.Y);
	MaxKey.Z = GS::Min(MaxKey.Z, ModifiedKeyBounds.Max.Z);
	Vector3i MinChunkIdx = GetChunkIndexForKey(MinKey);
	Vector3i MaxChunkIdx = GetChunkIndexForKey(MaxKey);
	return AxisBox3i(MinChunkIdx, MaxChunkIdx);
}


int ModelGrid::GetNumAllocatedBlocks() const
{
	return (int)AllocatedBlocks.size();
}
void ModelGrid::EnumerateAllocatedBlocks(FunctionRef<void(Vector3i)> BlockItemFunc) const
{
	int N = (int)AllocatedBlocks.size();
	for (int k = 0; k < N; ++k) {
		if (AllocatedBlocks[k].Data != nullptr)
			BlockItemFunc(AllocatedBlocks[k].BlockIndex);
	}
}


AxisBox3d ModelGrid::GetChunkBounds(const Vector3i& BlockIndex) const
{
	AxisBox3i ChunkCells = GetKeyRangeForChunk(BlockIndex);
	Vector3d MinCorner = (Vector3d)ChunkCells.Min * CellDimensions;
	Vector3d MaxCorner = (Vector3d)ChunkCells.Max * CellDimensions;
	return AxisBox3d(MinCorner, MaxCorner + CellDimensions);
}



void ModelGrid::EnumerateFilledCells(
	FunctionRef<void(CellKey Key, EModelGridCellType CellType)> EnumerateFunc) const
{
	if (AllocatedChunkBounds.IsValid() == false) return;

	Vector3i MinIndex = ModifiedKeyBounds.Min;
	Vector3i MaxIndex = ModifiedKeyBounds.Max;
	for (int zi = MinIndex.Z; zi <= MaxIndex.Z; ++zi) {
		for (int yi = MinIndex.Y; yi <= MaxIndex.Y; ++yi) {
			for (int xi = MinIndex.X; xi <= MaxIndex.X; ++xi) {

				Vector3i CellIndex(xi, yi, zi);
				EModelGridCellType CellType = GetCellTypeInternal(CellIndex);
				if (CellType != EModelGridCellType::Empty)
				{
					EnumerateFunc(CellIndex, CellType);
				}
			}
		}
	}

}


void ModelGrid::EnumerateFilledCells(
	FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo, AxisBox3d LocalBounds)> ApplyFunc)
{
	if (AllocatedChunkBounds.IsValid() == false) return;

	Vector3i MinIndex = ModifiedKeyBounds.Min;
	Vector3i MaxIndex = ModifiedKeyBounds.Max;

	for (int zi = MinIndex.Z; zi <= MaxIndex.Z; ++zi) {
		for (int yi = MinIndex.Y; yi <= MaxIndex.Y; ++yi) {
			for (int xi = MinIndex.X; xi <= MaxIndex.X; ++xi) {

				Vector3i CellIndex(xi, yi, zi);
				ModelGridCell CellInfo = GetCellInternal(CellIndex); //  CellStorage.Get(xi, yi, zi);
				if (CellInfo.CellType != EModelGridCellType::Empty)
				{
					Vector3d MinCorner(
						CellIndex.X * CellDimensions.X, CellIndex.Y * CellDimensions.Y, CellIndex.Z * CellDimensions.Z);
					AxisBox3d LocalBounds(MinCorner, MinCorner + CellDimensions);
					ApplyFunc( CellKey(CellIndex), CellInfo, LocalBounds );
				}
			}
		}
	}
}


void ModelGrid::EnumerateFilledChunkCells(
	const Vector3i& BlockIndex,
	FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo, const AxisBox3d& LocalBounds)> ApplyFunc) const
{
	const BlockData* Data = GetAllocatedChunk(BlockIndex);
	if (!Data) return;

	Data->CellType.EnumerateAllCells([&](size_t LinearIndex, uint16_t CellTypeInt)
	{
		EModelGridCellType CellType = (EModelGridCellType)CellTypeInt;
		if (CellType != EModelGridCellType::Empty)
		{
			Vector3i LocalIndex = Data->CellType.ToVectorIndex(LinearIndex);
			CellKey Key = ToKey(BlockIndex, LocalIndex);
			Vector3d MinCorner = (Vector3d)Key * CellDimensions;
			ModelGridCell Cell = UnpackToCell(*Data, LinearIndex);
			ApplyFunc(Key, Cell, AxisBox3d(MinCorner, MinCorner + CellDimensions));
		}
	});
}




const Vector3i GridNeighbours6[6] =
{
	Vector3i{0,0,-1}, Vector3i{0,0,1},
	Vector3i{-1,0,0}, Vector3i{1,0,0},
	Vector3i{0,-1,0}, Vector3i{0,1,0}
};

const int GridNeighbours6_AxisIndex[6] = {2, 2, 0, 0, 1, 1};


void ModelGrid::EnumerateConnectedCells(
	CellKey InitialCellKey,
	FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo)> ApplyFunc,
	ModelGrid::EnumerateCellsCache* Cache)
{
	ModelGrid::EnumerateCellsCache LocalCache;
	ModelGrid::EnumerateCellsCache& UseCache = (Cache != nullptr) ? *Cache : LocalCache;

	UseCache.Reset();
	UseCache.AddToQueue(InitialCellKey);
	UseCache.AddProcessed(InitialCellKey);

	while (UseCache.ItemsRemaining())
	{
		CellKey CurCellKey = UseCache.RemoveNextFromQueue();
		for (int j = 0; j < 6; ++j)
		{
			Vector3i Offset = GridNeighbours6[j];
			CellKey NeighbourKey = CellKey((Vector3i)CurCellKey + Offset);
			if ( UseCache.HasBeenProcessed(NeighbourKey) == false)
			{
				Vector3i NeighbourCellIdx = (Vector3i)NeighbourKey;
				if (CellIndexBounds.Contains(NeighbourCellIdx))
				{
					ModelGridCell CellInfo = GetCellInternal(NeighbourCellIdx);
					if (CellInfo.CellType != EModelGridCellType::Empty)
					{
						ApplyFunc(NeighbourKey, CellInfo);
						UseCache.AddProcessed(NeighbourKey);
						UseCache.AddToQueue(NeighbourKey);
					}
				}
			}
		}
	}

	if(Cache) Cache->Reset();
}




const Vector3i GridNeighboursByAxis[3][4] =
{
	{ Vector3i{0,-1,0}, Vector3i{0,1,0}, Vector3i{0,0,-1}, Vector3i{0,0,1} },
	{ Vector3i{-1,0,0}, Vector3i{1,0,0}, Vector3i{0,0,-1}, Vector3i{0,0,1} },
	{ Vector3i{-1,0,0}, Vector3i{1,0,0}, Vector3i{0,-1,0}, Vector3i{0,1,0} },
};


void ModelGrid::EnumerateConnectedPlanarCells(
	CellKey InitialCellKey,
	int PlaneAxis,
	FunctionRef<bool(CellKey FromKey, CellKey ToKey)> ConnectedFilterFunc,
	FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo)> ApplyFunc,
	bool bSkipEmpty,
	ModelGrid::EnumerateCellsCache* Cache)
{
	ModelGrid::EnumerateCellsCache LocalCache;
	ModelGrid::EnumerateCellsCache& UseCache = (Cache != nullptr) ? *Cache : LocalCache;

	UseCache.Reset();
	UseCache.AddToQueue(InitialCellKey);
	UseCache.AddProcessed(InitialCellKey);

	while (UseCache.ItemsRemaining())
	{
		CellKey CurCellKey = UseCache.RemoveNextFromQueue();
		for (int j = 0; j < 4; ++j)
		{
			Vector3i Offset = GridNeighboursByAxis[PlaneAxis][j];
			CellKey NeighbourKey = CellKey( (Vector3i)CurCellKey + Offset );
			if (UseCache.HasBeenProcessed(NeighbourKey) == false && ConnectedFilterFunc(CurCellKey, NeighbourKey) == true)
			{
				Vector3i NeighbourCellIdx = (Vector3i)NeighbourKey;
				if (CellIndexBounds.Contains(NeighbourCellIdx))
				{
					ModelGridCell CellInfo = GetCellInternal(NeighbourCellIdx);
					if (bSkipEmpty == false || CellInfo.CellType != EModelGridCellType::Empty)
					{
						ApplyFunc(NeighbourKey, CellInfo);
						UseCache.AddProcessed(NeighbourKey);
						UseCache.AddToQueue(NeighbourKey);
					}
				}
			}
		}
	}

	if (Cache) Cache->Reset();
}


void ModelGrid::EnumerateAdjacentCells(
	CellKey InitialCellKey,
	Vector3i HalfExtents,
	bool bSkipEmpty,
	FunctionRef<void(CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo)> ApplyFunc)
{
	for (int dz = -HalfExtents.Z; dz <= HalfExtents.Z; ++dz)
	{
		for (int dy = -HalfExtents.Y; dy <= HalfExtents.Y; ++dy)
		{
			for (int dx = -HalfExtents.X; dx <= HalfExtents.X; ++dx)
			{
				Vector3i Offset(dx, dy, dz);
				CellKey NeighbourKey = CellKey((Vector3i)InitialCellKey + Offset);
				Vector3i NeighbourCellIdx = (Vector3i)NeighbourKey;
				if (CellIndexBounds.Contains(NeighbourCellIdx))
				{
					ModelGridCell CellInfo = GetCellInternal(NeighbourCellIdx);
					if (bSkipEmpty == false || CellInfo.CellType != EModelGridCellType::Empty)
					{
						ApplyFunc(NeighbourKey, Offset, CellInfo);
					}
				}
			}
		}
	}
}







Vector3d ModelGrid::GetTransformedCellDimensions(uint8_t AxisDirection, uint8_t AxisRotation) const
{
	Quaterniond TargetOrientation = MakeCubeOrientation(CubeOrientation(AxisDirection, AxisRotation));
	Vector3d RotatedDimensions = TargetOrientation.InverseMultiply(CellDimensions).Abs();
	return RotatedDimensions;
}



bool ModelGrid::GetCellOrientationTransform(CellKey Key, TransformListd& TransformSeq, bool bIgnoreSubCellDimensions) const
{
	TransformSeq.Reset();

	bool bIsInGrid = false;
	ModelGridCell CellInfo = GetCellInfo(Key, bIsInGrid);
	if (!bIsInGrid) return false;

	GS::GetUnitCellTransform(CellInfo, CellDimensions, TransformSeq, bIgnoreSubCellDimensions);
	return true;
}


bool ModelGrid::ComputeCellBoxIntersection(const Ray3d& Ray, const Vector3i& CellKey, double& RayParameterOut, Vector3d& HitPositionOut, Vector3d& CellFaceNormalOut) const
{
	GS::AxisBox3d CellBounds = GetCellLocalBounds(CellKey);
	return GS::ComputeRayBoxIntersection(Ray, CellBounds, RayParameterOut, HitPositionOut, CellFaceNormalOut);
}



//std::unique_ptr<ModelGrid::FUndoRedoData> ModelGrid::MakeCheckpoint() const
//{
//	std::unique_ptr<FUndoRedoData> Data = std::make_unique<FUndoRedoData>();
//	//Data->CellDimensions = this->CellDimensions;
//	//Data->CellIndexBounds = this->CellIndexBounds;
//	//Data->CellStorage = this->CellStorage;
//	return Data;
//}
//
//void ModelGrid::RestoreCheckpoint(const FUndoRedoData& Data)
//{
//	// todo: if dimensions did not change, can copy into existing CellStorage memory
//	//this->CellDimensions = Data.CellDimensions;
//	//this->CellIndexBounds = Data.CellIndexBounds;
//	//this->CellStorage = Data.CellStorage;
//}



void ModelGrid::EnumerateCellsCache::AddToQueue(CellKey Key)
{
	Stack.push_back(Key);
}
ModelGrid::CellKey ModelGrid::EnumerateCellsCache::RemoveNextFromQueue()
{
	ModelGrid::CellKey Next = Stack.back();
	Stack.pop_back();
	return Next;
}

void ModelGrid::EnumerateCellsCache::AddProcessed(CellKey Key)
{
	if (bUsingMap)
	{
		ProcessedMap.insert(Key);
	}
	else
	{
		if (ProcessedArray.size() < 100)
		{
			ProcessedArray.add(Key);
		}
		else
		{
			for (auto ProcessedKey : ProcessedArray)
				ProcessedMap.insert(ProcessedKey);
			ProcessedArray.clear();
			bUsingMap = true;
		}
	}
}









void ModelGrid::EnumerateBlockHandles(FunctionRef<void(GridRegionHandle RegionHandle)> ApplyFunc, bool bOnlyAllocated) const
{
	if (bOnlyAllocated)
	{
		int N = (int)AllocatedBlocks.size();
		for (int k = 0; k < N; ++k)
		{
			if (AllocatedBlocks[k].Data != nullptr)
			{
				GridRegionHandle Handle;
				Handle.BlockIndex = AllocatedBlocks[k].BlockIndex;
				Handle.CellIndexRange = GetKeyRangeForChunk(Handle.BlockIndex);
				Handle.GridHandle = (void*)this;
				Handle.BlockHandle = (void*)AllocatedBlocks[k].Data;
				Handle.MagicNumber = 0x7373;
				ApplyFunc(Handle);
			}
		}
	}
	else
	{
		int64_t MaxLinearIndex = (int64_t)IndexGrid.Size();
		for (int64_t LinearIndex = 0; LinearIndex < MaxLinearIndex; ++LinearIndex)
		{
			GridRegionHandle Handle;
			Handle.BlockIndex = IndexGrid.ToVectorIndex(LinearIndex);
			Handle.CellIndexRange = GetKeyRangeForChunk(Handle.BlockIndex);
			Handle.GridHandle = (void*)this;
			Handle.BlockHandle = (void*)GetAllocatedChunk(Handle.BlockIndex);
			Handle.MagicNumber = 0x7373;
			ApplyFunc(Handle);
		}
	}
}


GridRegionHandle ModelGrid::GetHandleForCell(Vector3i CellIndex, bool bWantDataReferences) const
{
	return GetHandleForBlock(GetChunkIndexForKey(CellIndex), bWantDataReferences);
}

GridRegionHandle ModelGrid::GetHandleForBlock(Vector3i BlockIndex, bool bWantDataReferences) const
{
	GridRegionHandle Handle;
	Handle.BlockIndex = BlockIndex;
	Handle.CellIndexRange = GetKeyRangeForChunk(Handle.BlockIndex);
	Handle.GridHandle = (void*)this;
	Handle.BlockHandle = (bWantDataReferences) ? (void*)GetAllocatedChunk(Handle.BlockIndex) : nullptr;
	Handle.MagicNumber = 0x7373;
	return Handle;
}

void ModelGrid::UnsafeRawBlockEditor::SetCurrentCell(Vector3i CellIndex)
{
	CurrentCellIndex = CellIndex;
	Vector3i ShiftKey = CellIndex - GridMinCoordCorner;
	Vector3i IndexIndex = ShiftKey / Block_CellType::TypeDimensions();
	CurrentLocalIndex = ShiftKey - IndexIndex * Block_CellType::TypeDimensions();
}

ModelGridCell ModelGrid::UnsafeRawBlockEditor::GetCellData()
{
	ModelGrid* Grid = (ModelGrid*)RegionHandle.GridHandle;
	ModelGrid::BlockData* Data = (ModelGrid::BlockData*)RegionHandle.BlockHandle;
	gs_debug_assert(Grid != nullptr && Data != nullptr);
	return Grid->UnpackToCell(*Data, CurrentLocalIndex);
}
void ModelGrid::UnsafeRawBlockEditor::SetCellData(const ModelGridCell& NewCell)
{
	ModelGrid* Grid = (ModelGrid*)RegionHandle.GridHandle;
	ModelGrid::BlockData* Data = (ModelGrid::BlockData*)RegionHandle.BlockHandle;
	gs_debug_assert(Grid != nullptr && Data != nullptr);
	int64_t LinearIndex = Data->CellType.ToLinearIndex(CurrentLocalIndex);
	Grid->ReinitializeCell_Internal(*Data, LinearIndex, NewCell, nullptr);

	ModifiedRegion.Contain(CurrentCellIndex);
}

bool ModelGrid::UnsafeRawBlockEditor::GetCurrentCellNeighbourInBlock(Vector3i NeighbourOffset, ModelGridCell& NeighbourCellData)
{
	ModelGrid* Grid = (ModelGrid*)RegionHandle.GridHandle;
	ModelGrid::BlockData* Data = (ModelGrid::BlockData*)RegionHandle.BlockHandle;
	gs_debug_assert(Grid != nullptr && Data != nullptr);

	Vector3i NeighbourCellIndex = CurrentLocalIndex + NeighbourOffset;
	if (!Data->CellType.IsValidIndex(NeighbourCellIndex)) return false;

	NeighbourCellData = Grid->UnpackToCell(*Data, NeighbourCellIndex);
	return true;
}

bool ModelGrid::UnsafeRawBlockEditor::IsNeighbourCellInBlock(Vector3i NeighbourOffset)
{
	ModelGrid* Grid = (ModelGrid*)RegionHandle.GridHandle;
	ModelGrid::BlockData* Data = (ModelGrid::BlockData*)RegionHandle.BlockHandle;
	gs_debug_assert(Grid != nullptr && Data != nullptr);
	Vector3i NeighbourCellIndex = CurrentLocalIndex + NeighbourOffset;
	return Data->CellType.IsValidIndex(NeighbourCellIndex);
}

bool ModelGrid::UnsafeRawBlockEditor::IsNeighbourCellOccupiedInBlock(Vector3i NeighbourOffset)
{
	ModelGrid* Grid = (ModelGrid*)RegionHandle.GridHandle;
	ModelGrid::BlockData* Data = (ModelGrid::BlockData*)RegionHandle.BlockHandle;
	gs_debug_assert(Grid != nullptr && Data != nullptr);

	Vector3i NeighbourCellIndex = CurrentLocalIndex + NeighbourOffset;
	if (Data->CellType.IsValidIndex(NeighbourCellIndex) && (EModelGridCellType)Data->CellType.Get(NeighbourCellIndex) != EModelGridCellType::Empty)
		return true;
	return false;
}


ModelGrid::UnsafeRawBlockEditor ModelGrid::GetRawBlockEditor_Safe(GridRegionHandle RegionHandle)
{
	gs_debug_assert(RegionHandle.MagicNumber == 0x7373);

	UnsafeRawBlockEditor Editor;
	Editor.RegionHandle = RegionHandle;

	if (RegionHandle.BlockHandle == nullptr)
	{
		std::scoped_lock lock(const_cast<ModelGrid*>(this)->BlockDataLock);
		BlockData* Data = GetOrAllocateChunk(RegionHandle.BlockIndex);
		gs_debug_assert(Data != nullptr);
		Editor.RegionHandle.BlockHandle = (void*)Data;
	}

	Editor.GridMinCoordCorner = MinCoordCorner;

	return Editor;
}


ModelGridCell ModelGrid::GetCellInfo_Safe(CellKey Key, bool& bIsInGrid) const
{
	Vector3i CellIndex(Key);
	bIsInGrid = CellIndexBounds.Contains(CellIndex);
	if (!bIsInGrid) return EmptyCell;

	std::scoped_lock lock(const_cast<ModelGrid*>(this)->BlockDataLock);

	Vector3i LocalIndex;
	const BlockData* Data = ToLocalIfAllocated(Key, LocalIndex);
	if (Data == nullptr) return EmptyCell;
	return UnpackToCell(*Data, LocalIndex);
}
