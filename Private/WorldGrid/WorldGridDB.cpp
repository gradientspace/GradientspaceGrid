// Copyright Gradientspace Corp. All Rights Reserved.
#include "WorldGrid/WorldGridDB.h"
#include "WorldGrid/WorldGridStorage.h"
#include "ModelGrid/ModelGridSerializer.h"

#include "Core/gs_debug.h"
#include "Core/GSAsync.h"
#include "Core/ParallelFor.h"
#include "Grid/GSGridUtil.h"

#include <thread>

using namespace GS;


WorldGridDB::WorldGridDB()
{

}

WorldGridDB::~WorldGridDB()
{
	for (int k = 0; k < AllocatedRegions.size(); ++k)
	{
		delete AllocatedRegions[k]->Data;
		AllocatedRegions[k]->Data = nullptr;
	}
	AllocatedRegions.clear(true);
}


void WorldGridDB::Initialize(Vector3d CellDimensionsIn, IWorldGridDBListener* Listener, IWorldGridStorageAPI* StorageAPIIn)
{
	const_cast<Vector3d&>(CellDimensions) = CellDimensionsIn;

	Vector3i ModelGridDimensions = ModelGrid::ModelGridDimensions();
	const_cast<Vector3d&>(BlockBounds) = CellDimensions * (Vector3d)ModelGridDimensions;

	AllocatedIndexGrid.Initialize(WorldGridDB::UNALLOCATED);
	Vector3i ModelGridsDimensions = AllocatedIndexGrid.Dimensions();
	Vector3i MaxWorldDimensions = ModelGridsDimensions * ModelGridDimensions;
	const_cast<Vector3i&>(MinCellCoordCorner) = -MaxWorldDimensions / 2;

	// The index of cells is in the bottom-left, so the (0,0,0) cell is part of the 'positive extents',
	// and that means (eg) for a 4x4x4 grid, the valid integer X coordinates are [-2,-1,0,1]. So we have
	// to subtract one from the dimensions here.
	const_cast<AxisBox3i&>(CellIndexBounds) = AxisBox3i(MinCellCoordCorner, MinCellCoordCorner + MaxWorldDimensions - Vector3i::One());

	AllocatedRegionIndexBounds = AxisBox3i::Empty();

	// TODO: if we ever support adding additional listeners, then Listeners needs to be protected with a lock..
	if (Listener)
		Listeners.push_back(Listener);

	this->StorageAPI = StorageAPIIn;

	// temp debug output...
	//UE_LOG(LogTemp, Warning, TEXT("[WorldGridDB::Initialize]"));
	//UE_LOG(LogTemp, Warning, TEXT("     ModelGrid dimension is %d x %d x %d"), ModelGridDimensions.X, ModelGridDimensions.Y, ModelGridDimensions.Z);
	//UE_LOG(LogTemp, Warning, TEXT("     WorldGrid is %d x %d x %d ModelGrids"), ModelGridsDimensions.X, ModelGridsDimensions.Y, ModelGridsDimensions.Z);
	//UE_LOG(LogTemp, Warning, TEXT("     WorldSize is %d x %d x %d"), MaxWorldDimensions.X, MaxWorldDimensions.Y, MaxWorldDimensions.Z);
	//UE_LOG(LogTemp, Warning, TEXT("     WorldUnits are %.2fkm x %.2fkm x %.2fkm"), 
	//	MaxWorldDimensions.X*CellDimensions.X/(100.0*1000.0), MaxWorldDimensions.Y*CellDimensions.Y/(100.0*1000.0), MaxWorldDimensions.Z*CellDimensions.Z/(100.0*1000.0));
}


void WorldGridDB::InitRegionData(WorldRegionData& NewRegionData)
{
	NewRegionData.Grid.Initialize(CellDimensions);
	WorldRegionModelGridInfo::BlockInfo NewInfo;
	NewInfo.Fields = 0;
	NewRegionData.GridInfo.BlockStates.Initialize(NewInfo);
}


