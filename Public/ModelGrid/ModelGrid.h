// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"

#include "Color/GSColor3b.h"
#include "Math/GSIntVector2.h"
#include "Math/GSIntVector3.h"
#include "Math/GSFrame3.h"
#include "Math/GSAxisBox3.h"
#include "Math/GSIntAxisBox3.h"
#include "ModelGrid/ModelGridCell.h"
#include "ModelGrid/ModelGridUtil.h"
#include "ModelGrid/ModelGridInternals.h"
#include "Grid/GSFixedGrid3.h"
#include "Core/unsafe_vector.h"
#include "Core/FunctionRef.h"

#include <mutex>
#include <vector>
#include <unordered_set>
#include <memory>

namespace GS
{

struct GRADIENTSPACEGRID_API GridCellFace
{
	Vector3i CellIndex;
	uint32_t FaceIndex;
};



class GRADIENTSPACEGRID_API ModelGrid
{
public:
	using CellKey = Vector3i;

	static constexpr int BlockSize_XY = 16;
	static constexpr int BlockSize_Z = 16;
	static constexpr int IndexSize_XY = 16;
	static constexpr int IndexSize_Z = 16;

	//static constexpr int BlockSize_XY = 32;
	//static constexpr int BlockSize_Z = 16;
	//static constexpr int IndexSize_XY = 32;
	//static constexpr int IndexSize_Z = 32;

	static constexpr Vector3i ModelGridDimensions()
	{
		return Block_CellType::TypeDimensions() * BlockIndexGrid::TypeDimensions();
	}
	static constexpr Vector3i BlockDimensions()
	{
		return Block_CellType::TypeDimensions();
	}

protected:

	// X/Y/Z dimensions of grid cells
	Vector3d CellDimensions;

	// Bounds on valid CellKey values. Note that this will be [-Extent, Extent-1], the -1 is due to the
	// fact that the "positive" side includes the 0-cell, so eg for a 4-cell dimension the valid indices are [-2,-1,0,1].
	AxisBox3i CellIndexBounds;


	// Total grid is an assembly of sub-grids of fixed size/type. Stored as separate grids in BlockData.
	// BlockIndexGrid is the second-level grid, each element corresponds to a BlockData sub-grid

	using Block_CellType = FixedGrid3<uint16_t, BlockSize_XY, BlockSize_XY, BlockSize_Z>;		// 16-bit indexable
	using Block_CellData = FixedGrid3<uint64_t, BlockSize_XY, BlockSize_XY, BlockSize_Z>;


	using Block_Material = FixedGrid3<uint64_t, BlockSize_XY, BlockSize_XY, BlockSize_Z>;

	// set of per-face materials for a block. A dynamic list of these is stored in BlockData below,
	// they are allocated as needed (ie when individual blocks need per-face materials).
	GS::ModelGridInternal::PackedFaceMaterialsV1 DefaultMaterials;

	struct BlockData
	{
		Block_CellType CellType;
		Block_CellData CellData;
		Block_Material Material;

		// allocated as necessary, indexed via Material.ExtendedIndex
		GS::unsafe_vector<GS::ModelGridInternal::PackedFaceMaterialsV1> BlockFaceMaterials;
	};
	void InitBlockData(BlockData& NewBlockData);
	void CopyBlockData(BlockData& ToBlockData, const BlockData& FromBlockData);

	// fixed grid of 16-bit indices, takes 64kb
	using BlockIndexGrid = FixedGrid3<uint16_t, IndexSize_XY, IndexSize_XY, IndexSize_Z>;		// capping world at 1024x1024x512, index is 15 bits  (could go 32,32,64? use extra bit for something?)
	// for debugging
	//using BlockIndexGrid = FixedGrid3<uint16_t, 2, 2, 2>;

	constexpr static uint16_t UNALLOCATED = Math16u::MaxValue();

	// each element in the IndexGrid is either UNALLOCATED or an index into the AllocatedBlocks array
	BlockIndexGrid IndexGrid;

	struct BlockContainer
	{
		BlockData* Data;
		Vector3i BlockIndex;
	};
	GS::unsafe_vector<BlockContainer> AllocatedBlocks;

	// this is the global minimum Key value that refers to a cell, ie -(WorldGridDimensions/2)
	Vector3i MinCoordCorner;

