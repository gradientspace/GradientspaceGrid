// Copyright Gradientspace Corp. All Rights Reserved.
#include "WorldGrid/WorldGridSystem.h"

#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridMeshCache.h"
#include "ModelGrid/ModelGridEditMachine.h"
#include "ModelGrid/ModelGridWorker.h"
#include "ModelGrid/ModelGridSerializer.h"
#include "GenericGrid/RasterizeToGrid.h"
#include "WorldGrid/WorldGridHistory.h"
#include "WorldGrid/WorldGridGenerator.h"
#include "WorldGrid/WorldGridStorage.h"

#include "Core/ParallelFor.h"
#include "Core/GSAsync.h"
#include "Core/gs_debug.h"
#include "Grid/GSAtomicGrid3.h"
#include "Grid/GSGridUtil.h"
#include "GenericGrid/BoxIndexing.h"

using namespace GS;

WorldGridSystem::~WorldGridSystem()
{

}

void WorldGridSystem::Initialize(WorldGridParameters Parameters, WorldGridMeshSystemAPI* ExternalMeshSystemAPI, IWorldGridStorageAPI* ExternalStorageAPI)
{
	WorldParamters = Parameters;
	MeshSystemAPI = ExternalMeshSystemAPI;

	CurPlayerLocation = Vector3d::Zero();
	CurPlayerCell = WorldGridCellIndex(Vector3i::Zero());
	bCurPlayerLocationValid = false;

	if (ExternalStorageAPI != nullptr) {
		ExternalGridStorageAPI = ExternalStorageAPI;
		GridStorageAPI = ExternalGridStorageAPI;
	}
	else {
		WorldGridMemoryStorage* storage = new WorldGridMemoryStorage();
		InternalGridStorageAPI = std::shared_ptr<IWorldGridStorageAPI>(storage);
		GridStorageAPI = InternalGridStorageAPI.get();
	}

	GridDB.Initialize(WorldParamters.CellDimensions, this, GridStorageAPI);

	SetEnableHistory(Parameters.bTrackHistory);
}

void WorldGridSystem::OnNewWorldRegionCreated_Async(WorldGridRegionIndex RegionIndex)
{
	//UE_LOG(LogTemp, Warning, TEXT("WorldGrid new region created! %d %d %d"), RegionIndex.X, RegionIndex.Y, RegionIndex.Z);
	
	std::shared_ptr<LiveWorldGridRegion> NewRegion = std::make_shared<LiveWorldGridRegion>();
	NewRegion->RegionIndex = RegionIndex;
	NewRegion->RegionMode = ELiveRegionMode::NearField;
	NewRegion->MeshFactory = MeshSystemAPI->GetOrCreateMeshBuilderForRegionFunc(RegionIndex);
	NewRegion->MeshCache = std::make_unique<ModelGridMeshCache>();
	NewRegion->MeshCache->Initialize(WorldParamters.CellDimensions, NewRegion->MeshFactory.get());

	// avoids occlusion issues but way too expensive to do for the entire grid...maybe could
	// dynamically do for immediate grid?
	//NewRegion->MeshCache->bIncludeAllBlockBorderFaces = true;

	LiveRegionsLock.lock();
	gs_runtime_assert(LiveRegions.find(RegionIndex) == LiveRegions.end());
	LiveRegions[RegionIndex] = NewRegion;
	LiveRegionsLock.unlock();

	ClientsLock.lock();
	WorldGridRegionHandle RegionHandle{ RegionIndex };
	for (IWorldGridSystemClient* Client : Clients)
		Client->OnGridRegionLoaded_Async(RegionHandle);
	ClientsLock.unlock();
}


void WorldGridSystem::OnWorldRegionDestroyed_Async(WorldGridRegionIndex RegionIndex)
{
	GS_LOG("WorldGrid region destroyed! %d %d %d", RegionIndex.X, RegionIndex.Y, RegionIndex.Z);

	LiveRegionsLock.lock();
	gs_runtime_assert(LiveRegions.find(RegionIndex) != LiveRegions.end());
	auto LiveRegionPtr = LiveRegions[RegionIndex];
	LiveRegions.erase(RegionIndex);
	LiveRegionPtr.reset();
	LiveRegionsLock.unlock();

	ClientsLock.lock();
	WorldGridRegionHandle RegionHandle{ RegionIndex };
	for (IWorldGridSystemClient* Client : Clients)
		Client->OnGridRegionUnloaded_Async(RegionHandle);
	ClientsLock.unlock();
}