WorldGridDB::RegionContainerPtr WorldGridDB::GetRegion_Safe(const WorldGridRegionIndex& RegionIndex)
{
	RegionContainerPtr Result = RegionContainerPtr();

	// index grid is atomic so we can access it directly
	uint32_t StorageIndex = AllocatedIndexGrid[(Vector3i)RegionIndex];
	if (StorageIndex == WorldGridDB::UNALLOCATED)
		return Result;

	AllocatedLock.lock();
	Result = AllocatedRegions[StorageIndex];
	AllocatedLock.unlock();
		
	return Result;
}
WorldGridDB::RegionContainerPtr WorldGridDB::GetOrCreateRegion_Safe(const WorldGridRegionIndex& RegionIndex, bool& bCreated)
{
	RegionContainerPtr Result = RegionContainerPtr();
	bCreated = false;

	// todo: index grid is atomic so we could know if we need to allocate a block,
	// and do some of the setup before actually needing the allocated lock
	AllocatedLock.lock();

	uint16_t StorageIndex = (uint16_t)AllocatedIndexGrid[(Vector3i)RegionIndex];
	if (StorageIndex == WorldGridDB::UNALLOCATED)
	{
		uint16_t NewStorageIndex = (uint16_t)AllocatedRegions.size();
		AllocatedRegions.resize(NewStorageIndex + 1, false);

		RegionContainerPtr NewRegion = std::make_shared<RegionContainer>();
		NewRegion->RegionIndex = RegionIndex;
		NewRegion->Data = new WorldRegionData();
		gs_debug_assert(NewRegion->Data != nullptr);
		InitRegionData(*NewRegion->Data);
		NewRegion->Data->GridInfo.WorldOrigin = GetRegionWorldBounds(RegionIndex).Min;
		//AllocatedRegions.set_move(NewStorageIndex, std::move(NewRegion));
		AllocatedRegions.set_ref(NewStorageIndex, NewRegion);

		AllocatedIndexGrid.Set((Vector3i)RegionIndex, NewStorageIndex);
		AllocatedRegionIndexBounds.Contain((Vector3i)RegionIndex);
		bCreated = true;
		StorageIndex = NewStorageIndex;

		// always mark new region as pending load, until we are certain it isn't
		NewRegion->bIsLoadPending = true;
	}
	Result = AllocatedRegions[StorageIndex];

	AllocatedLock.unlock();

	return Result;
}

bool WorldGridDB::ProcessWorldRegion_Safe(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(const WorldRegionData&)> ProcessFunc, bool bWaitForPendingLoad) const
{
	// index grid is atomic so we can access it directly
	uint32_t StorageIndex = AllocatedIndexGrid[(Vector3i)RegionIndex];
	if (StorageIndex == WorldGridDB::UNALLOCATED)
		return false;

	AllocatedLock.lock();
	RegionContainerPtr ContainerPtr = AllocatedRegions[StorageIndex];
	AllocatedLock.unlock();

	while (bWaitForPendingLoad && ContainerPtr->bIsLoadPending)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

	ContainerPtr->region_lock.lock();
	ProcessFunc(*ContainerPtr->Data);
	ContainerPtr->region_lock.unlock();

	return true;
}


bool WorldGridDB::EnsureRegionCreated_Safe(const WorldGridRegionIndex& RegionIndex, bool& bRegionWasCreatedOut)
{
	// this function may be called from any thread!!

	// dimension check is against static data
	if (!AllocatedIndexGrid.IsValidIndex((Vector3i)RegionIndex)) {
		//UE_LOG(LogTemp, Warning, TEXT("WorldGridDB::EnsureRegionCreated_Safe : OOB BLOCK INDEX REQUESTED"));
		return false;
	}

	bool bCreated = false;
	RegionContainerPtr ContainerPtr = GetOrCreateRegion_Safe(RegionIndex, bCreated);
	if (!ContainerPtr)
	{
		//UE_LOG(LogTemp, Warning, TEXT("WorldGridDB::EnsureRegionCreated_Safe : GetOrAllocateChunk returned null!"));
		return false;
	}

	bRegionWasCreatedOut = bCreated;
	if (bCreated)
	{
		for (auto Listener : Listeners)
			Listener->OnNewWorldRegionCreated_Async(RegionIndex);
	}

	return true;
}




static bool IsBoundsInRadius(const AxisBox3d& WorldBounds, const Vector3d& WorldPosition, double Radius)
{
	bool bInSphere = WorldBounds.Contains(WorldPosition);
	if (!bInSphere)
	{
		double Distance = WorldBounds.DistanceSquared(WorldPosition);
		bInSphere = Distance < (Radius*Radius);
	}
	return bInSphere;
}


void WorldGridDB::FindOrCreateRegionsInRadius_Safe(const Vector3d& WorldPosition, double Radius, std::vector<WorldGridDB::FindOrCreateRegionInfo>& RegionsOut)
{
	RegionsOut.clear();
	EnumerateRegionsInRadius_Pure(WorldPosition, Radius, [&](WorldGridRegionIndex RegionIndex) { 
		FindOrCreateRegionInfo Info;
		Info.RegionIndex = RegionIndex;
		EnsureRegionCreated_Safe(RegionIndex, Info.bCreated);
		RegionsOut.push_back(Info);
	});
}