	// inclusive bounds on allocated chunk indices
	AxisBox3i AllocatedChunkBounds;

	// inclusive bounds on global Key indices that *may* contain non-EmptyCell values  
	// (currently conservative, assumes any non-const access might have written cell value)
	AxisBox3i ModifiedKeyBounds;

	// TODO: this may not be the right approach. Might be better if external owner passes locking ability
	//   into the various functions that might need it...
	// lock for AllocatedBlocks and IndexGrid data structures
	std::mutex BlockDataLock;

	void ToGlobalLocal(const CellKey& Key, Vector3i& Global, Vector3i& Local) const
	{
		Vector3i ShiftKey = Key - MinCoordCorner;
		Global = ShiftKey / Block_CellType::TypeDimensions();
		Local = ShiftKey - Global * Block_CellType::TypeDimensions();
	}

	CellKey ToKey(const Vector3i& GlobalIndex, const Vector3i& LocalIndex) const
	{
		return GlobalIndex* Block_CellType::TypeDimensions() + LocalIndex + MinCoordCorner;
	}

	const BlockData* ToLocalIfAllocated(const CellKey& Key, Vector3i& Local) const
	{
		Vector3i ShiftKey = Key - MinCoordCorner;
		Vector3i IndexIndex = ShiftKey / Block_CellType::TypeDimensions();
		uint16_t StorageIndex = IndexGrid[IndexIndex];
		if (StorageIndex == UNALLOCATED) return nullptr;
		Local = ShiftKey - IndexIndex * Block_CellType::TypeDimensions();
		return AllocatedBlocks[StorageIndex].Data;
	}

	const BlockData* GetAllocatedChunk(const Vector3i& BlockIndex) const
	{
		uint16_t StorageIndex = IndexGrid[BlockIndex];
		if (StorageIndex == UNALLOCATED) return nullptr;
		return AllocatedBlocks[StorageIndex].Data;
	}

	ModelGridCell UnpackToCell(const BlockData& BlockData, uint64_t LinearIndex) const;
	ModelGridCell UnpackToCell(const BlockData& BlockData, Vector3i LocalIndex) const;

	// assumes key is valid
	ModelGridCell GetCellInternal(CellKey Key) const
	{
		Vector3i LocalIndex;
		const BlockData* Data = ToLocalIfAllocated(Key, LocalIndex);
		if (Data == nullptr) return EmptyCell;
		return UnpackToCell(*Data, LocalIndex);
	}

	// assumes key is valid
	EModelGridCellType GetCellTypeInternal(CellKey Key) const
	{
		Vector3i LocalIndex;
		const BlockData* Data = ToLocalIfAllocated(Key, LocalIndex);
		if (Data == nullptr) return EModelGridCellType::Empty;
		int64_t LinearIndex = Data->CellType.ToLinearIndex(LocalIndex);
		return (EModelGridCellType)Data->CellType.Get(LocalIndex);
	}


	struct EditableCellRef
	{
		Vector3i BlockIndex = Vector3i::Zero();
		BlockData* Grid = nullptr;
		Vector3i LocalIndex = Vector3i::Zero();
	};

	BlockData* GetOrAllocateChunk(Vector3i BlockIndex);
	EditableCellRef GetEditableCellRef(CellKey Key);
	void ReinitializeCell_Internal(BlockData& GridBlockData, int64_t LinearBlockIndex, 
		const ModelGridCell& CopyFromCell, ModelGridCell* PrevCell = nullptr);

	friend class ModelGridSerializer;
	void RebuildAfterRestore();

	friend class ModelGridConstants;

private:
	// used in various places, hardcoded to ModelGridCell(), maybe can just be removed...
	ModelGridCell EmptyCell = ModelGridCell();

public:

	ModelGrid();
	ModelGrid(ModelGrid&& moved) = default;
	ModelGrid& operator=(ModelGrid&& moved) = default;
	ModelGrid(const ModelGrid& Other);
	ModelGrid& operator=(const ModelGrid& copy);
	~ModelGrid();

	void Initialize(Vector3d CellDimensions);
	void SetNewCellDimensions(Vector3d CellDimensions);

	const Vector3d& CellSize() const { return this->CellDimensions; }
	const Vector3d& GetCellDimensions() const { return this->CellDimensions; }

	/**
	 * Return integer bounds (inclusive) of the total valid cell indices
	 */
	AxisBox3i GetCellIndexRange() const { return CellIndexBounds; }