void WorldGridSystem::Test_PopulateBlocks_Blocking(
	WorldGridRegionIndex RegionIndex,
	std::shared_ptr<LiveWorldGridRegion> Region,
	const std::vector<GridRegionHandle>& PendingHandles, 
	std::vector<Vector3i>& ModifiedModelBlocksOut)
{
	// BE AWARE THAT THIS DOES NOT NECESSARILY POPULATE THE ENTIRE MODELGRID, 
	// ONLY BLOCKS THAT ARE REQUESTED (in PendingHandles)

	WorldGridGenerator::BlockInfo GeneratorBlockInfo;
	GeneratorBlockInfo.WorldRegionIndex = RegionIndex;
	GeneratorBlockInfo.OriginCell = (Vector3i)GridDB.PositionToCellIndex(Vector3d::Zero());
	GeneratorBlockInfo.WorldCellIndexRange = GridDB.GetRegionIndexRange(RegionIndex);
	GeneratorBlockInfo.BlockMinIndex = GridDB.CellIndexToRegionCellIndex(WorldGridCellIndex(GeneratorBlockInfo.WorldCellIndexRange.Min));  // this is modelgrid min index (ie signed index range)

	// launch job to generate region
	GridDB.EditRegion_Blocking(RegionIndex, [&](ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)
	{
		gs_debug_assert(Region->MeshCache->IsInitialized());

		std::vector<GridRegionHandle> LoadedHandles, GenerateHandles;
		for (GridRegionHandle handle : PendingHandles)
		{
			WorldRegionModelGridInfo::BlockInfo BlockInfo = ExtendedInfo.BlockStates.Get(handle.BlockIndex);
			if (BlockInfo.IsPendingAfterLoad == 1)
				LoadedHandles.push_back(handle);
			else
				GenerateHandles.push_back(handle);
		}

		WorldGridGenerator Generator;
		Generator.PopulateRegionBlocks_Blocking(GeneratorBlockInfo, RegionGrid, GenerateHandles, ExtendedInfo);
		ModifiedModelBlocksOut = std::move(Generator.ModifiedModelBlocksOut);

		for (GridRegionHandle handle : LoadedHandles) {
			WorldRegionModelGridInfo::BlockInfo BlockInfo = ExtendedInfo.BlockStates.Get(handle.BlockIndex);
			gs_debug_assert(BlockInfo.IsGenerated == 1);
			if ( BlockInfo.IsEmpty == false )
				ModifiedModelBlocksOut.push_back(handle.BlockIndex);
			BlockInfo.IsPendingAfterLoad = 0;
			ExtendedInfo.BlockStates.Set(handle.BlockIndex, BlockInfo);
		}
	});

}