void WorldGridDB::RequireLoadedInRadius_Blocking(const Vector3d& WorldPosition, double Radius)
{
	std::scoped_lock sync_loads_lock(high_level_load_lock);

	std::vector<FindOrCreateRegionInfo> RelevantRegions;
	FindOrCreateRegionsInRadius_Safe(WorldPosition, Radius, RelevantRegions);
	if (RelevantRegions.size() == 0) return;

	IWorldGridDBListener::ModelGridBlockRequest RequestParams;
	RequestParams.bImmediate = true;

	//std::vector<WorldGridModelBlockHandle> AllNewBlocksRequired;
	//std::mutex all_blocks_lock;

	// can process all required blocks in parallel, but must wait for all to finish
	GS::ParallelFor((uint32_t)RelevantRegions.size(), [&](uint32_t idx) {
		const FindOrCreateRegionInfo& RegionInfo = RelevantRegions[idx];
		auto RegionIndex = RegionInfo.RegionIndex;

		RegionContainerPtr RegionPtr = GetRegion_Safe(RegionIndex);		// only using this to access atomic bIsLoadPending!!

		// if region was created, we may need to load it
		if (RegionInfo.bCreated)
		{
			size_t RegionBytes = 0;
			bool bLoadRegion = RegionInfo.bCreated && StorageAPI != nullptr && StorageAPI->HasWorldGridRegion(RegionIndex, RegionBytes);
			if (bLoadRegion && RegionBytes > 0) 
			{
				GS_LOG("Loading WorldGrid Region %d %d %d", RegionIndex.X, RegionIndex.Y, RegionIndex.Z);

				std::shared_ptr<PendingLoadRegionInfo> PendingLoad = std::make_shared<PendingLoadRegionInfo>();
				PendingLoad->RegionIndex = RegionIndex;
				PendingLoad->NumBytes = RegionBytes;
				PendingLoad->Cancel = false;
				BeginLoadRegion_Async(PendingLoad, /*bForceWaitForLoad=*/true, [](bool) {});
			} else 
				RegionPtr->bIsLoadPending = false;
		}

		// load must no longer pending, because we forced it
		gs_debug_assert(RegionPtr->bIsLoadPending == false);

		// it's possible to have the situation where RequestLoadedInRadius_Async() has created a region and kicked off a 
		// background load, and now we want to immediate-load. In that case, we need to wait for the load to finish from
		// this parallel-for thread.
		while (RegionPtr->bIsLoadPending)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));

		std::vector<WorldGridModelBlockHandle> NewBlocksRequiredForRegion;

		// find the blocks inside this region that are within the radius
		Internal_ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& BlockModelGrid, const WorldRegionModelGridInfo& ExtendedInfo)
		{
			AxisBox3d RegionWorldBounds = GetRegionWorldBounds(RegionIndex);
			Vector3d ModelGridOrigin = BlockModelGrid.GetChunkBounds(Vector3i::Zero()).Min;		// should be better way to get this...

			BlockModelGrid.EnumerateBlockHandles([&](GridRegionHandle ModelBlockHandle)
			{
				bool bIsKnownEmpty = (ExtendedInfo.BlockStates.Get(ModelBlockHandle.BlockIndex).IsEmpty == 1);
				if (bIsKnownEmpty) return;

				bool bChunkExists = BlockModelGrid.IsChunkIndexAllocated(ModelBlockHandle.BlockIndex);
				bool bIsPendingAfterLoad = (ExtendedInfo.BlockStates.Get(ModelBlockHandle.BlockIndex).IsPendingAfterLoad == 1);
				if (bChunkExists && bIsPendingAfterLoad == false) return;

				AxisBox3d LocalChunkBounds = BlockModelGrid.GetChunkBounds(ModelBlockHandle.BlockIndex);
				AxisBox3d WorldChunkBounds = LocalChunkBounds;
				WorldChunkBounds.Translate(-ModelGridOrigin + ExtendedInfo.WorldOrigin);	// translate to world
				gs_debug_assert(RegionWorldBounds.Contains(WorldChunkBounds));
				if (IsBoundsInRadius(WorldChunkBounds, WorldPosition, Radius))
				{
					WorldGridModelBlockHandle Handle(RegionIndex, ModelBlockHandle);
					NewBlocksRequiredForRegion.push_back(Handle);
				}

			}, /*bOnlyAllocated=*/false);
		}, /*bWaitForPendingLoad=*/false );

		// notify about new regions per-thread
		NotifyListenersOfNewRequiredBlocks(NewBlocksRequiredForRegion, true);

		// accumulate new regions and request them after (safer but above needs to be possible eventually...)
		//all_blocks_lock.lock();
		//for (WorldGridModelBlockHandle handle : NewBlocksRequiredForRegion)
		//	AllNewBlocksRequired.push_back(handle);
		//all_blocks_lock.unlock();
	});


	// could this be done per-parallel-for-thread?? that would be ideal as this runs the generator
	//if (AllNewBlocksRequired.size() > 0) {
	//	for (auto Listener : Listeners)
	//		Listener->OnNewModelGridBlocksRequired_Immediate(AllNewBlocksRequired, RequestParams);
	//}
}






