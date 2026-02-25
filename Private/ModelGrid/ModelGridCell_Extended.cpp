// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridCell_Extended.h"

using namespace GS;

static_assert(sizeof(GS::ModelGridCellData_StandardRST_Ext) == sizeof(uint64_t), "sizeof(ModelGridCellData_StandardRST_Ext) != sizeof(uint64_t)");

bool ModelGridCellData_StandardRST_Ext::IsSubType(EModelGridCellType CellType)
{
	return CellType == EModelGridCellType::VariableCutCorner_Parametric
		|| CellType == EModelGridCellType::VariableCutEdge_Parametric;
}

MGCell_VariableCutCorner MGCell_VariableCutCorner::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST_Ext::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST_Ext::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	Tmp.ParamA = 7;
	Tmp.ParamB = 7;
	Tmp.ParamC = 7;
	Tmp.ParamD = 0;
	return MGCell_VariableCutCorner{ Tmp };
}

MGCell_VariableCutEdge MGCell_VariableCutEdge::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST_Ext::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST_Ext::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	Tmp.ParamA = 7;
	Tmp.ParamB = 7;
	Tmp.ParamC = 0;
	Tmp.ParamD = 0;
	return MGCell_VariableCutEdge{ Tmp };
}
