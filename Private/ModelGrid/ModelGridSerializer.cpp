// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridSerializer.h"
#include "ModelGrid/ModelGridAdapter.h"
#include "ModelGrid/ModelGridInternals.h"
#include "Core/gs_serialize_utils.h"

using namespace GS;
using namespace GS::ModelGridInternal;


bool ModelGridSerializer::Serialize(const ModelGrid& Grid, GS::ISerializer& Serializer)
{
	GS::SerializationVersion CurrentVersion(ModelGridVersions::CurrentVersionNumber);
	bool bOK = Serializer.WriteVersion(SerializeVersionString(), CurrentVersion);

	return bOK && ModelGridSerializer::Serialize_V3(Grid, Serializer);
}

bool ModelGridSerializer::Restore(ModelGrid& Grid, GS::ISerializer& Serializer)
{
	GS::SerializationVersion StoredVersion;
	bool bOK = Serializer.ReadVersion(SerializeVersionString(), StoredVersion);

	if (StoredVersion.Version <= ModelGridVersions::Version2)
		return bOK && Restore_V1V2(Grid, Serializer, (StoredVersion.Version == ModelGridVersions::Version1));

	return bOK && ModelGridSerializer::Restore_V3(Grid, Serializer);
}


struct BlockHeader
{
	Vector3i BlockIndex;
	int Flags = 0;		// adding this for future use
};

bool ModelGridSerializer::Serialize_V3(const ModelGrid& Grid, GS::ISerializer& Serializer)
{
	return Serialize_V1(Grid, Serializer);
}
bool ModelGridSerializer::Serialize_V2(const ModelGrid& Grid, GS::ISerializer& Serializer)
{
	return Serialize_V1(Grid, Serializer);
}
bool ModelGridSerializer::Serialize_V1(const ModelGrid& Grid, GS::ISerializer& Serializer)
{
	bool bOK = true;

	// store all POD fields
	bOK = bOK && Serializer.WriteValue<Vector3d>("CellDimensions", Grid.CellDimensions);
	bOK = bOK && Serializer.WriteValue<Vector3i>("MinCoordCorner", Grid.MinCoordCorner);
	bOK = bOK && Serializer.WriteValue<AxisBox3i>("CellIndexBounds", Grid.CellIndexBounds);
	bOK = bOK && Serializer.WriteValue<AxisBox3i>("AllocatedChunkBounds", Grid.AllocatedChunkBounds);
	bOK = bOK && Serializer.WriteValue<AxisBox3i>("ModifiedKeyBounds", Grid.ModifiedKeyBounds);

	// store block and index-grid dimensions in case we have to recover later
	Vector3i BlockDimensions = ModelGrid::Block_CellType::TypeDimensions();
	bOK = bOK && Serializer.WriteValue<Vector3i>("BlockDimensions", BlockDimensions);
	Vector3i IndexDimensions = ModelGrid::BlockIndexGrid::TypeDimensions();
	bOK = bOK && Serializer.WriteValue<Vector3i>("IndexDimensions", IndexDimensions);

	// store AllocatedBlocks which can be used to rebuild IndexGrid
	size_t NumAllocatedBlocks = Grid.AllocatedBlocks.size();
	bOK = bOK && Serializer.WriteValue<size_t>("NumAllocatedBlocks", NumAllocatedBlocks);
	char BlockIDString[256];
	for (size_t k = 0; k < NumAllocatedBlocks; ++k)
	{
		const ModelGrid::BlockContainer& BlockInfo = Grid.AllocatedBlocks[(int)k];
		if (BlockInfo.Data == nullptr) continue;

		sprintf_s(BlockIDString, 255, "Block%d", (int)k);
		BlockHeader Header;
		Header.BlockIndex = BlockInfo.BlockIndex;
		bOK = bOK && Serializer.WriteValue<BlockHeader>(BlockIDString, Header);

		uint32_t CompressionType = 1;		// 0 == uncompressed, 1 == simple RLE
		bOK = bOK && Serializer.WriteValue("BlockCompressionType", CompressionType);
		if (CompressionType == 1)
		{
			bOK = bOK && GS::SerializeUtils::store_buffer_rle_compressed(BlockInfo.Data->CellType.Data.get_view(), Serializer, "CellType");
			bOK = bOK && GS::SerializeUtils::store_buffer_rle_compressed(BlockInfo.Data->CellData.Data.get_view(), Serializer, "CellData");
			bOK = bOK && GS::SerializeUtils::store_buffer_rle_compressed(BlockInfo.Data->Material.Data.get_view(), Serializer, "Material");
		}
		else
		{
			bOK = bOK && BlockInfo.Data->CellType.Data.Store(Serializer, "CellType");
			bOK = bOK && BlockInfo.Data->CellData.Data.Store(Serializer, "CellData");
			bOK = bOK && BlockInfo.Data->Material.Data.Store(Serializer, "Material");
		}

		// RLE compression will not work here because each struct has a different parent index - would have to do it at the byte or uint32_t level...
		bOK = bOK && BlockInfo.Data->BlockFaceMaterials.Store(Serializer, "FaceMaterials");
	}

	return bOK;
}





