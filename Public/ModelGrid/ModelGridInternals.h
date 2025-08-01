// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Color/GSColor3b.h"
#include "Color/GSIntColor4.h"
#include "ModelGrid/ModelGridCell.h"
#include "ModelGrid/ModelGridTypes.h"
#include "Core/buffer_view.h"

//
// This file contains internal types used in ModelGrid, that need to be exposed to
// support (eg) utility functions for serialization, back-compat, etc
//

namespace GS::ModelGridInternal
{

// PackedMaterialInfo elements are stored in uint64_t inside ModelGrid blocks
struct PackedMaterialInfoV1
{
	union
	{
		struct
		{
			uint8_t MaterialType;		// EGridCellMaterialType value

			// 24-bits of color, stored as 3x8 bits. These values could be combined/interpreted in different ways based on MaterialType
			uint8_t ColorR;
			uint8_t ColorG;
			uint8_t ColorB;

			// 32 bits
			uint16_t ExtendedIndex;		// index into external material list, eg the per-block-face material list ModelGrid::BlockData::BlockFaceMaterials
			uint16_t Reserved;			// unused
			// 32 more bits
		};
		uint64_t Data;
	};
	constexpr PackedMaterialInfoV1()
		: MaterialType((int)EGridCellMaterialType::SolidColor),
		ColorR(255), ColorG(255), ColorB(255),
		ExtendedIndex(0xFFFF), Reserved(0) {}
	explicit constexpr PackedMaterialInfoV1(uint64_t DataIn) : Data(DataIn) {}
	constexpr PackedMaterialInfoV1(const Color4b& SolidColor)
		: MaterialType((int)EGridCellMaterialType::SolidColor),
		ColorR(SolidColor.R), ColorG(SolidColor.G), ColorB(SolidColor.B),
		ExtendedIndex(0xFFFF), Reserved(0) {}

	void SetFromRGBIndex(GS::GridMaterial RGBIndexMaterial)
	{
		MaterialType = (uint8_t)EGridCellMaterialType::SolidRGBIndex;
		Color3b Color = RGBIndexMaterial.AsColor3b();
		ColorR = Color.R; ColorG = Color.G; ColorB = Color.B;
		ExtendedIndex = RGBIndexMaterial.GetIndex8();
		Reserved = 0;
	}

	void SetFromRGBA(GS::GridMaterial RGBAMaterial)
	{
		MaterialType = (uint8_t)EGridCellMaterialType::SolidColor;
		Color4b Color = RGBAMaterial.AsColor4b();
		ColorR = Color.R; ColorG = Color.G; ColorB = Color.B;
		ExtendedIndex = Color.A;
		Reserved = 0;
	}

	Color3b CellColor3b() const { return Color3b(ColorR, ColorG, ColorB); }
	//Color4b CellColor4b() const { return Color4b(ColorR, ColorG, ColorB, 255); }
};


// set of per-face materials for a cell
// currently this is 9 uint32_t's...a bit weird
struct PackedFaceMaterialsV1
{
	uint16_t ParentCellIndex = -1;	// index of parent cell that owns this packed material set
	uint16_t Placeholder = 0;		// for future use
	uint32_t FaceMaterials[8];		// 8 RGBA colors, etc - each element is assumed to be a GS::GridMaterial
	GridMaterial operator[](int index) const { return GridMaterial(FaceMaterials[index]); }
};


GRADIENTSPACEGRID_API
ModelGridCell UnpackCellFromPackedDataV1(
	uint16_t CellType, 
	uint64_t CellData, 
	uint64_t CellMaterial,
	const const_buffer_view<PackedFaceMaterialsV1>& PackedMaterialSet,
	uint32_t UsingVersion /* = ModelGridVersions::CurrentVersionNumber */);


} // end namespace GS
