// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridMeshCache.h"
#include "ModelGrid/ModelGridCell_Extended.h"
#include "Core/ParallelFor.h"
#include "Core/gs_debug.h"
#include "GenericGrid/BoxIndexing.h"

using namespace GS;


ModelGridMeshCache::ModelGridMeshCache()
{
}

ModelGridMeshCache::~ModelGridMeshCache()
{
	for (auto pair : ChunkMeshes)
	{
		if (pair.second != nullptr)
			delete pair.second;
	}
	ChunkMeshes.clear();
	for (auto pair : ZColumns)
	{
		if (pair.second != nullptr)
			delete pair.second;
	}
	ZColumns.clear();
}


void ModelGridMeshCache::Initialize(Vector3d CellDimensions, IMeshBuilderFactory* BuilderFactory)
{
	gs_debug_assert(bIsInitialized == false);
	MeshBuilderFactory = BuilderFactory;
	MeshBuilder.Initialize(CellDimensions);
	bIsInitialized = true;
}

void ModelGridMeshCache::SetMaterialMap(GS::SharedPtr<ICellMaterialToIndexMap> Mapper)
{
	ActiveMaterialMap = Mapper;
}




void ModelGridMeshCache::UpdateInBounds(const ModelGrid& TargetGrid, const AxisBox3d& LocalBounds, FunctionRef<void(Vector2i)> OnColumnUpdatedFunc)
{
	AxisBox3i ChunkIdxRange = TargetGrid.GetAllocatedChunkRangeBounds(LocalBounds);
	if (ChunkIdxRange.IsValid())
		this->UpdateChunkRange(TargetGrid, ChunkIdxRange, OnColumnUpdatedFunc);
}

void ModelGridMeshCache::UpdateInKeyBounds(const ModelGrid& TargetGrid, const AxisBox3i& IndexRange, FunctionRef<void(Vector2i)> OnColumnUpdatedFunc)
{
	if (!IndexRange.IsValid()) return;

	Vector3i ChunkMin = TargetGrid.GetChunkIndexForKey(IndexRange.Min);
	Vector3i ChunkMax = TargetGrid.GetChunkIndexForKey(IndexRange.Max);
	AxisBox3i ChunkIdxRange(ChunkMin, ChunkMax);
	if (ChunkIdxRange.IsValid())
		UpdateChunkRange(TargetGrid, ChunkIdxRange, OnColumnUpdatedFunc);
}


void ModelGridMeshCache::UpdateChunkRange(const ModelGrid& TargetGrid, const AxisBox3i& ChunkIdxRange, 
	FunctionRef<void(Vector2i)> OnColumnUpdatedFunc)
{
	if (ChunkIdxRange.IsValid() == false) return;
	Vector3i Dims = (Vector3i)ChunkIdxRange.AxisCounts();

	unsafe_vector<Vector3i> UpdateChunks;
	UpdateChunks.reserve( (Dims.X+1) * (Dims.Y+1) * (Dims.Z+1) );

	unsafe_vector<Vector2i> UpdateColumns;

	for (int zi = ChunkIdxRange.Min.Z; zi <= ChunkIdxRange.Max.Z; zi++)
	{
		for (int yi = ChunkIdxRange.Min.Y; yi <= ChunkIdxRange.Max.Y; yi++)
		{
			for (int xi = ChunkIdxRange.Min.X; xi <= ChunkIdxRange.Max.X; xi++)
			{
				Vector3i ChunkIndex(xi, yi, zi);
				if (TargetGrid.IsChunkIndexAllocated(ChunkIndex))
				{
					UpdateChunks.add(ChunkIndex);
					UpdateColumns.add_unique(Vector2i(ChunkIndex.X, ChunkIndex.Y));

					if (ChunkMeshes.contains(ChunkIndex) == false)
					{
						IMeshBuilder* ChunkMesh = MeshBuilderFactory->Allocate();
						ChunkMeshes.insert({ ChunkIndex, ChunkMesh });

						AddNewMeshToColumn( ChunkIndex, ChunkMesh );
					}
				}
			}
		}
	}

	GS::ParallelFor((uint32_t)UpdateChunks.size(), [&](int Index)
	{
		Vector3i ChunkIndex = UpdateChunks[Index];
		auto found_itr = ChunkMeshes.find(ChunkIndex);
		if (found_itr != ChunkMeshes.end())
		{
			IMeshBuilder* FoundMesh = found_itr->second;
			FoundMesh->ResetMesh();
			BuildChunkMeshGeometry(TargetGrid, ChunkIndex, *FoundMesh);
		}
	});

	for (Vector2i v : UpdateColumns)
		OnColumnUpdatedFunc(v);
}