void WorldGridSystem::Test_PopulateBlocksAndSpawnMeshJobs(WorldGridRegionIndex RegionBlockIndex, const std::vector<GridRegionHandle>& PendingHandles, bool bImmediate)
{
	LiveRegionsLock.lock();
	std::shared_ptr<LiveWorldGridRegion> Region = LiveRegions[RegionBlockIndex];
	LiveRegionsLock.unlock();

	std::vector<Vector3i> ModifiedModelBlocks;
	Test_PopulateBlocks_Blocking(RegionBlockIndex, Region, PendingHandles, ModifiedModelBlocks);

	if (ModifiedModelBlocks.size() == 0) 
		return;

	// should sort by distance to player, and not use a map!
	std::unordered_map<Vector2i, std::vector<Vector3i>> Columns;
	for (Vector3i ModelBlockIndex : ModifiedModelBlocks)
	{
		Vector2i ColumnIndex(ModelBlockIndex.X, ModelBlockIndex.Y);
		if (Columns.contains(ColumnIndex) == false)
		{
			Columns.insert({ ColumnIndex, std::vector<Vector3i>() });
		}
		Columns[ColumnIndex].push_back(ModelBlockIndex);
	}

	MeshUpdateParams UseParams = MeshUpdate_InitialSpawn( (bImmediate) ? Priority_ImmediateBlock() : Priority_FarBlock() );
	
	GS::TaskContainer LauncherTask = GS::Parallel::StartTask([Region, Columns, RegionBlockIndex, bImmediate, UseParams, this]()
	{
		std::vector<GS::TaskContainer> PendingTasks;

		// launch a separate mesh update job for each column. this would be preferable but currently it is not efficient
		// because of contention in locking the modelgrid (see SpawnUpdateMeshesJob_Async)
		//for (auto Pair : Columns)
		//{
		//	Vector2i Column = Pair.first;
		//	std::vector<Vector3i>& Blocks = Pair.second;
		//	GS::TaskContainer ColumnTask = SpawnUpdateMeshesJob_Async(Region, UseParams, std::move(Blocks));

		//	if (bImmediate)
		//		PendingTasks.push_back(ColumnTask);
		//}

		// launch a single job for the entire region, less efficient but avoids contention
		std::vector<Vector3i> AllBlocks;
		for (auto Pair : Columns) {
			for (Vector3i block : Pair.second)
				AllBlocks.push_back(block);
		}
		GS::TaskContainer ColumnTask = SpawnUpdateMeshesJob_Async(Region, UseParams, std::move(AllBlocks));
		if (bImmediate)
			PendingTasks.push_back(ColumnTask);

		if (bImmediate)
			GS::Parallel::WaitForAllTasks(PendingTasks);
	}, "PopulateBlocksAndSpawnMeshJobs");
	if (bImmediate)
		GS::Parallel::WaitForTask(LauncherTask);

}


void WorldGridSystem::Test_PopulateAndSpawnMeshJobs(const std::vector<WorldGridModelBlockHandle>& PendingHandles, bool bImmediate)
{
	// 
	unsafe_vector<WorldGridRegionIndex> UniqueBlocks;
	for (auto& Handle : PendingHandles) {
		UniqueBlocks.add_unique(Handle.WorldRegionHandle.BlockIndex);
	}

	std::vector<GS::TaskContainer> PendingTasks;

	for (WorldGridRegionIndex BlockIndex : UniqueBlocks)
	{
		// gross to have to copy PendingHandles here...
		GS::TaskContainer BlockTask = GS::Parallel::StartTask([BlockIndex, PendingHandles, bImmediate, this]()
		{
			std::vector<GridRegionHandle> ModelBlockHandles;
			for (auto& Handle : PendingHandles)
			{
				if (Handle.WorldRegionHandle.BlockIndex == BlockIndex)
					ModelBlockHandles.push_back(Handle.ModelBlockHandle);
			}
			Test_PopulateBlocksAndSpawnMeshJobs(BlockIndex, ModelBlockHandles, bImmediate);

		}, "PopulateAndSpawnMeshJobs");
		if (bImmediate)
			PendingTasks.push_back(BlockTask);
	}

	if (bImmediate)
		GS::Parallel::WaitForAllTasks(PendingTasks);
}




void WorldGridSystem::OnNewModelGridBlocksRequired_Async(const std::vector<WorldGridModelBlockHandle>& BlockHandles, IWorldGridDBListener::ModelGridBlockRequest RequestParams)
{
	Test_PopulateAndSpawnMeshJobs(BlockHandles, RequestParams.bImmediate);
}

void WorldGridSystem::OnNewModelGridBlocksRequired_Immediate(const std::vector<WorldGridModelBlockHandle>& BlockHandles, IWorldGridDBListener::ModelGridBlockRequest RequestParams)
{
	// TODO: currently this is hardcoded to publish the jobs with known ExternalPriority which we can use
	//  to filter in OnWaitForPendingRegionMeshUpdates below
	Test_PopulateAndSpawnMeshJobs(BlockHandles, true);

	// above may publish async mesh update requests, tell clients to force-wait for them
	ClientsLock.lock();
	for (IWorldGridSystemClient* Client : Clients)
		Client->OnWaitForPendingRegionMeshUpdates([&](const WorldGridMeshUpdate& Update) { return Update.ExternalPriority >= Priority_AdjacentBlock(); });
	ClientsLock.unlock();
}