void WorldGridDB::RequestLoadedInRadius_Async(const Vector3d& WorldPosition, double Radius)
{
	std::scoped_lock sync_loads_lock(high_level_load_lock);

	std::vector<FindOrCreateRegionInfo> RelevantRegions;
	FindOrCreateRegionsInRadius_Safe(WorldPosition, Radius, RelevantRegions);
	if (RelevantRegions.size() == 0) return;

	// TODO: somehow need to avoid re-processing this constantly...we only want to process blocks in the radius,
	// but ideally want to avoid re-re-processing every time the radius changes!


	// TODO: this is called every frame and it's not clear that anything would prevent
	// the previous-frame work from still being ongoing when the next frame happens...
	// (similarly w/ async-vs-next-frame-immediate call)
	//
	// the tricky bit is that the posts to listeners are fire-and-forget, so the listener
	// also has to be able to handle duplicate blocks and/or basically do the same checks as in CollectRegionBlocksInRadius()...
	// 
	// possibly need some kinda timestamp-like system, so the listener can know if a request was made
	// for the same or a different state of the block...


	GS::ParallelFor((uint32_t)RelevantRegions.size(), [&](int idx) {
		const FindOrCreateRegionInfo& RegionInfo = RelevantRegions[idx];
		auto RegionIndex = RegionInfo.RegionIndex;

		// for now, if a load for this region is in-flight, we are just going to skip it and let a later call handle different position
		if (IsLoadInProgressForRegion(RegionIndex))
			return;

		RegionContainerPtr RegionPtr = GetRegion_Safe(RegionIndex);		// only using this to access atomic bIsLoadPending!!

		// if we created this region, we have to decide whether or not to load it
		bool bStartedLoad = false;
		if (RegionInfo.bCreated) 
		{
			size_t RegionBytes = 0;
			bool bLoadRegion = RegionInfo.bCreated && StorageAPI != nullptr && StorageAPI->HasWorldGridRegion(RegionIndex, RegionBytes);
			if (RegionBytes == 0) bLoadRegion = false;

			if (bLoadRegion)
			{
				GS_LOG("[WorldGridDB::RequestLoadedInRadius_Async] Starting load of WorldGrid Region %d %d %d", RegionIndex.X, RegionIndex.Y, RegionIndex.Z);
				std::shared_ptr<PendingLoadRegionInfo> PendingLoad = std::make_shared<PendingLoadRegionInfo>();
				PendingLoad->RegionIndex = RegionIndex;
				PendingLoad->NumBytes = RegionBytes;
				PendingLoad->Cancel = false;
				BeginLoadRegion_Async(PendingLoad, /*bForceWaitForLoad=*/false, [this, RegionIndex, WorldPosition, Radius](bool bCompleted) {
					// region has been loaded, now we can determine which blocks we need and send them on
					// (alternately we could just wait until next time this is called?)
					std::vector<WorldGridModelBlockHandle> NewBlocksRequiredForRegion;
					CollectRegionBlocksInRadius(RegionIndex, WorldPosition, Radius, NewBlocksRequiredForRegion);
					NotifyListenersOfNewRequiredBlocks(NewBlocksRequiredForRegion, false);
					GS_LOG("[WorldGridDB::RequestLoadedInRadius_Async]   Finished load of WorldGrid Region %d %d %d", RegionIndex.X, RegionIndex.Y, RegionIndex.Z);
				});
				bStartedLoad = true;
			}
			else
				RegionPtr->bIsLoadPending = false;		// if we created region but have nothing to load, load isn't pending anymore
		}

		// if we did not begin a load, and no load is pending, we can process this region
		if (bStartedLoad == false && RegionPtr->bIsLoadPending == false)
		{
			std::vector<WorldGridModelBlockHandle> NewBlocksRequiredForRegion;
			CollectRegionBlocksInRadius(RegionIndex, WorldPosition, Radius, NewBlocksRequiredForRegion);
			NotifyListenersOfNewRequiredBlocks(NewBlocksRequiredForRegion, false);
		}

	});
}


