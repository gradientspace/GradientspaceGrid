// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "WorldGrid/WorldGridDB.h"
#include "WorldGrid/WorldGridInterfaces.h"

#include "Core/unsafe_vector.h"
#include "Core/GSAsync.h"
#include "Core/FunctionRef.h"

#include <mutex>
#include <memory>
#include <vector>
#include <unordered_map>


namespace GS
{

class ModelGridMeshCache;
class WorldGridHistory;


enum EWorldGridMeshCachingPolicy
{
	AlwaysCache = 0,
	NeverCache = 1
};


struct GRADIENTSPACEGRID_API WorldGridParameters
{
	Vector3d CellDimensions = Vector3d(50,50,50);
	bool bTrackHistory = true;
	EWorldGridMeshCachingPolicy CachingPolicy = EWorldGridMeshCachingPolicy::NeverCache;
};


class GRADIENTSPACEGRID_API WorldGridSystem 
	: public IWorldGridDBListener
{
protected:
	WorldGridParameters WorldParamters;
	WorldGridDB GridDB;
	WorldGridMeshSystemAPI* MeshSystemAPI = nullptr;

	std::shared_ptr<IWorldGridStorageAPI> InternalGridStorageAPI = nullptr;
	IWorldGridStorageAPI* ExternalGridStorageAPI = nullptr;
	IWorldGridStorageAPI* GridStorageAPI = nullptr;

public:
	virtual ~WorldGridSystem();

	void Initialize(WorldGridParameters Parameters, WorldGridMeshSystemAPI* ExternalMeshSystemAPI, IWorldGridStorageAPI* ExternalStorageAPI);

	void UpdatePlayerLocation(int PlayerID, const Vector3d& NewLocation);

	void RunWorldCleanupJob();

	WorldGridCellIndex GetCellIndexFromWorldPosition(const Vector3d& WorldPosition) const;
	AxisBox3d GetCellBoundingBox(const WorldGridCellIndex& CellIndex) const;
	bool QueryCellIndex(const WorldGridCellIndex& CellIndex, ModelGridCell& CellDataOut) const;

	AxisBox3d GetRegionWorldBounds(WorldGridRegionHandle Handle) const;

	double GetCurrentLoadingRadius() const { return CurrentLoadingRadius; }

public:
	virtual void TryPlaceBlock_Async(const WorldGridCellIndex& CellIndex, ModelGridCell NewCell);
	virtual void TryPlaceBlocks_Async(const std::vector<WorldGridCellIndex>& CellIndices, const std::vector<ModelGridCell>& NewCells, bool bReplace);
	virtual void TryDeleteBlock_Async(const WorldGridCellIndex& CellIndex);


public:
	const WorldGridDB& DebugAccessDB() const { return GridDB; }

public:
	// IWorldGridDBListener impl
	virtual void OnNewWorldRegionCreated_Async(WorldGridRegionIndex RegionIndex) override;
	virtual void OnWorldRegionDestroyed_Async(WorldGridRegionIndex RegionIndex) override;
	virtual void OnNewModelGridBlocksRequired_Async(const std::vector<WorldGridModelBlockHandle>& BlockHandles, IWorldGridDBListener::ModelGridBlockRequest RequestParams) override;
	virtual void OnNewModelGridBlocksRequired_Immediate(const std::vector<WorldGridModelBlockHandle>& BlockHandles, IWorldGridDBListener::ModelGridBlockRequest RequestParams) override;


protected:
	unsafe_vector<IWorldGridSystemClient*> Clients;
	std::mutex ClientsLock;
public:
	virtual bool RegisterClient(IWorldGridSystemClient* Client);
	virtual bool UnregisterClient(IWorldGridSystemClient* Client);



protected:
	Vector3d CurPlayerLocation;
	WorldGridCellIndex CurPlayerCell;
	bool bCurPlayerLocationValid;

	double CurrentLoadingRadius = 1.0;

	enum class ELiveRegionMode
	{
		Immediate,
		NearField,
		FarField,
		Background
	};

	struct LiveWorldGridRegion
	{
		WorldGridRegionIndex RegionIndex;
		ELiveRegionMode RegionMode;
		std::shared_ptr<GS::IMeshBuilderFactory> MeshFactory;
		std::unique_ptr<ModelGridMeshCache> MeshCache;
	};

	// this is dumb, probably should use a grid that mirrors WorldGridDB...need to extract out the local-grid aspect
	std::unordered_map<WorldGridRegionIndex, std::shared_ptr<LiveWorldGridRegion>> LiveRegions;
	std::mutex LiveRegionsLock;
	void AccessRegion(const WorldGridRegionIndex& RegionIndex, FunctionRef<void(LiveWorldGridRegion&)> ProcessFunc);


	struct MeshUpdateParams
	{
		uint32_t Identifer = 0;
		uint32_t Priority = 0;
	};

	virtual GS::TaskContainer SpawnUpdateMeshesJob_Async(std::shared_ptr<LiveWorldGridRegion> Region, MeshUpdateParams UpdateParams, std::vector<Vector3i>&& ModelGridBlocks);
	virtual void SpawnUpdateMeshJob_Async(WorldGridRegionIndex RegionIndex, GridRegionHandle ModelGridBlockHandle, MeshUpdateParams UpdateParams, bool bForceWait);


	void Test_PopulateAndSpawnMeshJobs(const std::vector<WorldGridModelBlockHandle>& PendingHandles, bool bImmediate);
	void Test_PopulateBlocksAndSpawnMeshJobs(WorldGridRegionIndex RegionIndex, const std::vector<GridRegionHandle>& PendingHandles, bool bImmediate);


	void Test_PopulateBlocks_Blocking(WorldGridRegionIndex RegionIndex, std::shared_ptr<LiveWorldGridRegion> Region, const std::vector<GridRegionHandle>& PendingHandles, std::vector<Vector3i>& ModifiedModelBlocksOut);


protected:
	std::shared_ptr<WorldGridHistory> History;
	std::atomic<bool> bTrackHistory = false;
	std::atomic<bool> bInUndoRedo = false;

public:
	void SetEnableHistory(bool bEnabled);
	void ClearHistory();
	void TryUndo();
	void TryRedo();





public:
	static constexpr uint32_t Priority_ImmediateBlock() { return 1000; }
	static constexpr uint32_t Priority_AdjacentBlock() { return 500; }
	static constexpr uint32_t Priority_FarBlock() { return 10; }

	static constexpr uint32_t Identifier_InitialSpawn() { return 1; }
	static constexpr uint32_t Identifier_AddBlock() { return 10; }
	static constexpr uint32_t Identifier_RemoveBlock() { return 11; }

	static constexpr MeshUpdateParams MeshUpdate_InitialSpawn(uint32_t Priority) { return MeshUpdateParams{ Identifier_InitialSpawn(), Priority }; }
	static constexpr MeshUpdateParams MeshUpdate_AddBlock() { return MeshUpdateParams{ Identifier_AddBlock(), Priority_ImmediateBlock() }; }
	static constexpr MeshUpdateParams MeshUpdate_RemoveBlock() { return MeshUpdateParams{ Identifier_RemoveBlock(), Priority_ImmediateBlock() }; }
};


} // end namespace GS
