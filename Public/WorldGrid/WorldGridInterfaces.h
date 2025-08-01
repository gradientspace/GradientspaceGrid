// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Math/GSIntVector2.h"
#include "Math/GSIntVector3.h"
#include "Math/GSVector3.h"
#include "ModelGrid/ModelGridTypes.h"
#include "Core/FunctionRef.h"
#include "GenericGrid/GridIndex3.h"
#include "Mesh/GenericMeshAPI.h"

#include <functional>
#include <memory>

namespace GS
{

//! index of a Region/ModelGrid in the WorldGrid
struct WorldGridRegionIndex : GridIndex3<WorldGridRegionIndex>
{
	constexpr WorldGridRegionIndex() : GridIndex3(0, 0, 0) {}
	explicit constexpr WorldGridRegionIndex(Vector3i VecIndex) : GridIndex3(VecIndex) {}
	explicit constexpr WorldGridRegionIndex(int X, int Y, int Z) : GridIndex3(X, Y, Z) {}
};

//! Index of a single cell in the WorldGrid
struct WorldGridCellIndex : GridIndex3<WorldGridCellIndex>
{
	constexpr WorldGridCellIndex() : GridIndex3(0, 0, 0) {}
	explicit constexpr WorldGridCellIndex(Vector3i VecIndex) : GridIndex3(VecIndex) {}
	explicit constexpr WorldGridCellIndex(int X, int Y, int Z) : GridIndex3(X, Y, Z) {}
};

//! handle for a Region/ModelGrid in the WorldGrid
struct GRADIENTSPACEGRID_API WorldGridRegionHandle
{
	WorldGridRegionIndex BlockIndex = WorldGridRegionIndex(Vector3i::Zero());

	bool operator==(const WorldGridRegionHandle& Other) const { return BlockIndex == Other.BlockIndex; }
};

//! handle for a block of a ModelGrid in the WorldGrid
struct GRADIENTSPACEGRID_API WorldGridModelBlockHandle
{
	//! handle for ModelGrid in WorldGrid
	WorldGridRegionHandle WorldRegionHandle;		
	//! handle for Block in ModelGrid
	GridRegionHandle ModelBlockHandle;				

	WorldGridModelBlockHandle() = default;
	WorldGridModelBlockHandle(WorldGridRegionIndex RegionIndex, GridRegionHandle ModelBlockHandleIn)
		: WorldRegionHandle{ RegionIndex }, ModelBlockHandle(ModelBlockHandleIn) {}
};



enum class EWorldGridMeshType
{
	FullRegion = 0,
	RegionColumn = 1,
	RegionBlock = 2
};

//! handle for a mesh chunk of the WorldGrid
// (Currently this is only ever used with ::RegionColumn type and the name/definition doesn't exactly make sense for the other types...)
struct GRADIENTSPACEGRID_API WorldGridMeshColumnHandle
{
	WorldGridRegionHandle RegionHandle;
	EWorldGridMeshType MeshType = EWorldGridMeshType::RegionColumn;

	Vector2i RegionColumnIndex = Vector2i::Zero();
	Vector3i RegionBlockIndex = Vector3i::Zero();

	WorldGridMeshColumnHandle() {}
	WorldGridMeshColumnHandle(WorldGridRegionHandle RegionHandleIn, Vector2i ColumnIndexIn)
		: RegionHandle(RegionHandleIn), MeshType(EWorldGridMeshType::RegionColumn), RegionColumnIndex(ColumnIndexIn) {}
};


/**
 * WorldGridMeshSystemAPI is basically a factory interface for creating mesh stuff that 
 * will be needed by the various parts of the WorldGridSystem. Since all the meshing is
 * currently done via abstract interfaces, the WorldGridSystem and subclasses (ModelGridMeshCache,etc)
 * need some way to create concrete instances. The functions in this API are currently only used
 * by WorldGridSystem, it creates the things and then passes them down to the lower-level components
 */
struct GRADIENTSPACEGRID_API WorldGridMeshSystemAPI
{
	// the grid region index is passed to these functions in case the higher-level system wants to do
	// some kind of caching