void WorldGridSystem::UpdatePlayerLocation(int PlayerID, const Vector3d& NewLocation)
{
	CurPlayerLocation = NewLocation;
	WorldGridCellIndex NewCellIndex = GridDB.PositionToCellIndex(CurPlayerLocation);
	if (bCurPlayerLocationValid == false || NewCellIndex != CurPlayerCell)
	{
		CurPlayerCell = NewCellIndex;
		bCurPlayerLocationValid = true;

		// immediate blocking load
		// this ensures that a 3-cube radius around the player is always immediately available
		double LocalRadius = WorldParamters.CellDimensions.Length() * sqrt(2.01);
		GridDB.RequireLoadedInRadius_Blocking(CurPlayerLocation, LocalRadius);

		// non-blocking load for larger surrounding area. 20 @ 50cm blocks works out to 480m currently  (so ~1km diameter)
		//double BlockDiagonal = 20.0 * (WorldParamters.CellDimensions * (Vector3d)ModelGrid::BlockDimensions()).Length();
		//double BlockDiagonal = 8.0 * (WorldParamters.CellDimensions * (Vector3d)ModelGrid::BlockDimensions()).Length();
		double BlockDiagonal = 14.0 * (WorldParamters.CellDimensions * (Vector3d)ModelGrid::BlockDimensions()).Length();
		CurrentLoadingRadius = BlockDiagonal;
		GS::TaskContainer LoadRadiusTask = GS::Parallel::StartTask([this, BlockDiagonal]()
		{
			GridDB.RequestLoadedInRadius_Async(CurPlayerLocation, BlockDiagonal);
		}, "UpdatePlayerLocation");

		// ?!? the load is blocking...probably because there was no way to know if generation of a block was in-progress...
		GS::Parallel::WaitForTask(LoadRadiusTask);
	}
}


void WorldGridSystem::RunWorldCleanupJob()
{
	// todo run async...
	GridDB.UnloadRegionsOutsideRadius_Async(CurPlayerLocation, CurrentLoadingRadius * 1.1);
}



WorldGridCellIndex WorldGridSystem::GetCellIndexFromWorldPosition(const Vector3d& WorldPosition) const
{
	return GridDB.PositionToCellIndex(WorldPosition);
}

AxisBox3d WorldGridSystem::GetCellBoundingBox(const WorldGridCellIndex& CellIndex) const
{
	return GridDB.GetCellBoundingBox(CellIndex);
}

bool WorldGridSystem::QueryCellIndex(const WorldGridCellIndex& CellIndex, ModelGridCell& CellDataOut) const
{
	return GridDB.QueryCellIndex(CellIndex, CellDataOut);
}

AxisBox3d WorldGridSystem::GetRegionWorldBounds(WorldGridRegionHandle Handle) const
{
	return GridDB.GetRegionWorldBounds(Handle.BlockIndex);
}




bool WorldGridSystem::RegisterClient(IWorldGridSystemClient* Client)
{
	std::scoped_lock ScopeLock(ClientsLock);
	if (Client != nullptr && Clients.contains(Client) == false)
	{
		Clients.add(Client);
		return true;
	}
	return false;
}

bool WorldGridSystem::UnregisterClient(IWorldGridSystemClient* Client)
{
	std::scoped_lock ScopeLock(ClientsLock);
	if (Client == nullptr) return false;
	int64_t index = Clients.index_of(Client);
	if (index < 0) return false;
	Clients.remove_at(index);
	return true;
}




void WorldGridSystem::AccessRegion(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(LiveWorldGridRegion&)> ProcessFunc)
{
	// currently unused, do we need it?
	gs_runtime_assert(false);

	std::shared_ptr<LiveWorldGridRegion> RegionPtr;

	LiveRegionsLock.lock();
	auto found = LiveRegions.find(RegionIndex);
	if (found != LiveRegions.end())
	{
		RegionPtr = (*found).second;
	}
	LiveRegionsLock.unlock();

	if (RegionPtr)
	{
		// todo we need to lock this somehow? what if it is modified?
		ProcessFunc(*RegionPtr);
	}
}