void WorldGridDB::NotifyListenersOfNewRequiredBlocks(std::vector<WorldGridModelBlockHandle>& NewBlocksRequired, bool bImmediate)
{
	if (Listeners.size() == 0 || NewBlocksRequired.size() == 0) return;

	IWorldGridDBListener::ModelGridBlockRequest RequestParams;
	RequestParams.bImmediate = bImmediate;
	for (auto Listener : Listeners) {
		if (bImmediate)
			Listener->OnNewModelGridBlocksRequired_Immediate(NewBlocksRequired, RequestParams);
		else
			Listener->OnNewModelGridBlocksRequired_Async(NewBlocksRequired, RequestParams);
	}
}


void WorldGridDB::CollectRegionBlocksInRadius(const WorldGridRegionIndex& RegionIndex, const Vector3d& WorldPosition, double Radius,
	std::vector<WorldGridModelBlockHandle>& NewBlocksRequiredForRegionOut)
{
	Internal_ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& BlockModelGrid, const WorldRegionModelGridInfo& ExtendedInfo)
	{
		AxisBox3d RegionWorldBounds = GetRegionWorldBounds(RegionIndex);
		Vector3d ModelGridOrigin = BlockModelGrid.GetChunkBounds(Vector3i::Zero()).Min;		// should be better way to get this...

		// TODO: should compute intersection between sphere (or sphere-bbox) and block grid and use it to filter this enumeration...

		BlockModelGrid.EnumerateBlockHandles([&](GridRegionHandle ModelBlockHandle)
		{
			bool bIsKnownEmpty = (ExtendedInfo.BlockStates.Get(ModelBlockHandle.BlockIndex).IsEmpty == 1);
			if (bIsKnownEmpty) return;

			bool bChunkExists = BlockModelGrid.IsChunkIndexAllocated(ModelBlockHandle.BlockIndex);
			bool bIsPendingAfterLoad = (ExtendedInfo.BlockStates.Get(ModelBlockHandle.BlockIndex).IsPendingAfterLoad == 1);
			if (bChunkExists && bIsPendingAfterLoad == false) return;

			AxisBox3d LocalChunkBounds = BlockModelGrid.GetChunkBounds(ModelBlockHandle.BlockIndex);
			AxisBox3d WorldChunkBounds = LocalChunkBounds;
			WorldChunkBounds.Translate(-ModelGridOrigin + ExtendedInfo.WorldOrigin);	// translate to world
			gs_debug_assert(RegionWorldBounds.Contains(WorldChunkBounds));
			if (IsBoundsInRadius(WorldChunkBounds, WorldPosition, Radius))
			{
				WorldGridModelBlockHandle Handle(RegionIndex, ModelBlockHandle);
				NewBlocksRequiredForRegionOut.push_back(Handle);
			}

		}, /*bOnlyAllocated=*/false);
	}, /*bWaitForPendingLoad=*/false );
}



void WorldGridDB::UnloadRegionsOutsideRadius_Async(const Vector3d& WorldPosition, double Radius)
{
	std::scoped_lock sync_loads_lock(high_level_load_lock);

	std::vector<WorldGridRegionIndex> Regions;
	AllocatedLock.lock();
	for (const RegionContainerPtr& Ptr : AllocatedRegions)
	{
		AxisBox3d WorldBounds = GetRegionWorldBounds(Ptr->RegionIndex);
		if (IsBoundsInRadius(WorldBounds, WorldPosition, Radius) == false)
			Regions.push_back(Ptr->RegionIndex);
	}
	AllocatedLock.unlock();

	for (WorldGridRegionIndex UnloadIndex : Regions)
	{
		// remove region from internal data structures
		AllocatedLock.lock();
		uint32_t RemoveIndex = AllocatedIndexGrid.Get( (Vector3i)UnloadIndex );
		gs_debug_assert(RemoveIndex != UNALLOCATED);
		RegionContainerPtr BlockPtr = AllocatedRegions[RemoveIndex];
		AllocatedIndexGrid.Set( (Vector3i)UnloadIndex, UNALLOCATED );
		int64_t SwappedIndex = -1;
		AllocatedRegions.swap_remove(RemoveIndex, SwappedIndex);
		if (SwappedIndex >= 0)
		{
			RegionContainerPtr SwappedPtr = AllocatedRegions[ RemoveIndex ];
			WorldGridRegionIndex UpdateIndex = SwappedPtr->RegionIndex;
			AllocatedIndexGrid.Set((Vector3i)UpdateIndex, RemoveIndex);
		}
		AllocatedLock.unlock();

		// save region data if block was modified
		if (BlockPtr->bPossiblyModified && StorageAPI != nullptr)
		{
			std::shared_ptr<PendingSaveRegionInfo> PendingSave = std::make_shared<PendingSaveRegionInfo>();
			PendingSave->RegionIndex = UnloadIndex;
			PendingSave->RegionGrid = std::move(BlockPtr->Data->Grid);
			PendingSave->RegionGridInfo = std::move(BlockPtr->Data->GridInfo);
			PendingSave->Cancel = false;
			BeginSaveRegion_Async(PendingSave);			
		}

		delete BlockPtr->Data;
		BlockPtr->Data = nullptr;
		BlockPtr.reset();

		for (auto Listener : Listeners)
			Listener->OnWorldRegionDestroyed_Async(UnloadIndex);
	}
}






