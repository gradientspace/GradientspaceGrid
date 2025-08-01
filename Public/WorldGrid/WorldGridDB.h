// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "WorldGrid/WorldGridInterfaces.h"
#include "Grid/GSAtomicGrid3.h"

#include <mutex>
#include <memory>
#include <vector>


namespace GS
{



class GRADIENTSPACEGRID_API IWorldGridDBListener
{
public:
	virtual ~IWorldGridDBListener() {}


	virtual void OnNewWorldRegionCreated_Async(WorldGridRegionIndex RegionIndex) = 0;
	virtual void OnWorldRegionDestroyed_Async(WorldGridRegionIndex RegionIndex) = 0;

	struct ModelGridBlockRequest
	{
		bool bImmediate = false;
		// these are not used currently...
		//uint32_t BlockIdentifier = 0;
		//uint32_t BlockPriority = 0;
	};

	virtual void OnNewModelGridBlocksRequired_Async(
		const std::vector<WorldGridModelBlockHandle>& BlockHandles, 
		ModelGridBlockRequest RequestParams) = 0;
	virtual void OnNewModelGridBlocksRequired_Immediate(
		const std::vector<WorldGridModelBlockHandle>& BlockHandles,
		IWorldGridDBListener::ModelGridBlockRequest RequestParams) = 0;
};





struct GRADIENTSPACEGRID_API WorldRegionModelGridInfo
{
	Vector3d WorldOrigin;

	struct BlockInfo
	{
		union {
			struct {
				uint8_t IsGenerated : 1;
				uint8_t IsEmpty : 1;
				uint8_t IsPendingAfterLoad : 1;
				uint8_t Unused : 5;
			};
			uint8_t Fields;
		};
	};
	static_assert(sizeof(BlockInfo) == 1);

	// info about each ModelGrid block for a given region
	FixedGrid3<BlockInfo, 32, 32, 32> BlockStates;
};



/**
 * Stores world grid
 * todo: all pure/geometric stuff should be factored into a separate class? 
 */
class GRADIENTSPACEGRID_API WorldGridDB
{
public:
	static constexpr Vector3i TypeCellDimensions()
	{
		return ModelGrid::ModelGridDimensions() * RegionIndexGrid::TypeDimensions();
	}
	static constexpr Vector3i TypeRegionDimensions()
	{
		return RegionIndexGrid::TypeDimensions();
	}

protected:
	// these are global parameters of the WorldGrid, set in Initialize() and never modified afterwards

	// X/Y/Z dimensions of unit grid cells
	const Vector3d CellDimensions;

	// X/Y/Z dimensions of ModelGrid-level chunks
	const Vector3d BlockBounds;

	// Bounds on valid cell indices. Note that this will be [-Extent, Extent-1], the -1 is due to the
	// fact that the "positive" side includes the 0-cell, so eg for a 4-cell dimension the valid indices are [-2,-1,0,1].
	const AxisBox3i CellIndexBounds;

	// this is the global minimum integer coord that refers to a cell. 
	const Vector3i MinCellCoordCorner;


protected:

	struct WorldRegionData
	{
		ModelGrid Grid;
		WorldRegionModelGridInfo GridInfo;
	};
	void InitRegionData(WorldRegionData& NewRegionData);

	//using RegionIndexGrid = FixedGrid3<uint16_t, 32, 32, 32>;		// 32 x 32 x 32 modelgrids, index is 15 bits
	// atomic has to be a uint32_t, but all our values will fit in uint16_t
	using RegionIndexGrid = AtomicFixedGrid3<uint32_t, 32, 32, 32>;
	
	constexpr static uint16_t UNALLOCATED = Math16u::MaxValue();

	// each element in the IndexGrid is either UNALLOCATED or an index into the AllocatedRegions array
	// note that this is a grid of atomics so it is safe to read & write from any thread
	// if a new region has to be allocated, AllocatedLock must be used
	RegionIndexGrid AllocatedIndexGrid;