	/** 
	 * Return integer bounds of the grid region that has ever been accessed for editing, expanded by PadExtent.
	 * If bounds are invalid, ie grid has never been modified, then for PadExtent > 0 return [-PadExtent,PadExtent], otherwise return Empty() box
	 * Note that the upper-right corner is at (Box.Max+1), as the integer index corresponds to the min-corner
	 */ 
	AxisBox3i GetModifiedRegionBounds(int PadExtent) const;

	/**
	 * Calculate integer bounds of grid region that has non-empty cells
	 */
	AxisBox3i GetOccupiedRegionBounds(int PadExtent) const;

	inline CellKey GetCellAtPosition(const Vector3d& LocalPosition, bool& bIsInGrid) const;

	EModelGridCellType GetCellType(CellKey Key, bool& bIsInGrid) const;
	ModelGridCell GetCellInfo(CellKey Key, bool& bIsInGrid) const;
	//! returns false if cell key is not valid. CellInOut is only updated if cell is valid.
	bool GetCellInfoIfValid(CellKey Key, ModelGridCell& CellInOut) const;
	bool IsValidCell(CellKey Key) const;
	bool IsCellSolid(CellKey Key) const;
	bool IsCellEmpty(CellKey Key) const;
	AxisBox3d GetCellLocalBounds(CellKey Key) const;

	//Frame3d GetCellFrame(CellKey Key, bool bLocal) const;
	Frame3d GetCellFrame(CellKey Key) const;
	Frame3d GetCellFrameWorld(CellKey Key, const Frame3d& WorldFrame) const;

	bool ReinitializeCell(CellKey Key, const ModelGridCell& CopyFromCell, ModelGridCell* PrevCell = nullptr);


	bool AreCellsInSameBlock(CellKey A, CellKey B) const
	{
		return GetChunkIndexForKey(A) == GetChunkIndexForKey(B);
	}

	void EnumerateAdjacentConnectedChunks(CellKey Cell,
		FunctionRef<void(Vector3i, CellKey)> ProcessFunc) const;

	Vector3i GetChunkIndexForKey(CellKey Key) const
	{
		Vector3i ShiftKey = Key - MinCoordCorner;
		return ShiftKey / Block_CellType::TypeDimensions();
	}
	AxisBox3i GetKeyRangeForChunk(const Vector3i& BlockIndex) const
	{
		Vector3i MinCorner = BlockIndex * Block_CellType::TypeDimensions();
		MinCorner += MinCoordCorner;
		Vector3i MaxCorner = MinCorner + Block_CellType::TypeDimensions() - Vector3i::One();
		return AxisBox3i(MinCorner, MaxCorner);
	}
	AxisBox3i GetAllocatedChunkRangeBounds(const AxisBox3d& LocalBounds) const;

	bool IsChunkIndexAllocated(const Vector3i& BlockIndex) const
	{
		uint16_t StorageIndex = IndexGrid[BlockIndex];
		return (StorageIndex != UNALLOCATED && AllocatedBlocks[StorageIndex].Data != nullptr);
	}

	int GetNumAllocatedBlocks() const;
	void EnumerateAllocatedBlocks(FunctionRef<void(Vector3i)> BlockItemFunc) const;
	AxisBox3d GetChunkBounds(const Vector3i& BlockIndex) const;

	void EnumerateFilledCells(
		FunctionRef<void(CellKey Key, EModelGridCellType CellType)> EnumerateFunc) const;

	void EnumerateFilledCells(
		FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo, AxisBox3d LocalBounds)> ApplyFunc);