void WorldGridDB::BeginSaveRegion_Async(std::shared_ptr<PendingSaveRegionInfo> SaveInfo)
{
	PendingSaveOrLoad Pending;
	Pending.RegionIndex = (Vector3i)SaveInfo->RegionIndex;
	Pending.PendingSave = SaveInfo;

	// todo: find out if we have a pending save or load for this region, and handle those cases
	uint32_t CurrentTimestamp = 0;
	PendingSaveLoadLock.lock();
	CurrentTimestamp = PendingTimestamp++;
	Pending.Timestamp = CurrentTimestamp;
	PendingSavesAndLoads.push_back(Pending);
	PendingSaveLoadLock.unlock();

	GS::Parallel::StartTask([SaveInfo, this, CurrentTimestamp]()
	{
		// clear any PendingAfterLoad flags (maybe should not be stored here?)
		SaveInfo->RegionGridInfo.BlockStates.EnumerateAllCells([&](int64_t LinearIndex, WorldRegionModelGridInfo::BlockInfo blockInfo)
		{
			blockInfo.IsPendingAfterLoad = 0;
			SaveInfo->RegionGridInfo.BlockStates[LinearIndex] = blockInfo;
		});

		// run serialization
		MemorySerializer Serializer;
		Serializer.BeginWrite();
		ModelGridSerializer::Serialize(SaveInfo->RegionGrid, Serializer);
		SaveInfo->RegionGridInfo.BlockStates.Data.Store(Serializer, "BlockStates");

		// todo: cannot pass along the buffer here because Serializer stores via std::vector and so we cannot steal the memory!
		size_t NumBytes;
		const uint8_t* buffer = Serializer.GetBuffer(NumBytes);

		StorageAPI->StoreWorldGridRegion(SaveInfo->RegionIndex, buffer, NumBytes, /*bTakeOwnership=*/false);
		GS_LOG("[WorldGridDB] Stored region %d,%d,%d!", SaveInfo->RegionIndex.X, SaveInfo->RegionIndex.Y, SaveInfo->RegionIndex.Z);

		// remove from pending list
		PendingSaveLoadLock.lock();
		auto found_itr = std::find_if(PendingSavesAndLoads.begin(), PendingSavesAndLoads.end(), [&](const PendingSaveOrLoad& item) {
			return item.Timestamp == CurrentTimestamp;
		});
		gs_debug_assert(found_itr != PendingSavesAndLoads.end());
		PendingSavesAndLoads.erase(found_itr);
		PendingSaveLoadLock.unlock();
	});
}