	struct RegionContainer
	{
		WorldRegionData* Data;
		WorldGridRegionIndex RegionIndex;

		//! this is set to true on creation and set to false after either (1) we are certain there is no
		//! region data to load or (2) region data has finished being loaded
		std::atomic<bool> bIsLoadPending = false;

		// flag set if this region could ever possibly have been edited
		bool bPossiblyModified = false;

		mutable std::mutex region_lock;
	};
	typedef std::shared_ptr<RegionContainer> RegionContainerPtr;
	GS::unsafe_vector<RegionContainerPtr> AllocatedRegions;

	// inclusive bounds on allocated region indices
	AxisBox3i AllocatedRegionIndexBounds;

	// lock for AllocatedRegions / AllocatedRegionIndexBounds
	mutable std::mutex AllocatedLock;

	// why protected?
	constexpr WorldGridCellIndex ToCellIndex(const WorldGridRegionIndex& RegionIndex, const Vector3i& BlockRelativeIndex) const
	{
		return WorldGridCellIndex(
			(Vector3i)RegionIndex * ModelGrid::ModelGridDimensions() + BlockRelativeIndex + MinCellCoordCorner);
	}

	//! finds and returns region if it exists. thread-safe via locking.
	RegionContainerPtr GetRegion_Safe(const WorldGridRegionIndex& RegionIndex);
	//! finds and returns region if it exists, creates if it doesn't. thread-safe via locking.
	RegionContainerPtr GetOrCreateRegion_Safe(const WorldGridRegionIndex& RegionIndex, bool& bCreated);
	//! runs ProcessFunc on region if it exists, and returns true. returns false if region did not exist. Region is locked during processing.
	bool ProcessWorldRegion_Safe(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(const WorldRegionData&)> ProcessFunc, bool bWaitForPendingLoad ) const;


protected:
	// external connections

	std::vector<IWorldGridDBListener*> Listeners;
	IWorldGridStorageAPI* StorageAPI;



public:
	WorldGridDB();
	WorldGridDB(WorldGridDB&& moved) = delete;
	WorldGridDB& operator=(WorldGridDB&& moved) = delete;
	WorldGridDB(const WorldGridDB& Other) = delete;
	WorldGridDB& operator=(const WorldGridDB& copy) = delete;
	virtual ~WorldGridDB();

	void Initialize(Vector3d CellDimensions, IWorldGridDBListener* ListenerIn, IWorldGridStorageAPI* StorageAPI);

	//
	// index queries
	// generally these are pure/geometric functions that can be called from any thread.
	//

	//! converts 3D world position to world cell index. pure/geometric.
	inline constexpr WorldGridCellIndex PositionToCellIndex(const Vector3d& Position) const;

	//! calculate region index for a world cell. pure/geometric.
	constexpr WorldGridRegionIndex CellIndexToRegionIndex(const WorldGridCellIndex& CellIndex) const;

	// todo not 100% sure what BlockRelativeCellIndex is here...is it relative to modelgrid block, or to entire modelgrid? pure/geometric.
	constexpr void CellIndexToRegionAndBlockCellIndex(const WorldGridCellIndex& CellIndex, WorldGridRegionIndex& RegionIndex, Vector3i& BlockRelativeCellIndex, bool bWantSignedModelGridIndex) const;

	//! converts world CellIndex to a signed-coords relative index inside the owning ModelGrid (but does not return that grid...). pure/geometric.
	inline constexpr Vector3i CellIndexToRegionCellIndex(const WorldGridCellIndex& CellIndex) const;

	//
	// bounds queries
	// generally these are pure/geometric functions that can be called from any thread.
	//

	//! computes world bounding box from world cell index. pure/geometric.
	inline AxisBox3d GetCellBoundingBox(const WorldGridCellIndex& CellIndex) const;