void WorldGridSystem::TryPlaceBlock_Async(const WorldGridCellIndex& CellIndex, ModelGridCell NewCell)
{
	if (NewCell.CellType == EModelGridCellType::Empty) return;

	WorldGridRegionIndex RegionIndex; Vector3i ModelGridCellIndex;
	GridDB.CellIndexToRegionAndBlockCellIndex(CellIndex, RegionIndex, ModelGridCellIndex, true);

	GridRegionHandle ModelGridBlockHandle;

	bool bModified = false;
	GridDB.EditRegion_Blocking(RegionIndex, [&](ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)
	{
		ModelGridBlockHandle = RegionGrid.GetHandleForCell(ModelGridCellIndex);

		// mark block as non-empty...but we could be setting to empty...
		WorldRegionModelGridInfo::BlockInfo BlockInfo = ExtendedInfo.BlockStates.Get(ModelGridBlockHandle.BlockIndex);
		gs_debug_assert(BlockInfo.IsGenerated == 1);
		BlockInfo.IsEmpty = false;
		ExtendedInfo.BlockStates.Set(ModelGridBlockHandle.BlockIndex, BlockInfo);

		ModelGrid::UnsafeRawBlockEditor BlockEditor = RegionGrid.GetRawBlockEditor_Safe(ModelGridBlockHandle);
		BlockEditor.SetCurrentCell(ModelGridCellIndex);
		ModelGridCell CurCell = BlockEditor.GetCellData();
		if (CurCell.CellType == EModelGridCellType::Empty)
		{
			BlockEditor.SetCellData(NewCell);
			bModified = true;
		}
	});

	if (!bModified)
	{
		return;		// notify somehow??
	}

	if (bTrackHistory && bInUndoRedo == false)
	{
		History->PushPlaceBlock(CellIndex, NewCell);
	}

	// todo only really needs to be immediate if player is in this block...
	SpawnUpdateMeshJob_Async(RegionIndex, ModelGridBlockHandle, MeshUpdate_AddBlock(), true);
}