struct ModelGridCellData_StandardRST_Version1
{
	union Parameters {
		struct {
			uint8_t AxisDirection : 3;
			uint8_t AxisRotation : 2;
			uint8_t MirrorX : 1;
			uint8_t DimensionX : 4;
			uint8_t DimensionY : 4;
			uint8_t DimensionZ : 4;
			uint8_t TranslateX : 4;
			uint8_t TranslateY : 4;
			uint8_t TranslateZ : 4;
		};
		uint32_t Fields;
	};
	Parameters Params;
};




template<int BlockSizeXY, int BlockSizeZ, int IndexSizeXY, int IndexSizeZ, int ModelGridVersion>
class ModelGridVariant
{
public:
	static constexpr Vector3i ModelGridDimensions() {
		return Block_CellType::TypeDimensions() * BlockIndexGrid::TypeDimensions();
	}
	static constexpr Vector3i BlockDimensions()	{
		return Block_CellType::TypeDimensions();
	}
	using Block_CellType = FixedGrid3<uint16_t, BlockSizeXY, BlockSizeXY, BlockSizeZ>;
	using Block_CellData = FixedGrid3<uint64_t, BlockSizeXY, BlockSizeXY, BlockSizeZ>;
	using Block_Material = FixedGrid3<uint64_t, BlockSizeXY, BlockSizeXY, BlockSizeZ>;
	struct BlockData
	{
		Block_CellType CellType;
		Block_CellData CellData;
		Block_Material Material;
		GS::unsafe_vector<GS::ModelGridInternal::PackedFaceMaterialsV1> BlockFaceMaterials;
	};
	using BlockIndexGrid = FixedGrid3<uint16_t, IndexSizeXY, IndexSizeXY, IndexSizeZ>;

	BlockIndexGrid IndexGrid;
	struct BlockContainer {
		BlockData* Data;
		Vector3i BlockIndex;
	};
	GS::unsafe_vector<BlockContainer> AllocatedBlocks;

	Vector3d CellDimensions;
	AxisBox3i CellIndexBounds;
	Vector3i MinCoordCorner;
	AxisBox3i AllocatedChunkBounds;
	AxisBox3i ModifiedKeyBounds;

	ModelGridCell EmptyCell = ModelGridCell();
	constexpr static uint16_t UNALLOCATED = Math16u::MaxValue();

	void BuildIndexGrid() {
		IndexGrid.Initialize(UNALLOCATED);
		int NumBlocks = (int)AllocatedBlocks.size();
		for (int k = 0; k < NumBlocks; ++k) {
			BlockContainer& BlockInfo = AllocatedBlocks[k];
			IndexGrid.Set(BlockInfo.BlockIndex, (uint16_t)k);
		}
	}

	const BlockData* ToLocalIfAllocated(const Vector3i& Key, Vector3i& Local) const {
		Vector3i ShiftKey = Key - MinCoordCorner;
		Vector3i IndexIndex = ShiftKey / Block_CellType::TypeDimensions();
		uint16_t StorageIndex = IndexGrid[IndexIndex];
		if (StorageIndex == UNALLOCATED) return nullptr;
		Local = ShiftKey - IndexIndex * Block_CellType::TypeDimensions();
		return AllocatedBlocks[StorageIndex].Data;
	}