void WorldGridDB::BeginLoadRegion_Async(std::shared_ptr<PendingLoadRegionInfo> LoadInfo, bool bForceWait,
	std::function<void(bool)> LoadCompletedCallback_Async)
{
	PendingSaveOrLoad Pending;
	Pending.RegionIndex = (Vector3i)LoadInfo->RegionIndex;
	Pending.PendingLoad = LoadInfo;

	// todo: find out if we have a pending save or load for this region, and handle those cases
	uint32_t CurrentTimestamp = 0;
	PendingSaveLoadLock.lock();
	CurrentTimestamp = PendingTimestamp++;
	Pending.Timestamp = CurrentTimestamp;
	PendingSavesAndLoads.push_back(Pending);
	PendingSaveLoadLock.unlock();
	
	GS::TaskContainer LoadTask = GS::Parallel::StartTask([LoadInfo, this, CurrentTimestamp, LoadCompletedCallback_Async]()
	{
		bool bLoadCompleted = false;
		dynamic_buffer<uint8_t> Data;
		Data.resize(LoadInfo->NumBytes);
		bool bOK = StorageAPI->FetchWorldGridRegion(LoadInfo->RegionIndex, Data.raw_pointer(), Data.size());
		if (bOK) 
		{
			GS::ModelGrid RestoredGrid;
			GS::WorldRegionModelGridInfo RestoredGridInfo;
			GS::MemorySerializer Serializer;
			Serializer.InitializeMemory((size_t)Data.size(), Data.raw_pointer());
			Serializer.BeginRead();
			bool bRestoredGrid = GS::ModelGridSerializer::Restore(RestoredGrid, Serializer);
			gs_debug_assert(bRestoredGrid);
			bool bRestoredStateData = bRestoredGrid && RestoredGridInfo.BlockStates.Data.Restore(Serializer, "BlockStates");
			gs_debug_assert(bRestoredStateData);
			bool bRestoreOK = (bRestoredGrid && bRestoredStateData);
			if (bRestoreOK) {

				// set PendingAfterLoad flags for any generated blocks that were loaded
				RestoredGridInfo.BlockStates.EnumerateAllCells([&](int64_t LinearIndex, WorldRegionModelGridInfo::BlockInfo blockInfo) {
					if (blockInfo.IsGenerated == 1) {
						blockInfo.IsPendingAfterLoad = 1;
						RestoredGridInfo.BlockStates[LinearIndex] = blockInfo;
					}
				});

				// actually run grid update
				Internal_EditRegion_Blocking(LoadInfo->RegionIndex, [&](ModelGrid& RegionGrid, WorldRegionModelGridInfo& RegionGridInfo)
				{
					RegionGrid = std::move(RestoredGrid);
					RegionGridInfo.BlockStates = std::move(RestoredGridInfo.BlockStates);
					bLoadCompleted = true;
				}, /*bMarkRegionAsEdited=*/false, /*bWaitForPendingLoad=*/false );
			}
		}

		LoadCompletedCallback_Async(bLoadCompleted);

		// remove from pending list
		PendingSaveLoadLock.lock();
		auto found_itr = std::find_if(PendingSavesAndLoads.begin(), PendingSavesAndLoads.end(), [&](const PendingSaveOrLoad& item) {
			return item.Timestamp == CurrentTimestamp;
		});
		gs_debug_assert(found_itr != PendingSavesAndLoads.end());
		PendingSavesAndLoads.erase(found_itr);
		PendingSaveLoadLock.unlock();

		// mark region as not needing load anymore
		RegionContainerPtr RegionPtr = GetRegion_Safe(LoadInfo->RegionIndex);
		gs_debug_assert(RegionPtr->bIsLoadPending == true);
		RegionPtr->bIsLoadPending = false;
	});


	if (bForceWait)
		GS::Parallel::WaitForTask(LoadTask);
}

bool WorldGridDB::IsLoadInProgressForRegion(WorldGridRegionIndex RegionIndex)
{
	PendingSaveLoadLock.lock();
	auto found_itr = std::find_if(PendingSavesAndLoads.begin(), PendingSavesAndLoads.end(), [&](const PendingSaveOrLoad& item) {
		return item.RegionIndex == RegionIndex;
	});
	bool bIsPending = (found_itr != PendingSavesAndLoads.end());
	PendingSaveLoadLock.unlock();

	return bIsPending;
}

void WorldGridDB::WaitForPendingRegionLoad(const WorldGridRegionIndex& RegionIndex)
{
	RegionContainerPtr RegionPtr = GetRegion_Safe(RegionIndex);
	gs_debug_assert(RegionPtr != nullptr);
	while (RegionPtr->bIsLoadPending)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
}


void WorldGridDB::EnumerateRegionsInRadius_Pure(const Vector3d& WorldPosition, double Radius, FunctionRef<void(WorldGridRegionIndex)> RegionFunc)
{
	WorldGridRegionIndex MinBoxIndex = CellIndexToRegionIndex( PositionToCellIndex(WorldPosition - Vector3d(Radius)) );
	WorldGridRegionIndex MaxBoxIndex = CellIndexToRegionIndex( PositionToCellIndex(WorldPosition + Vector3d(Radius)) );
	GS::EnumerateCellsInRangeInclusive((Vector3i)MinBoxIndex, (Vector3i)MaxBoxIndex, [&](Vector3i IndexValue)
	{
		WorldGridRegionIndex RegionIndex(IndexValue);
		AxisBox3d WorldBounds = GetRegionWorldBounds(RegionIndex);
		if (IsBoundsInRadius(WorldBounds, WorldPosition, Radius))
			RegionFunc(RegionIndex);
	});
}


AxisBox3d WorldGridDB::GetRegionWorldBounds(const WorldGridRegionIndex& RegionIndex) const
{
	WorldGridCellIndex MinCellIndex = ToCellIndex(RegionIndex, Vector3i::Zero());
	// we want the topmost cell in the block, why are we doing it this weird way??
	//Vector3i MaxCellIndex = ToCellIndex(RegionIndex+Vector3i::One(), Vector3i::Zero()) - Vector3i::One();
	WorldGridCellIndex MaxCellIndex = ToCellIndex(RegionIndex, ModelGrid::ModelGridDimensions() - Vector3i::One());
	AxisBox3d Box = GetCellBoundingBox(MinCellIndex);
	Box.Contain(GetCellBoundingBox(MaxCellIndex));
	return Box;
}
AxisBox3i WorldGridDB::GetRegionIndexRange(const WorldGridRegionIndex& RegionIndex) const
{
	WorldGridCellIndex MinCellIndex = ToCellIndex(RegionIndex, Vector3i::Zero());
	return AxisBox3i((Vector3i)MinCellIndex, (Vector3i)MinCellIndex + ModelGrid::ModelGridDimensions() - Vector3i::One());
}