void WorldGridSystem::TryPlaceBlocks_Async(const std::vector<WorldGridCellIndex>& CellIndices, const std::vector<ModelGridCell>& NewCells, bool bReplace)
{
	if (CellIndices.size() == 0 || CellIndices.size() != NewCells.size()) return;
	int N = (int)CellIndices.size();

	// tod this bit should become a utility function...

	struct IndexInfo
	{
		int k;
		WorldGridRegionIndex RegionIndex;
		Vector3i ModelGridCellIndex;
		const bool operator<(const IndexInfo& Other) const { return RegionIndex < Other.RegionIndex; }
	};
	std::vector<IndexInfo> IndexInfo;
	//IndexInfo.SetNumUninitialized(N);
	IndexInfo.resize(N);
	for (int k = 0; k < N; ++k)
	{
		IndexInfo[k].k = k;
		GridDB.CellIndexToRegionAndBlockCellIndex(CellIndices[k], IndexInfo[k].RegionIndex, IndexInfo[k].ModelGridCellIndex, true);
	}
	std::sort(IndexInfo.begin(), IndexInfo.end());
	//IndexInfo.Sort();

	struct RegionIndexSpan
	{
		int Start = 0;
		int Count = 0;
	};
	std::vector<RegionIndexSpan> RegionSpans;
	WorldGridRegionIndex CurIndex = IndexInfo[0].RegionIndex;
	RegionIndexSpan CurSpan{ 0, 1 };
	for (int k = 1; k < N; ++k)
	{
		if (IndexInfo[k].RegionIndex != CurIndex)
		{
			RegionSpans.push_back(CurSpan);
			CurIndex = IndexInfo[k].RegionIndex;
			CurSpan = RegionIndexSpan{ k, 1 };
		}
		else
		{
			CurSpan.Count++;
		}
	}
	RegionSpans.push_back(CurSpan);



	for (RegionIndexSpan Span : RegionSpans)
	{
		int StartIndex = Span.Start;
		int Count = Span.Count;

		WorldGridRegionIndex RegionIndex = IndexInfo[StartIndex].RegionIndex;

		unsafe_vector<GridRegionHandle> BlocksToUpdate;		// using unsafe for add unique

		bool bModified = false;
		GridDB.EditRegion_Blocking(RegionIndex, [&](ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)
		{
			for (int k = 0; k < Count; ++k)
			{
				int UseIndex = StartIndex + k;
				int OrigIndex = IndexInfo[UseIndex].k;
				Vector3i ModelGridCellIndex = IndexInfo[UseIndex].ModelGridCellIndex;
				GridRegionHandle ModelGridBlockHandle = RegionGrid.GetHandleForCell(ModelGridCellIndex);

				// mark block as non-empty...but we could be setting to empty...
				WorldRegionModelGridInfo::BlockInfo BlockInfo = ExtendedInfo.BlockStates.Get(ModelGridBlockHandle.BlockIndex);
				gs_debug_assert(BlockInfo.IsGenerated == 1);
				BlockInfo.IsEmpty = false;
				ExtendedInfo.BlockStates.Set(ModelGridBlockHandle.BlockIndex, BlockInfo);

				// TODO should now sort by ModelGridBlockHandle...
				ModelGrid::UnsafeRawBlockEditor BlockEditor = RegionGrid.GetRawBlockEditor_Safe(ModelGridBlockHandle);
				BlockEditor.SetCurrentCell(ModelGridCellIndex);
				ModelGridCell CurCell = BlockEditor.GetCellData();	// todo don't need to read if bReplace=true..
				if (CurCell.CellType == EModelGridCellType::Empty || bReplace)
				{
					BlockEditor.SetCellData(NewCells[OrigIndex]);
					bModified = true;
					BlocksToUpdate.add_unique(ModelGridBlockHandle);
				}
			}
		});

		if (bModified)
		{
			if (bTrackHistory && bInUndoRedo == false)
			{
				for (int k = 0; k < Count; ++k)
				{
					int OrigIndex = IndexInfo[StartIndex+k].k;
					History->PushPlaceBlock(CellIndices[OrigIndex], NewCells[OrigIndex]);
				}
			}

			// todo only really needs to be immediate if player is in this block...
			for (GridRegionHandle ModelGridBlockHandle : BlocksToUpdate)
			{
				SpawnUpdateMeshJob_Async(RegionIndex, ModelGridBlockHandle, MeshUpdate_AddBlock(), true);
			}
		}
	}
}


void WorldGridSystem::TryDeleteBlock_Async(const WorldGridCellIndex& CellIndex)
{
	ModelGridCell SetEmptyCell = ModelGridCell();

	WorldGridRegionIndex RegionIndex; Vector3i ModelGridCellIndex;
	GridDB.CellIndexToRegionAndBlockCellIndex(CellIndex, RegionIndex, ModelGridCellIndex, true);

	GridRegionHandle ModelGridBlockHandle;
	std::vector<GridRegionHandle> AdjacentBlockHandles;

	ModelGridCell ExistingCell;

	bool bModified = false;
	GridDB.EditRegion_Blocking(RegionIndex, [&](ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)
	{
		ModelGridBlockHandle = RegionGrid.GetHandleForCell(ModelGridCellIndex);

		if (ExtendedInfo.BlockStates.Get(ModelGridBlockHandle.BlockIndex).IsEmpty == 1)
			return;  // entire block is already empty, we can skip this

		ModelGrid::UnsafeRawBlockEditor BlockEditor = RegionGrid.GetRawBlockEditor_Safe(ModelGridBlockHandle);
		BlockEditor.SetCurrentCell(ModelGridCellIndex);
		ExistingCell = BlockEditor.GetCellData();
		if (ExistingCell.CellType != EModelGridCellType::Empty)
		{
			BlockEditor.SetCellData(SetEmptyCell);
			bModified = true;

			RegionGrid.EnumerateAdjacentConnectedChunks(ModelGridCellIndex, [&](Vector3i BlockIndex, ModelGrid::CellKey CellKey)
			{
				AdjacentBlockHandles.push_back(RegionGrid.GetHandleForBlock(BlockIndex));
			});
		}

		// conceivably we might want to check if the entire block is empty now, if it is
		// we could deallocate it and set BlockState...
	});

	if (!bModified)
	{
		return;		// notify somehow??
	}

	if (bTrackHistory && bInUndoRedo == false)
	{
		History->PushRemoveBlock(CellIndex, ExistingCell);
	}

	SpawnUpdateMeshJob_Async(RegionIndex, ModelGridBlockHandle, MeshUpdate_RemoveBlock(), true);
	for (GridRegionHandle AdjacentHandle : AdjacentBlockHandles)
	{
		// TODO if this block contains player it should be bImmediate=true, otherwise player could fall through

		SpawnUpdateMeshJob_Async(RegionIndex, AdjacentHandle, MeshUpdate_RemoveBlock(), false);  // could do this in one shot? just avoiding adding another function here...
	}
}