	ModelGridCell GetCellData(Vector3i Key, bool& bIsInGrid) const
	{
		Vector3i CellIndex(Key);
		bIsInGrid = CellIndexBounds.Contains(CellIndex);
		if (!bIsInGrid) return EmptyCell;

		Vector3i LocalIndex;
		const BlockData* Data = ToLocalIfAllocated(Key, LocalIndex);
		if (Data == nullptr) return EmptyCell;

		const_buffer_view<PackedFaceMaterialsV1> face_materials = Data->BlockFaceMaterials.get_view();

		int64_t LinearIndex = Data->CellType.ToLinearIndex(LocalIndex);
		return ModelGridInternal::UnpackCellFromPackedDataV1(
			Data->CellType[LinearIndex],
			Data->CellData[LinearIndex],
			Data->Material[LinearIndex],
			face_materials, ModelGridVersion );
	}

	void EditAllCells(FunctionRef<bool(ModelGridCell& Cell)> EditFunc, bool bUpdateCellType, bool bUpdateCellData, bool bUpdateMaterial)
	{
		for (BlockContainer& Container : AllocatedBlocks) {
			const_buffer_view<PackedFaceMaterialsV1> face_materials = Container.Data->BlockFaceMaterials.get_view();
			for (int LinearIndex = 0; LinearIndex < BlockSizeXY * BlockSizeXY * BlockSizeZ; ++LinearIndex) {
				ModelGridCell Cell = ModelGridInternal::UnpackCellFromPackedDataV1(
					Container.Data->CellType[LinearIndex],
					Container.Data->CellData[LinearIndex],
					Container.Data->Material[LinearIndex],
					face_materials, ModelGridVersion);
				auto old_mat_type = Cell.MaterialType;
				if (EditFunc(Cell)) {
					gs_debug_assert(Cell.MaterialType == old_mat_type);
					if (bUpdateCellType)
						Container.Data->CellType[LinearIndex] = (uint16_t)Cell.CellType;
					if (bUpdateCellData)
						Container.Data->CellData[LinearIndex] = Cell.CellData;
					if (bUpdateMaterial)
						Container.Data->Material[LinearIndex] = Cell.CellMaterial.PackedValue();
				}
			}
		}
	}
};
class ModelGridVersion_V1V2 : public ModelGridVariant<32, 16, 32, 32, ModelGridVersions::Version2>
{
public:
};



class ModelGridVersion_V1V2_Adapter : public IModelGridAdapter
{
public:
	const ModelGridVersion_V1V2* SourceGrid = nullptr;

	ModelGridVersion_V1V2_Adapter(const ModelGridVersion_V1V2* SourceGridIn) { SourceGrid = SourceGridIn; }

	virtual Vector3i GetModelGridDimensions() const override
	{
		return SourceGrid->ModelGridDimensions();
	}
	virtual Vector3d GetCellDimensions() const override
	{
		return SourceGrid->CellDimensions;
	}
	virtual AxisBox3i GetCellIndexRange() const override
	{
		return SourceGrid->CellIndexBounds;
	}
	virtual ModelGridCell GetCellAtIndex(Vector3i CellIndex, bool& bIsInGrid) const override
	{
		return SourceGrid->GetCellData(CellIndex, bIsInGrid);
	}
};



