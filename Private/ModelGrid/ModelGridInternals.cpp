// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridInternals.h"

using namespace GS;
using namespace GS::ModelGridInternal;

static_assert(sizeof(GS::ModelGridInternal::PackedMaterialInfoV1) == sizeof(uint64_t), "sizeof(PackedMaterialInfoV1) != sizeof(uint64_t)");



GS::ModelGridCell GS::ModelGridInternal::UnpackCellFromPackedDataV1(
	uint16_t CellType, 
	uint64_t CellData, 
	uint64_t CellMaterial,
	const const_buffer_view<PackedFaceMaterialsV1>& PackedMaterialSet,
	uint32_t UsingVersion )
{
	ModelGridCell Result;
	Result.CellType = (EModelGridCellType)CellType;
	Result.CellData = CellData;

	PackedMaterialInfoV1 MaterialInfo(CellMaterial);
	if ((EGridCellMaterialType)MaterialInfo.MaterialType == EGridCellMaterialType::SolidColor)
	{
		Result.SetToSolidColor(MaterialInfo.CellColor3b());
	}
	else if ((EGridCellMaterialType)MaterialInfo.MaterialType == EGridCellMaterialType::SolidRGBIndex)
	{
		if (UsingVersion < ModelGridVersions::Version3)
		{
			gs_debug_assert(MaterialInfo.ColorR < 255);
			Result.SetToSolidRGBIndex(Color3b::White(), MaterialInfo.ColorR);
		}
		else
		{
			gs_debug_assert(MaterialInfo.ExtendedIndex < 255);
			Result.SetToSolidRGBIndex(MaterialInfo.CellColor3b(), (uint8_t)MaterialInfo.ExtendedIndex);
			//Result.SetToSolidRGBIndex(MaterialInfo.CellColor3b(), 0);
		}
	}
	else if ((EGridCellMaterialType)MaterialInfo.MaterialType == EGridCellMaterialType::FaceColors)
	{
		if (MaterialInfo.ExtendedIndex < PackedMaterialSet.size())
		{
			Result.MaterialType = EGridCellMaterialType::FaceColors;
			const PackedFaceMaterialsV1& FaceMaterials = PackedMaterialSet[MaterialInfo.ExtendedIndex];
			for (int j = 0; j < 8; ++j)
				Result.FaceMaterials.Faces[j] = FaceMaterials[j];
		}
		else
		{
			Result.SetToSolidColor(Color3b::HotPink());
		}
	}
	else
	{
		Result.SetToSolidColor(Color3b::HotPink());
	}

	return Result;
}