void ModelGridMeshCache::UpdateBlockIndex_Async(const ModelGrid& TargetGrid, Vector3i BlockIndex, Vector2i& UpdatedColumnIndexOut)
{
	// TODO need to somehow make sure we are not processing this block in another thread....
	// maybe keep a grid of per-block atomics? 

	IMeshBuilder* UseChunkMesh = nullptr;

	MeshesLock.lock();
	auto found_itr = ChunkMeshes.find(BlockIndex);
	if (found_itr == ChunkMeshes.end())
	{
		UseChunkMesh = MeshBuilderFactory->Allocate();
		ChunkMeshes.insert({ BlockIndex, UseChunkMesh });
		AddNewMeshToColumn(BlockIndex, UseChunkMesh);
	}
	else
	{
		UseChunkMesh = found_itr->second;
	}
	MeshesLock.unlock();

	UpdatedColumnIndexOut = Vector2i(BlockIndex.X, BlockIndex.Y);

	UseChunkMesh->ResetMesh();

	//ProcessModelGrid([&](const ModelGrid& TargetGrid) {
	//	BuildChunkMeshGeometry(TargetGrid, BlockIndex, *UseChunkMesh);
	//});
	BuildChunkMeshGeometry(TargetGrid, BlockIndex, *UseChunkMesh);
}



bool ModelGridMeshCache::RequireBlockIndex_Async(const ModelGrid& TargetGrid, Vector3i BlockIndex, Vector2i& ColumnIndexOut)
{
	bool bMeshExists = false;
	MeshesLock.lock();
	auto found_itr = ChunkMeshes.find(BlockIndex);
	if (found_itr != ChunkMeshes.end() && found_itr->second->GetTriangleCount() > 0)
		bMeshExists = true;
	MeshesLock.unlock();

	ColumnIndexOut = Vector2i(BlockIndex.X, BlockIndex.Y);
	if (bMeshExists == false)
		UpdateBlockIndex_Async(TargetGrid, BlockIndex, ColumnIndexOut);

	return bMeshExists;
}



void ModelGridMeshCache::AddNewMeshToColumn(Vector3i ChunkIndex, const IMeshBuilder* MeshBuilderIn)
{
	ColumnLock.lock();

	Vector2i ColumnIndex(ChunkIndex.X, ChunkIndex.Y);
	auto found_itr = ZColumns.find(ColumnIndex);
	if (found_itr == ZColumns.end())
	{
		ColumnCache* Cache = new ColumnCache();
		Cache->ColumnIndex = ColumnIndex;

		Cache->ColumnCenter = Vector3d::Zero();
		Cache->ColumnChunks.add(ChunkIndex);
		Cache->ColumnChunkMeshes.add(MeshBuilderIn);
		
		ZColumns.insert({ ColumnIndex, Cache });
	}
	else
	{
		ColumnCache* Found = found_itr->second;
		gs_debug_assert(Found->ColumnChunks.contains(ChunkIndex) == false);
		Found->ColumnChunks.add(ChunkIndex);
		Found->ColumnChunkMeshes.add(MeshBuilderIn);
	}

	ColumnLock.unlock();
}



void ModelGridMeshCache::ExtractFullMesh(IMeshCollector& Collector)
{
	for (const auto& ChunkMeshPair : ChunkMeshes)
		Collector.AppendMesh(ChunkMeshPair.second);
}


void ModelGridMeshCache::ExtractColumnMesh_Async(Vector2i ColumnIndex, IMeshCollector& Collector, bool bReleaseAllMeshes)
{
	// TODO this function is not really thread-safe because the meshes in TempColumnChunkMeshes could
	// be deleted or modified. We should be storing shared pointers to something that has the mesh and
	// a lock, so that even if other places discard this mesh, we can still keep using it here

	unsafe_vector<const IMeshBuilder*> TempColumnChunkMeshes;
	ColumnLock.lock();
	//ColumnCache** Found = ZColumns.Find(ColumnIndex);
	auto found_itr = ZColumns.find(ColumnIndex);
	ColumnCache* Found = nullptr;
	if (found_itr != ZColumns.end()) {
		Found = found_itr->second;
		TempColumnChunkMeshes = Found->ColumnChunkMeshes;
	}
	ColumnLock.unlock();

	if (Found == nullptr) return;

	// TODO: really do need some locking here but for current testing once mesh is generated it is never modified...
	//MeshesLock.lock();
	for (const IMeshBuilder* Mesh : TempColumnChunkMeshes)
		Collector.AppendMesh(Mesh);
	//MeshesLock.unlock();
	
	if (bReleaseAllMeshes)
	{
		MeshesLock.lock();
		ColumnLock.lock();
		// Found above is a pointer into ZColumns, so we have to find it again in case ZColumns was modified in the interim
		found_itr = ZColumns.find(ColumnIndex);
		//Found = ZColumns.Find(ColumnIndex);
		if (found_itr != ZColumns.end())
		{
			Found = found_itr->second;
			for (Vector3i Block : Found->ColumnChunks) {
				delete ChunkMeshes[Block];
				ChunkMeshes.erase(Block);
			}
			ZColumns.erase(ColumnIndex);
		}
		ColumnLock.unlock();
		MeshesLock.unlock();
	}
}