// remap/convert V1 cell params to V2
static uint64_t UpgradeCellData_V1toV2(uint32_t V1CellParams)
{
	ModelGridCellData_StandardRST_Version1 OldParams;
	OldParams.Params.Fields = V1CellParams;

	ModelGridCellData_StandardRST NewParams;
	NewParams.Params.Fields = 0;
	NewParams.Params.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	NewParams.Params.AxisDirection = OldParams.Params.AxisDirection;
	NewParams.Params.AxisRotation = OldParams.Params.AxisRotation;

	NewParams.Params.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	NewParams.Params.DimensionX = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.DimensionX, 0u, ModelGridCellData_StandardRST::MaxDimension);
	NewParams.Params.DimensionY = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.DimensionY, 0u, ModelGridCellData_StandardRST::MaxDimension);
	NewParams.Params.DimensionZ = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.DimensionZ, 0u, ModelGridCellData_StandardRST::MaxDimension);

	NewParams.Params.TranslateX = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.TranslateX, 0u, ModelGridCellData_StandardRST::MaxTranslate);
	NewParams.Params.TranslateY = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.TranslateY, 0u, ModelGridCellData_StandardRST::MaxTranslate);
	NewParams.Params.TranslateZ = (uint8_t)GS::Clamp((unsigned int)OldParams.Params.TranslateZ, 0u, ModelGridCellData_StandardRST::MaxTranslate);

	return NewParams.Params.Fields;
}



static bool RestoreGrid_V1V2(ModelGridVersion_V1V2& Grid, GS::ISerializer& Serializer, bool bIsV1)
{
	bool bOK = true;

	// restore all POD fields
	bOK = bOK && Serializer.ReadValue<Vector3d>("CellDimensions", Grid.CellDimensions);
	bOK = bOK && Serializer.ReadValue<Vector3i>("MinCoordCorner", Grid.MinCoordCorner);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("CellIndexBounds", Grid.CellIndexBounds);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("AllocatedChunkBounds", Grid.AllocatedChunkBounds);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("ModifiedKeyBounds", Grid.ModifiedKeyBounds);

	Vector3i BlockDimensions, IndexDimensions;
	bOK = bOK && Serializer.ReadValue<Vector3i>("BlockDimensions", BlockDimensions);
	bOK = bOK && Serializer.ReadValue<Vector3i>("IndexDimensions", IndexDimensions);

	size_t NumAllocatedBlocks = 0;
	bOK = bOK && Serializer.ReadValue<size_t>("NumAllocatedBlocks", NumAllocatedBlocks);
	if (NumAllocatedBlocks > 0)
	{
		Grid.AllocatedBlocks.resize(NumAllocatedBlocks);
		char BlockIDString[256];
		for (size_t k = 0; k < NumAllocatedBlocks; ++k)
		{
			ModelGridVersion_V1V2::BlockContainer& BlockInfo = Grid.AllocatedBlocks[(int)k];

			sprintf_s(BlockIDString, 255, "Block%d", (int)k);
			BlockHeader Header;
			bOK = bOK && Serializer.ReadValue<BlockHeader>(BlockIDString, Header);
			BlockInfo.BlockIndex = Header.BlockIndex;

			BlockInfo.Data = new ModelGridVersion_V1V2::BlockData();		// todo ModelGrid needs to do this allocation?

			bOK = bOK && BlockInfo.Data->CellType.Data.Restore(Serializer, "CellType");
			if (bIsV1)
			{
				// v1 stored celldata as 32-bit "flags", load that into lower 32-bits
				FixedGrid3<uint32_t, 32, 32, 16> V1Flags;
				bOK = bOK && V1Flags.Data.Restore(Serializer, "Flags");
				if (bOK) {
					BlockInfo.Data->CellData.Initialize();
					BlockInfo.Data->CellData.SetFromMapped([&](int64_t LinearIndex) {
						return UpgradeCellData_V1toV2(V1Flags[LinearIndex]);
					});
				}
			}
			else
			{
				bOK = bOK && BlockInfo.Data->CellData.Data.Restore(Serializer, "CellData");
			}
			bOK = bOK && BlockInfo.Data->Material.Data.Restore(Serializer, "Material");
			// write vector for face materials data structures
			bOK = bOK && BlockInfo.Data->BlockFaceMaterials.Restore(Serializer, "FaceMaterials");
		}
	}

	if (bOK) Grid.BuildIndexGrid();

	return bOK;
}