	void EnumerateFilledChunkCells(
		const Vector3i& BlockIndex,
		FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo, const AxisBox3d& LocalBounds)> ApplyFunc) const;


	// this is a utility for stack-based region-growing algos and probably should be moved out of this class as a template
	// todo: this will not be safe to use across mixed-DLL boundaries due to allocations...should maybe move it into .cpp and
	//       client can request an instance?
	struct EnumerateCellsCache
	{
		std::vector<CellKey> Stack;
		GS::unsafe_vector<CellKey> ProcessedArray;
		std::unordered_set<CellKey> ProcessedMap;
		bool bUsingMap = false;
		void Reset() { Stack.clear(); Stack.reserve(64); ProcessedArray.clear(); ProcessedArray.reserve(64); ProcessedMap.clear(); bUsingMap = false; }
		bool ItemsRemaining() const { return Stack.size() > 0; }
		bool HasBeenProcessed(CellKey Key) const { return (bUsingMap) ? ProcessedMap.contains(Key) : ProcessedArray.contains(Key); }
		void AddProcessed(CellKey Key);
		void AddToQueue(CellKey Key);
		CellKey RemoveNextFromQueue();
	};


	void EnumerateConnectedCells(
		CellKey InitialCellKey,
		FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo)> ApplyFunc,
		EnumerateCellsCache* Cache = nullptr);

	void EnumerateConnectedPlanarCells(
		CellKey InitialCellKey,
		int PlaneAxis,
		FunctionRef<bool(CellKey FromKey, CellKey ToKey)> ConnectedFilterFunc,
		FunctionRef<void(CellKey Key, const ModelGridCell& CellInfo)> ApplyFunc,
		bool bSkipEmpty,
		EnumerateCellsCache* Cache = nullptr);

	void EnumerateAdjacentCells(
		CellKey InitialCellKey,
		Vector3i HalfExtents, 
		bool bSkipEmpty,
		FunctionRef<void(CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo)> ApplyFunc);

	//! enumerates occupied column blocks for a given region. Locking.
	void EnumerateOccupiedColumnBlocks(const Vector2i& ColumnIndex,
		FunctionRef<void(Vector3i)> BlockFunc) const;


	//struct FUndoRedoData
	//{
	//	//Vector3d CellDimensions;
	//	//FAxisAlignedBox3i CellBounds;
	//	//DenseGridType CellStorage;
	//};

	//std::unique_ptr<FUndoRedoData> MakeCheckpoint() const;
	//void RestoreCheckpoint(const FUndoRedoData& Data);


	bool GetCellOrientationTransform(CellKey Key, TransformListd& TransformSeq, bool bIgnoreSubCellDimensions = false) const;

	Vector3d GetTransformedCellDimensions(uint8_t AxisDirection, uint8_t AxisRotation) const;

	bool ComputeCellBoxIntersection(const Ray3d& Ray, const Vector3i& CellKey, double& RayParameterOut, Vector3d& HitPositionOut, Vector3d& CellFaceNormalOut) const;


	void EnumerateBlockHandles(FunctionRef<void(GridRegionHandle RegionHandle)> ApplyFunc, bool bOnlyAllocated) const;
	GridRegionHandle GetHandleForBlock(Vector3i BlockIndex, bool bWantDataReferences = false) const;
	GridRegionHandle GetHandleForCell(Vector3i CellIndex, bool bWantDataReferences = false) const;

	
	struct UnsafeRawBlockEditor
	{
		GridRegionHandle RegionHandle;
		Vector3i GridMinCoordCorner;		// needed for coord tranforms

		Vector3i CurrentCellIndex = Vector3i::Zero();
		Vector3i CurrentLocalIndex = Vector3i::Zero();
		AxisBox3i ModifiedRegion = AxisBox3i::Empty();

		void SetCurrentCell(Vector3i CellIndex);
		ModelGridCell GetCellData();
		void SetCellData(const ModelGridCell& NewCell);
		bool GetCurrentCellNeighbourInBlock(Vector3i NeighbourOffset, ModelGridCell& NeighbourCellData);
		bool IsNeighbourCellInBlock(Vector3i NeighbourOffset);
		bool IsNeighbourCellOccupiedInBlock(Vector3i NeighbourOffset);
	};

	UnsafeRawBlockEditor GetRawBlockEditor_Safe(GridRegionHandle RegionHandle);
	ModelGridCell GetCellInfo_Safe(CellKey Key, bool& bIsInGrid) const;
};



ModelGrid::CellKey ModelGrid::GetCellAtPosition(const Vector3d& LocalPosition, bool& bIsInGrid) const
{
	Vector3i CellIndex(
		(int)GS::Floor(LocalPosition.X / CellDimensions.X),
		(int)GS::Floor(LocalPosition.Y / CellDimensions.Y),
		(int)GS::Floor(LocalPosition.Z / CellDimensions.Z));
	bIsInGrid = CellIndexBounds.Contains(CellIndex);
	return CellKey(CellIndex);
}






} // end namespace GS