void WorldGridSystem::SpawnUpdateMeshJob_Async(WorldGridRegionIndex RegionIndex, GridRegionHandle ModelGridBlockHandle, MeshUpdateParams UpdateParams, bool bForceWait)
{
	LiveRegionsLock.lock();
	// TODO assuming this exists!
	std::shared_ptr<LiveWorldGridRegion> Region = LiveRegions[RegionIndex];
	LiveRegionsLock.unlock();

	std::vector<Vector3i> ModelGridBlocks;
	ModelGridBlocks.push_back(ModelGridBlockHandle.BlockIndex);
	GS::TaskContainer Job = SpawnUpdateMeshesJob_Async(Region, UpdateParams, std::move(ModelGridBlocks));

	if (bForceWait)
		GS::Parallel::WaitForTask(Job);
}
GS::TaskContainer WorldGridSystem::SpawnUpdateMeshesJob_Async(
	std::shared_ptr<LiveWorldGridRegion> Region, 
	MeshUpdateParams UpdateParams,
	std::vector<Vector3i>&& ModelGridBlocksIn)
{
	if (ModelGridBlocksIn.size() == 0) return GS::TaskContainer();

	WorldGridRegionIndex RegionIndex = Region->RegionIndex;

	// TODO: calls to GridDB.ProcessRegion_Blocking() make this slow because we are potentially procesing many blocks in the same region,
	// and they all have to take turns waiting to access the modelgrid. Need some kind of read-only locking at the block level...
	//
	// NOTE: this is made worse because we want to run this function separately for each column in parallel! in that case they essentially
	// become serialized.   (however also note that the MeshCache is per-region and would have some issues for column-by-column)
	//
	// Part of the issue w/ processing per-block is that we want to look at neighbours to determine occlusion, and so conceptually we need
	// to read-lock the 8-neighbours of a block to be able to actually mesh it correctly...and also lock the neighbour region... 
	//
	// (perhaps processing each column in parallel at high level is not worth it? task below is still doing a lot in parallel)

	GS::TaskContainer BlockTask = GS::Parallel::StartTask([Region, RegionIndex, UpdateParams, ModelGridBlocks=std::move(ModelGridBlocksIn), this]()
	{
		GS_LOG("BlockTask for rgn %d,%d,%d  with %d blocks", RegionIndex.X,RegionIndex.Y,RegionIndex.Z, (int)ModelGridBlocks.size());

		// dangerous thing that skips any locking...resolves perf issue though, so it's definitely this locking
		//const ModelGrid* HackGrid = nullptr;
		//GridDB.ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo) {
		//	HackGrid = &RegionGrid;
		//});
		//const ModelGrid& RegionGrid = *HackGrid;

		// updated modified blocks
		std::vector<Vector2i> BlockColumnIndices;
		BlockColumnIndices.resize(ModelGridBlocks.size());
		GridDB.ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo) {
			GS::ParallelFor((uint32_t)ModelGridBlocks.size(), [&](int i)
			{
				Vector2i ColumnIndex;
				Region->MeshCache->UpdateBlockIndex_Async(RegionGrid, ModelGridBlocks[i], ColumnIndex);
				BlockColumnIndices[i] = ColumnIndex;
			});
		});

		// collect up unique modified columns
		unsafe_vector<Vector2i> ColumnsToUpdate;
		for (Vector2i ColumnIndex : BlockColumnIndices)
			ColumnsToUpdate.add_unique(ColumnIndex);

		// TODO technically no reason to keep these together in a ParallelFor, could launch tasks and forget about them,
		// except that we want the option for caller to force-wait...
		GS::ParallelFor((uint32_t)ColumnsToUpdate.size(), [&](int i)
		{
			Vector2i ColumnIndex = ColumnsToUpdate[i];

			// find rest of occupied modelgrid-column blocks and ensure they are all meshed
			// T-Array<Vector3i, TInlineAllocator<32>> SubRegionCells;
			std::vector<Vector3i> SubRegionCells;		// todo some kinda inline-allocator support...
			SubRegionCells.reserve(32);
			GridDB.ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo) {

				RegionGrid.EnumerateOccupiedColumnBlocks(ColumnIndex, [&](Vector3i ModelGridBlockIndex) {
					SubRegionCells.push_back(ModelGridBlockIndex);
				});

				GS::ParallelFor((uint32_t)SubRegionCells.size(), [&](int j) {
					Vector2i ColumnIndex;
					Region->MeshCache->RequireBlockIndex_Async(RegionGrid, SubRegionCells[j], ColumnIndex);
				});
			});

			// extract column mesh
			// TODO: should have a policy that caches meshes near player, but need a background job
			// to discard near-player cached meshes as they run around...
			std::shared_ptr<GS::IMeshCollector> Collector = MeshSystemAPI->GetOrCreateMeshAccumulatorForRegionFunc(Region->RegionIndex);
			bool bReleaseMeshes = (WorldParamters.CachingPolicy == EWorldGridMeshCachingPolicy::NeverCache) ? true : false;
			Region->MeshCache->ExtractColumnMesh_Async(ColumnIndex, *Collector, bReleaseMeshes);

			WorldGridMeshColumnHandle MeshHandle(WorldGridRegionHandle{ RegionIndex }, ColumnIndex);
			WorldGridMeshContainer MeshContainer;
			MeshContainer.Mesh = Collector;
			MeshContainer.WorldRegionBounds = GridDB.GetRegionWorldBounds(RegionIndex);
			MeshContainer.WorldRegionOrigin = MeshContainer.WorldRegionBounds.Center();

			// currently mesh is generated ModelGrid coordinates. So chunks further from the modelgrid origin
			// will have worse precision. Ideally would transform the meshes to local coordinates but this means
			// it will have to be translated around as it is regenerated. Possibly using the 'column' bounds would be the best option...
			MeshContainer.bMeshInRegionCoords = true;
			MeshContainer.MeshBounds = Collector->GetBounds();
			MeshContainer.WorldMeshBounds = MeshContainer.MeshBounds.Translated(MeshContainer.WorldRegionOrigin);

			WorldGridMeshUpdate MeshUpdate;
			MeshUpdate.WorldHandle = MeshHandle;
			MeshUpdate.MeshContainer = MeshContainer;
			MeshUpdate.Identifier = UpdateParams.Identifer;
			MeshUpdate.ExternalPriority = UpdateParams.Priority;

			ClientsLock.lock();
			for (IWorldGridSystemClient* Client : Clients)
				Client->OnGridRegionMeshUpdated_Async( MeshUpdate );
			ClientsLock.unlock();
		});
	}, "SpawnUpdateMeshesJob_Async");

	return BlockTask;
}


void WorldGridSystem::SetEnableHistory(bool bEnabled)
{
	bTrackHistory = bEnabled;
	if (bEnabled)
		History = std::make_shared<WorldGridHistory>();
	else
		History.reset();
}

void WorldGridSystem::ClearHistory()
{
	if (bTrackHistory && !!History )
		History->ClearHistory();
}

void WorldGridSystem::TryUndo()
{
	if (bTrackHistory && !!History )
	{
		bInUndoRedo = true;
		History->UndoOneStep(this);
		bInUndoRedo = false;
	}
}

void WorldGridSystem::TryRedo()
{
	if (bTrackHistory && !!History)
	{
		bInUndoRedo = true;
		History->RedoOneStep(this);
		bInUndoRedo = false;
	}
}
