// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "ModelGrid/ModelGridCell.h"

namespace GS
{

// Extended version of ModelGridCellData_StandardRST that replaces the 16-bit ExtendedData
// field with four 4-bit parameter fields (ParamA, ParamB, ParamC, ParamD).
// The first 48 bits are identical to ModelGridCellData_StandardRST.
struct GRADIENTSPACEGRID_API ModelGridCellData_StandardRST_Ext
{
	union Parameters
	{
		// WARNING: bit-field element cannot cross byte-boundary, this will insert padding.
		// this struct must (currently) remain 64 bits!
		struct {
			// 8 bits for transform type and rotation (same as StandardRST)
			uint8_t TransformType : 3;
			uint8_t AxisDirection : 3;
			uint8_t AxisRotation : 2;

			// 8 bits for dimension mode and Z dimension (same as StandardRST)
			uint8_t DimensionMode : 2;
			uint8_t ReservedD : 2;
			uint8_t DimensionZ : 4;

			// 8 bits for X/Y dimension (same as StandardRST)
			uint8_t DimensionX : 4;
			uint8_t DimensionY : 4;

			// 8 bits for X translation (same as StandardRST)
			uint8_t TranslateX : 5;
			uint8_t FlipX : 1;
			uint8_t ReservedX : 2;

			// 8 bits for Y translation (same as StandardRST)
			uint8_t TranslateY : 5;
			uint8_t FlipY : 1;
			uint8_t ReservedY : 2;

			// 8 bits for Z translation (same as StandardRST)
			uint8_t TranslateZ : 5;
			uint8_t FlipZ : 1;
			uint8_t ReservedZ : 2;

			// 16 bits: four 4-bit parameter fields (replaces ExtendedData)
			uint8_t ParamA : 4;
			uint8_t ParamB : 4;
			uint8_t ParamC : 4;
			uint8_t ParamD : 4;
		};
		uint64_t Fields;
	};
	Parameters Params;

	static constexpr unsigned int MaxRotationAxis = ModelGridCellData_StandardRST::MaxRotationAxis;
	static constexpr unsigned int MaxRotationAngle = ModelGridCellData_StandardRST::MaxRotationAngle;
	static constexpr unsigned int MaxDimensionMode = ModelGridCellData_StandardRST::MaxDimensionMode;
	static constexpr unsigned int MaxDimension = ModelGridCellData_StandardRST::MaxDimension;
	static constexpr unsigned int MaxTranslate = ModelGridCellData_StandardRST::MaxTranslate;
	static constexpr unsigned int MaxTranslate_Thirds = ModelGridCellData_StandardRST::MaxTranslate_Thirds;
	static constexpr unsigned int MaxParam = 15;

	static bool IsSubType(EModelGridCellType CellType);
};


struct GRADIENTSPACEGRID_API MGCell_VariableCutCorner : public ModelGridCellData_StandardRST_Ext
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::VariableCutCorner_Parametric; }
	static MGCell_VariableCutCorner GetDefaultCellParams();
};

struct GRADIENTSPACEGRID_API MGCell_VariableCutEdge : public ModelGridCellData_StandardRST_Ext
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::VariableCutEdge_Parametric; }
	static MGCell_VariableCutEdge GetDefaultCellParams();
};


} // end namespace GS