	//! computes world bounding box of a modelgrid region. pure/geometric.
	AxisBox3d GetRegionWorldBounds(const WorldGridRegionIndex& RegionIndex) const;
	//! returns *inclusive* range of world cell indices for a modelgrid region. This function is geometric and safe to call from any thread. pure/geometric.
	AxisBox3i GetRegionIndexRange(const WorldGridRegionIndex& RegionIndex) const;

	//
	// data queries
	//

	//! returns cell data for a given world cell index. This is an immediate function, which will lock the region (so relatively expensive)
	bool QueryCellIndex(const WorldGridCellIndex& CellIndex, ModelGridCell& CellDataOut) const;

	//! enumerates occupied column blocks for a given region. This is an immediate function, which will lock the region (so relatively expensive)
	//! ColumnIndex here is relative to the region modelgrid
	void EnumerateOccupiedColumnBlocks(const WorldGridRegionIndex& RegionIndex, const Vector2i& ColumnIndex,
		FunctionRef<void(Vector3i)> BlockFunc) const;


	//
	// loading/unloading
	//

	//note: currently these three functions all lock/unlock a shared mutex to prevent multiple simultaneous calls.
	// This shouldn't be strictly necessary internally, but it's not clear that higher-level listeners can handle
	// the overlapping requests it might generate...

	//! Force-load any cells within Radius of WorldPostion. Effectively this force-loads regions and the region-modelgrid-blocks within that radius.
	//! This will (1) create regions, (2) load them from storage if necessary, (3) ask client to immediately generate missing blocks & meshes, before returning
	void RequireLoadedInRadius_Blocking(const Vector3d& WorldPosition, double Radius);

	//! request load of cells within Radius of WorldPosition. This will spawn any newly-touched regions before returning, however
	//! load-from-storage will run async, and requests to client to generate missing blocks, and mesh required blocks, will all run async.
	//! It is safe to call this function basically anywhere, ie multiple times per frame from multiple threads, in addition to across frames
	void RequestLoadedInRadius_Async(const Vector3d& WorldPosition, double Radius);

	//! request unload of regions that are outside the specified radius. This will save regions to storage if necessary/available.
	void UnloadRegionsOutsideRadius_Async(const Vector3d& WorldPosition, double Radius);

public:
	virtual void EnumerateLoadedRegions_Blocking( FunctionRef<void(WorldGridRegionIndex RegionIndex, const AxisBox3d& RegionBounds)> ApplyFunc ) const;

	virtual bool ProcessRegion_Blocking(const WorldGridRegionIndex& RegionIndex,
		FunctionRef<void(const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo)> ProcessFunc) const;

	virtual bool EditRegion_Blocking(const WorldGridRegionIndex& RegionIndex,
		FunctionRef<void(ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)> EditFunc);

protected:

	mutable std::mutex high_level_load_lock;

	bool EnsureRegionCreated_Safe(const WorldGridRegionIndex& RegionIndex, bool& bRegionWasCreatedOut);
	void EnumerateRegionsInRadius_Pure(const Vector3d& WorldPosition, double Radius, FunctionRef<void(WorldGridRegionIndex)> RegionFunc);
	struct FindOrCreateRegionInfo
	{
		WorldGridRegionIndex RegionIndex;
		bool bCreated;
	};
	void FindOrCreateRegionsInRadius_Safe(const Vector3d& WorldPosition, double Radius, std::vector<FindOrCreateRegionInfo>& RegionsOut);

	bool Internal_ProcessRegion_Blocking(const WorldGridRegionIndex& RegionIndex,
		FunctionRef<void(const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo)> ProcessFunc, bool bWaitForPendingLoad) const;

	bool Internal_EditRegion_Blocking(const WorldGridRegionIndex& RegionIndex,
		FunctionRef<void(ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)> EditFunc,
		bool bMarkRegionAsEdited, bool bWaitForPendingLoad);