void ModelGridMeshCache::BuildChunkMeshGeometry(const ModelGrid& TargetGrid, Vector3i ChunkIndex, IMeshBuilder& Mesh)
{
	ModelGridMesher::AppendCache Cache;
	MeshBuilder.InitAppendCache(Cache);

	TargetGrid.EnumerateFilledChunkCells(ChunkIndex,
		[&](ModelGrid::CellKey CellKey, const ModelGridCell& CellInfo, const AxisBox3d& LocalBounds)
	{
		// determine cell color...maybe UseMaterials.FaceMaterials can be a pointer?
		ModelGridMesher::CellMaterials UseMaterials;
		UseMaterials.CellType = CellInfo.MaterialType;
		UseMaterials.CellMaterial = CellInfo.CellMaterial;
		if (CellInfo.MaterialType == EGridCellMaterialType::SolidRGBIndex && ActiveMaterialMap)
		{
			int MapMaterialID = ActiveMaterialMap->GetMaterialID(CellInfo.MaterialType, UseMaterials.CellMaterial);
			gs_debug_assert(MapMaterialID < 255);
			UseMaterials.CellMaterial.RGBColorIndex.Index = (uint8_t)MapMaterialID;
		}
		UseMaterials.FaceMaterials = CellInfo.FaceMaterials;
		// TODO handle indexed materials on faces...

		if (CellInfo.CellType == EModelGridCellType::Filled)
		{
			int VisibleFaces = 0;
			for (int k = 0; k < 6; ++k)
			{
				Vector3i NeighbourKey = CellKey + FaceIndexToOffset(k);

				bool bIncludeAsBorderFace = bIncludeAllBlockBorderFaces && (TargetGrid.AreCellsInSameBlock(CellKey, NeighbourKey) == false);

				if (bIncludeAsBorderFace || (TargetGrid.IsCellSolid(NeighbourKey) == false) )
				{
					VisibleFaces |= (1 << k);
				}
			}
			if (VisibleFaces > 0)
			{
				MeshBuilder.AppendBoxFaces(LocalBounds, UseMaterials, VisibleFaces, Mesh, Cache);
			}
		}
		else
		{
			TransformListd TransformSeq;
			GetUnitCellTransform(CellInfo, TargetGrid.CellSize(), TransformSeq);

			if (CellInfo.CellType == EModelGridCellType::Slab_Parametric)
			{
				MeshBuilder.AppendBox(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::Ramp_Parametric)
			{
				MeshBuilder.AppendRamp(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::Corner_Parametric)
			{
				MeshBuilder.AppendCorner(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::CutCorner_Parametric)
			{
				MeshBuilder.AppendCutCorner(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::Pyramid_Parametric)
			{
				MeshBuilder.AppendPyramid(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::Peak_Parametric)
			{
				MeshBuilder.AppendPeak(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::Cylinder_Parametric)
			{
				MeshBuilder.AppendCylinder(LocalBounds, UseMaterials, Mesh, TransformSeq, Cache);
			}
			else if (CellInfo.CellType == EModelGridCellType::VariableCutCorner_Parametric)
			{
				ModelGridCellData_StandardRST_Ext ExtParams;
				InitializeSubCellFromGridCell(CellInfo, ExtParams);
				MeshBuilder.AppendVariableCutCorner(LocalBounds, UseMaterials, Mesh, TransformSeq,
					ExtParams.Params.ParamA, ExtParams.Params.ParamB, ExtParams.Params.ParamC);
			}
			else if (CellInfo.CellType == EModelGridCellType::VariableCutEdge_Parametric)
			{
				ModelGridCellData_StandardRST_Ext ExtParams;
				InitializeSubCellFromGridCell(CellInfo, ExtParams);
				MeshBuilder.AppendVariableCutEdge(LocalBounds, UseMaterials, Mesh, TransformSeq,
					ExtParams.Params.ParamA, ExtParams.Params.ParamB);
			}

		}
	});
}