bool ModelGridSerializer::Restore_V1V2(ModelGrid& Grid, GS::ISerializer& Serializer, bool bIsV1)
{
	ModelGridVersion_V1V2 OldGrid;
	if (!RestoreGrid_V1V2(OldGrid, Serializer, bIsV1))
		return false;

	Grid.Initialize(OldGrid.CellDimensions);

	// populate new grid (only what fits)
	AxisBox3i CellIndexRange = Grid.GetCellIndexRange();
	for ( int zi = CellIndexRange.Min.Z; zi <= CellIndexRange.Max.Z; ++zi )
		for (int yi = CellIndexRange.Min.Y; yi <= CellIndexRange.Max.Y; ++yi )
			for (int xi = CellIndexRange.Min.X; xi <= CellIndexRange.Max.X; ++xi)
			{
				bool bIsInGrid;
				ModelGridCell OldCell = OldGrid.GetCellData(Vector3i(xi,yi,zi), bIsInGrid);
				if (OldCell.IsEmpty() == false)
					Grid.ReinitializeCell(Vector3i(xi,yi,zi), OldCell);
			}
	
	return true;
}




bool ModelGridSerializer::Restore_V3(ModelGrid& Grid, GS::ISerializer& Serializer)
{
	bool bOK = true;

	// restore all POD fields
	bOK = bOK && Serializer.ReadValue<Vector3d>("CellDimensions", Grid.CellDimensions);
	bOK = bOK && Serializer.ReadValue<Vector3i>("MinCoordCorner", Grid.MinCoordCorner);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("CellIndexBounds", Grid.CellIndexBounds);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("AllocatedChunkBounds", Grid.AllocatedChunkBounds);
	bOK = bOK && Serializer.ReadValue<AxisBox3i>("ModifiedKeyBounds", Grid.ModifiedKeyBounds);

	Vector3i BlockDimensions, IndexDimensions;
	bOK = bOK && Serializer.ReadValue<Vector3i>("BlockDimensions", BlockDimensions);
	bOK = bOK && Serializer.ReadValue<Vector3i>("IndexDimensions", IndexDimensions);

	size_t NumAllocatedBlocks = 0;
	bOK = bOK && Serializer.ReadValue<size_t>("NumAllocatedBlocks", NumAllocatedBlocks);
	if (NumAllocatedBlocks > 0)
	{
		Grid.AllocatedBlocks.resize(NumAllocatedBlocks);
		char BlockIDString[256];
		for (size_t k = 0; k < NumAllocatedBlocks; ++k)
		{
			ModelGrid::BlockContainer& BlockInfo = Grid.AllocatedBlocks[(int)k];

			sprintf_s(BlockIDString, 255, "Block%d", (int)k);
			BlockHeader Header;
			bOK = bOK && Serializer.ReadValue<BlockHeader>(BlockIDString, Header);
			BlockInfo.BlockIndex = Header.BlockIndex;

			BlockInfo.Data = new ModelGrid::BlockData();		// todo ModelGrid needs to do this allocation?

			uint32_t CompressionType = 0;		// 0 == uncompressed, 1 == simple RLE
			bOK = bOK && Serializer.ReadValue<uint32_t>("BlockCompressionType", CompressionType);
			if (CompressionType == 1)
			{
				bOK = bOK && GS::SerializeUtils::restore_buffer_rle_compressed<uint16_t>(BlockInfo.Data->CellType.Data, Serializer, "CellType");
				bOK = bOK && GS::SerializeUtils::restore_buffer_rle_compressed<uint64_t>(BlockInfo.Data->CellData.Data, Serializer, "CellData");
				bOK = bOK && GS::SerializeUtils::restore_buffer_rle_compressed<uint64_t>(BlockInfo.Data->Material.Data, Serializer, "Material");
			}
			else
			{
				bOK = bOK && BlockInfo.Data->CellType.Data.Restore(Serializer, "CellType");
				bOK = bOK && BlockInfo.Data->CellData.Data.Restore(Serializer, "CellData");
				bOK = bOK && BlockInfo.Data->Material.Data.Restore(Serializer, "Material");
			}

			// read vector for face materials data structures
			bOK = bOK && BlockInfo.Data->BlockFaceMaterials.Restore(Serializer, "FaceMaterials");
		}
	}

	// have grid rebuild data structures we did not serialize directly
	if (bOK) Grid.RebuildAfterRestore();

	return bOK;
}
