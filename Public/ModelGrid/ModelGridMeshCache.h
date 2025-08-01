// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Math/GSIntVector2.h"
#include "Math/GSAxisBox3.h"
#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridMesher.h"
#include "ModelGrid/MaterialReferenceSet.h"
#include "Core/SharedPointer.h"

#include <unordered_map>
#include <mutex>
#include <functional>

namespace GS
{

class GRADIENTSPACEGRID_API ModelGridMeshCache
{
public:
	IMeshBuilderFactory* MeshBuilderFactory = nullptr;
	ModelGridMesher MeshBuilder;

	//! if true, then each ModelGrid block is meshed as a separate grid, ie no occlusion between neighbouring blocks
	bool bIncludeAllBlockBorderFaces = false;

	bool bIsInitialized = false;

public:
	ModelGridMeshCache();
	~ModelGridMeshCache();

	void Initialize(Vector3d CellDimensions, IMeshBuilderFactory* BuilderFactory);
	bool IsInitialized() const { return bIsInitialized; }

	void SetMaterialMap(GS::SharedPtr<ICellMaterialToIndexMap> Mapper);

	void UpdateInBounds(const ModelGrid& TargetGrid, const AxisBox3d& LocalBounds, FunctionRef<void(Vector2i)> OnColumnUpdatedFunc);

	void UpdateInKeyBounds(const ModelGrid& TargetGrid, const AxisBox3i& IndexRange, FunctionRef<void(Vector2i)> OnColumnUpdatedFunc);

	// TODO: this needs to take some kind of object that can thread-safely access a grid block(s)
	void UpdateBlockIndex_Async(const ModelGrid& TargetGrid, Vector3i BlockIndex, Vector2i& UpdatedColumnIndexOut);
	// Ensure block mesh is created. Calls UpdateBlockIndex_Async() if it isn't.
	bool RequireBlockIndex_Async(const ModelGrid& TargetGrid, Vector3i BlockIndex, Vector2i& ColumnIndexOut);


	void ExtractFullMesh(IMeshCollector& Collector);

	void ExtractColumnMesh_Async(Vector2i ColumnIndex, IMeshCollector& Collector, bool bReleaseAllMeshes = false);

protected:
	GS::SharedPtr<ICellMaterialToIndexMap> ActiveMaterialMap;
	
	// todo using map is dumb here...modelgrid chunks are in a fixed grid
	std::unordered_map<Vector3i, IMeshBuilder*> ChunkMeshes;
	std::mutex MeshesLock;

	void BuildChunkMeshGeometry(const ModelGrid& TargetGrid, Vector3i ChunkIndex, IMeshBuilder& Mesh);
	void UpdateChunkRange(const ModelGrid& TargetGrid, const AxisBox3i& ChunkIndexRange,
		FunctionRef<void(Vector2i)> OnColumnUpdatedFunc);
		

	struct ColumnCache
	{
		Vector2i ColumnIndex;
		Vector3d ColumnCenter;
		GS::unsafe_vector<Vector3i> ColumnChunks;
		GS::unsafe_vector<const IMeshBuilder*> ColumnChunkMeshes;
	};
	
	std::unordered_map<Vector2i, ColumnCache*> ZColumns;
	std::mutex ColumnLock;			// if also locking MeshesLock, ColumnLock comes second!

	void AddNewMeshToColumn(Vector3i Index, const IMeshBuilder* MeshBuilder);
};


} // end namespace GS