bool WorldGridDB::QueryCellIndex(const WorldGridCellIndex& CellIndex, ModelGridCell& CellDataOut) const
{
	CellDataOut = ModelGridCell();

	WorldGridRegionIndex RegionIndex; Vector3i ModelGridCellIndex;
	CellIndexToRegionAndBlockCellIndex(CellIndex, RegionIndex, ModelGridCellIndex, true);

	bool bIsInGrid = false;
	ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo)
	{
		Vector3i ModelGridBlockIndex = RegionGrid.GetChunkIndexForKey(ModelGridCellIndex);
		if (ExtendedInfo.BlockStates.Get(ModelGridBlockIndex).IsEmpty == 1) 
			return;		// block is known to be empty
		CellDataOut = RegionGrid.GetCellInfo_Safe(ModelGridCellIndex, bIsInGrid);
	});
	return bIsInGrid;
}


void WorldGridDB::EnumerateOccupiedColumnBlocks(const WorldGridRegionIndex& RegionIndex, const Vector2i& ColumnIndex,
	FunctionRef<void(Vector3i)> BlockFunc) const
{
	ProcessRegion_Blocking(RegionIndex, [&](const ModelGrid& RegionGrid, const WorldRegionModelGridInfo& ExtendedInfo)
	{
		// TODO: could we generalize ExtendedInfo.BlockStates to keep track of which blocks are occupied? 
		// this would save accessing RegionGrid here and maybe avoid locking...
		// could keep count of occupied cells in each block, instead of just a flag...

		// not checking ExtendedInfo.BlockStates here - is it right?
		RegionGrid.EnumerateOccupiedColumnBlocks(ColumnIndex, BlockFunc);
	});
}



void WorldGridDB::EnumerateLoadedRegions_Blocking(FunctionRef<void(WorldGridRegionIndex RegionIndex, const AxisBox3d& RegionBounds)> ApplyFunc) const
{
	std::scoped_lock sync_loads_lock(high_level_load_lock);

	// have to lock entire allocated list here to iterate over it
	AllocatedLock.lock();

	for (const RegionContainerPtr& Container : AllocatedRegions)
	{
		ApplyFunc( Container->RegionIndex, GetRegionWorldBounds(Container->RegionIndex));
	}

	AllocatedLock.unlock();
}


bool WorldGridDB::ProcessRegion_Blocking(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(const ModelGrid&, const WorldRegionModelGridInfo&)> ProcessFunc) const
{
	return Internal_ProcessRegion_Blocking(RegionIndex, ProcessFunc, /*bWaitForPendingLoad=*/true);
}

bool WorldGridDB::Internal_ProcessRegion_Blocking(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(const ModelGrid&, const WorldRegionModelGridInfo&)> ProcessFunc, bool bWaitForPendingLoad) const
{
	return ProcessWorldRegion_Safe(RegionIndex, [&](const WorldRegionData& Data)
	{
		ProcessFunc(Data.Grid, Data.GridInfo);
	}, bWaitForPendingLoad);
}

bool WorldGridDB::EditRegion_Blocking(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(ModelGrid&, WorldRegionModelGridInfo&)> EditFunc)
{
	return Internal_EditRegion_Blocking(RegionIndex, EditFunc, /*bMarkRegionAsEdited*/true, /*bWaitForPendingLoad=*/true);
}

bool WorldGridDB::Internal_EditRegion_Blocking(const WorldGridRegionIndex& RegionIndex,
	FunctionRef<void(ModelGrid& RegionGrid, WorldRegionModelGridInfo& ExtendedInfo)> EditFunc,
	bool bMarkRegionAsEdited, bool bWaitForPendingLoad)
{
	RegionContainerPtr ContainerPtr = GetRegion_Safe(RegionIndex);
	if (!ContainerPtr)
		return false;

	while (bWaitForPendingLoad && ContainerPtr->bIsLoadPending)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

	ContainerPtr->region_lock.lock();
	ContainerPtr->bPossiblyModified = bMarkRegionAsEdited;
	EditFunc(ContainerPtr->Data->Grid, ContainerPtr->Data->GridInfo);
	ContainerPtr->region_lock.unlock();

	return true;
}