	void CollectRegionBlocksInRadius(const WorldGridRegionIndex& RegionIndex, const Vector3d& WorldPosition, double Radius,
		std::vector<WorldGridModelBlockHandle>& NewBlocksRequiredForRegionOut);
	void NotifyListenersOfNewRequiredBlocks(std::vector<WorldGridModelBlockHandle>& NewBlocksRequired, bool bImmediate);

	// this just spins until a region we want to access has been loaded
	void WaitForPendingRegionLoad(const WorldGridRegionIndex& RegionIndex);

	struct PendingSaveRegionInfo
	{
		WorldGridRegionIndex RegionIndex;
		ModelGrid RegionGrid;
		WorldRegionModelGridInfo RegionGridInfo;
		std::atomic<bool> Cancel;
	};
	struct PendingLoadRegionInfo
	{
		WorldGridRegionIndex RegionIndex;
		size_t NumBytes;
		std::atomic<bool> Cancel;
	};

	struct PendingSaveOrLoad
	{
		Vector3i RegionIndex;
		std::shared_ptr<PendingSaveRegionInfo> PendingSave;
		std::shared_ptr<PendingLoadRegionInfo> PendingLoad;
		uint32_t Timestamp;
	};

	std::vector<PendingSaveOrLoad> PendingSavesAndLoads;
	uint32_t PendingTimestamp = 0;
	std::mutex PendingSaveLoadLock;
	void BeginSaveRegion_Async(std::shared_ptr<PendingSaveRegionInfo> SaveInfo);
	void BeginLoadRegion_Async(std::shared_ptr<PendingLoadRegionInfo> LoadInfo, bool bForceWait,
		std::function<void(bool)> LoadCompletedCallback_Async );
	bool IsLoadInProgressForRegion(WorldGridRegionIndex RegionIndex);
};


constexpr WorldGridCellIndex WorldGridDB::PositionToCellIndex(const Vector3d& Position) const
{
	return WorldGridCellIndex(
		(int)GS::Floor(Position.X / CellDimensions.X),
		(int)GS::Floor(Position.Y / CellDimensions.Y),
		(int)GS::Floor(Position.Z / CellDimensions.Z));
}


AxisBox3d WorldGridDB::GetCellBoundingBox(const WorldGridCellIndex& CellIndex) const
{
	Vector3d MinCorner((double)CellIndex.X * CellDimensions.X, (double)CellIndex.Y * CellDimensions.Y, (double)CellIndex.Z * CellDimensions.Z);
	return AxisBox3d(MinCorner, MinCorner + CellDimensions);
}

constexpr Vector3i WorldGridDB::CellIndexToRegionCellIndex(const WorldGridCellIndex& CellIndex) const
{
	WorldGridRegionIndex RegionIndex; Vector3i RelativeIndex;
	CellIndexToRegionAndBlockCellIndex(CellIndex, RegionIndex, RelativeIndex, true);	// convert to region coords which are signed
	return RelativeIndex;
}


constexpr WorldGridRegionIndex WorldGridDB::CellIndexToRegionIndex(const WorldGridCellIndex& CellIndex) const
{
	Vector3i ShiftKey = (Vector3i)CellIndex - MinCellCoordCorner;
	return WorldGridRegionIndex(ShiftKey / ModelGrid::ModelGridDimensions());
}
constexpr void WorldGridDB::CellIndexToRegionAndBlockCellIndex(const WorldGridCellIndex& CellIndex, WorldGridRegionIndex& RegionIndex, Vector3i& BlockRelativeCellIndex, bool bWantSignedModelGridIndex) const
{
	Vector3i ShiftKey = (Vector3i)CellIndex - MinCellCoordCorner;
	RegionIndex = WorldGridRegionIndex(ShiftKey / ModelGrid::ModelGridDimensions());
	BlockRelativeCellIndex = ShiftKey - (Vector3i)RegionIndex * ModelGrid::ModelGridDimensions();
	if (bWantSignedModelGridIndex)
		BlockRelativeCellIndex -= ModelGrid::ModelGridDimensions() / 2;
}



} // end namespace GS

