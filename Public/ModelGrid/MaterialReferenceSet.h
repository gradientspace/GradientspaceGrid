// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Core/unsafe_vector.h"
#include "ModelGrid/ModelGridCell.h"


namespace GS
{

enum class EMaterialReferenceType
{
	DefaultMaterial = 0,
	ExternalMaterial = 1,
	TextureIndex = 2 
};


struct GRADIENTSPACEGRID_API MaterialReferenceID
{
	union
	{
		struct {
			EMaterialReferenceType MaterialType : 8;
			uint32_t Index : 24;
		};
		uint32_t PackedValues;
	};

	MaterialReferenceID() : PackedValues(0) {}
	explicit MaterialReferenceID(uint32_t InitialValue) : PackedValues(InitialValue) {}
	explicit MaterialReferenceID(EMaterialReferenceType TypeIn, uint32_t IndexIn) : MaterialType(TypeIn), Index(IndexIn) {}
};


class GRADIENTSPACEGRID_API MaterialReferenceSet
{
protected:
	struct MaterialInfo
	{
		uint32_t InternalID = 0;		// this is a MaterialReferenceID in packed form
		uint64_t ExternalID = 0;
	};

	uint32_t ExternalMaterialCounter = 0;
	uint32_t TextureIndexCounter = 0;

	GS::unsafe_vector<MaterialInfo> MaterialSet;

public:

	void Reset()
	{
		ExternalMaterialCounter = 0;
		TextureIndexCounter = 0;
		MaterialSet.clear(false);
	}

	//! returns packed MaterialReferenceID
	uint32_t RegisterExternalMaterial(uint64_t ExternalID = 0, int32_t UseInternalIndex = -1 )
	{
		return RegisterMaterialOfType(EMaterialReferenceType::ExternalMaterial, ExternalMaterialCounter, ExternalID, UseInternalIndex);
	}

	//! returns packed MaterialReferenceID
	uint32_t RegisterTextureIndex(uint64_t ExternalID = 0, int32_t UseInternalIndex = -1)
	{
		return RegisterMaterialOfType(EMaterialReferenceType::TextureIndex, TextureIndexCounter, ExternalID, UseInternalIndex);
	}

protected:

	uint32_t RegisterMaterialOfType(EMaterialReferenceType Type, uint32_t& TypeAllocatedIndexCounter, uint64_t ExternalID, int32_t UseInternalIndex = -1)
	{
		MaterialReferenceID NewMat;
		NewMat.MaterialType = Type;

		bool bUsedProvidedInternalID = false;
		if (UseInternalIndex >= 0)
		{
			if (UseInternalIndex >= (int32_t)TypeAllocatedIndexCounter && UseInternalIndex < (1 << 23))
			{
				NewMat.Index = (uint32_t)UseInternalIndex;
				TypeAllocatedIndexCounter = NewMat.Index + 1;
				bUsedProvidedInternalID = true;
			}
		}
		if (!bUsedProvidedInternalID)
		{
			NewMat.Index = TypeAllocatedIndexCounter++;
		}

		MaterialInfo NewMatInfo;
		NewMatInfo.InternalID = NewMat.PackedValues;
		NewMatInfo.ExternalID = ExternalID;
		MaterialSet.add(NewMatInfo);

		return NewMatInfo.InternalID;
	}

};



class GRADIENTSPACEGRID_API ICellMaterialToIndexMap
{
public:
	virtual ~ICellMaterialToIndexMap() {}
	virtual int GetMaterialID(EGridCellMaterialType MaterialType, GridMaterial Material) = 0;
};

class GRADIENTSPACEGRID_API DefaultCellMaterialMap : public ICellMaterialToIndexMap
{
public:
	virtual int GetMaterialID(EGridCellMaterialType MaterialType, GridMaterial Material) override { return 0; }
};




} // end namespace GS