	//! create a new IMeshBuilderFactory for a modelgrid
	std::function<std::shared_ptr<GS::IMeshBuilderFactory>(WorldGridRegionIndex)> GetOrCreateMeshBuilderForRegionFunc;

	//! create a new MeshCollector for a grid region 
	std::function<std::shared_ptr<GS::IMeshCollector>(WorldGridRegionIndex)> GetOrCreateMeshAccumulatorForRegionFunc;
};


// WorldGridSystem uses this to pass generated mesh chunks back to higher levels via IWorldGridSystemClient
// The collector is going to be allocated via the WorldGridMeshSystemAPI
struct GRADIENTSPACEGRID_API WorldGridMeshContainer
{
	std::shared_ptr<GS::IMeshCollector> Mesh;
	AxisBox3d MeshBounds;			// exact bounds of the mesh (precomputed)
	AxisBox3d WorldMeshBounds;		// bounds of the mesh in world space
	AxisBox3d WorldRegionBounds;	// bounds of the Region (ie full modelgrid) that contains the mesh
	Vector3d WorldRegionOrigin;		// origin of the Region that contains the mesh (center of Bounds)
	bool bMeshInRegionCoords;		// if true, mesh vertices are positioned relative to modelgrid origin (ie WorldRegionOrigin)
};

// WorldGridSystem passes WorldGridMeshUpdates up to higher levels via IWorldGridSystemClient
struct GRADIENTSPACEGRID_API WorldGridMeshUpdate
{
	WorldGridMeshColumnHandle WorldHandle;
	WorldGridMeshContainer MeshContainer;

	uint32_t Identifier = 0;
	uint32_t ExternalPriority = 0;
};


/**
 * Client of the WorldGridSystem, that is using it for something. 
 */
class GRADIENTSPACEGRID_API IWorldGridSystemClient
{
public:
	virtual ~IWorldGridSystemClient() {}

	//! notify Client that the WorldGridSystem has loaded a region
	virtual void OnGridRegionLoaded_Async(WorldGridRegionHandle Handle) {}
	//! notify Client that the WorldGridSystem has unloaded a region
	virtual void OnGridRegionUnloaded_Async(WorldGridRegionHandle Handle) {}
	//! currently unreferenced by WorldGridSystem?
	virtual void OnGridRegionMeshUpdated_Immediate(WorldGridMeshUpdate MeshUpdate) {}
	//! notify Client that the WorldGridSystem has a mesh update for a region
	virtual void OnGridRegionMeshUpdated_Async(WorldGridMeshUpdate MeshUpdate) {}
	//! notify Client that it needs to wait for some MeshUpdates to occur and process them immediately. 
	//! WorldGridSystem does this for things like the region containing the player
	virtual void OnWaitForPendingRegionMeshUpdates(FunctionRef<bool(const GS::WorldGridMeshUpdate& Update)> FilterFunc) {}
};




class GRADIENTSPACEGRID_API IWorldGridStorageAPI
{
public:
	virtual ~IWorldGridStorageAPI() {}

	virtual bool HasWorldGridRegion(WorldGridRegionIndex RegionIndex, size_t& SizeInBytesOut) const = 0;
	virtual bool FetchWorldGridRegion(WorldGridRegionIndex RegionIndex, uint8_t* DataBufferOut, size_t BufferSizeBytes) = 0;
	virtual void StoreWorldGridRegion(WorldGridRegionIndex RegionIndex, const uint8_t* DataBuffer, size_t NumBytes, bool bTakeOwnership) = 0;
};


} // endnamespace GS





// define std::hash function usable with unordered_map. maybe these should be moved to a separate file to avoid STL header dependency...
namespace std {
	template<>
	struct hash<GS::WorldGridRegionIndex> {
		inline size_t operator()(const GS::WorldGridRegionIndex& GridIndex) const {
			// size_t value = your hash computations over x
			return (size_t)GS::HashVector3(GridIndex.X, GridIndex.Y, GridIndex.Z);
		}
	};
}

