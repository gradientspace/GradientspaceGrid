// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridEditor.h"
#include "Core/gs_debug.h"
#include "GenericGrid/BoxIndexing.h"

using namespace GS;



ModelGridEditor::~ModelGridEditor()
{
	gs_debug_assert(!IsTrackingChange());
	Grid = nullptr;
}

bool ModelGridEditor::BeginChange()
{
	if (IsTrackingChange())
	{
		gs_debug_assert(false);
		return false;
	}

	ActiveChangeTracker = GSMakeUniquePtr<ModelGridDeltaChangeTracker>();
	ActiveChangeTracker->AllocateNewChange();

	return true;
}

UniquePtr<ModelGridDeltaChange> ModelGridEditor::EndChange()
{
	if (!IsTrackingChange())
	{
		return UniquePtr<ModelGridDeltaChange>();
	}

	UniquePtr<ModelGridDeltaChange> ResultChange = ActiveChangeTracker->ExtractChange();
	ActiveChangeTracker.reset();

	if (ResultChange->IsEmpty()) 
		return UniquePtr<ModelGridDeltaChange>();

	return ResultChange;
}

bool ModelGridEditor::IsTrackingChange() const
{
	return !!ActiveChangeTracker;
}

void ModelGridEditor::ReapplyChange(const ModelGridDeltaChange& Change, bool bRevert)
{
	int NumCells = (int)Change.CellKeys.size();
	for (int k = 0; k < NumCells; ++k)
	{
		ModelGrid::CellKey CellKey = Change.CellKeys[k];
		const ModelGridCell& SetCell = (bRevert) ? Change.CellsBefore[k] : Change.CellsAfter[k];
		Grid->ReinitializeCell(CellKey, SetCell);
	}
}


GS::AxisBox3i ModelGridEditor::RevertInProgressChange()
{
	AxisBox3i ModifiedRegion = ActiveChangeTracker->GetCurrentChangeBounds();
	gs_debug_assert(IsTrackingChange());

	// TODO do we need to do it this way? could just access change to revert and then Reset arrays...
	GS::UniquePtr<GS::ModelGridDeltaChange> ActiveChange = ActiveChangeTracker->ExtractChange();
	ReapplyChange(*ActiveChange, true);
	ActiveChangeTracker->AllocateNewChange();
	ActiveChange.reset();

	return ModifiedRegion;
}


bool ModelGridEditor::UpdateCell(ModelGrid::CellKey CellKey, const ModelGridCell& NewCell)
{
	ModelGridCell PrevCell;
	bool bIsValidCell = Grid->ReinitializeCell(CellKey, NewCell, &PrevCell);
	bool bModified = (PrevCell != NewCell);

	if (ActiveChangeTracker && bModified) 
		ActiveChangeTracker->AppendModifiedCell(CellKey, PrevCell, NewCell);
	
	return bModified;
}


bool ModelGridEditor::EraseCell(const Vector3i& CellIndex)
{
	bool bIsInGrid = false;
	ModelGridCell Existing = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || Existing.CellType == EModelGridCellType::Empty)
		return false;
	UpdateCell(CellIndex, ModelGridCell::EmptyCell());
	return true;
}


bool ModelGridEditor::FillCell(const Vector3i& CellIndex, 
	const ModelGridCell& NewCell,
	FunctionRef<bool(const ModelGridCell&)> CellFilterFunc,
	FunctionRef<void(const ModelGridCell& CurCell, ModelGridCell& NewCell)> NewCellModifierFunc)
{
	// todo: this may be cell-type-dependent
	uint32_t FlagsMask = 0xFFFFFFFF;

	bool bIsInGrid = false;
	ModelGridCell Existing = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || CellFilterFunc(Existing) == false)
		return false;

	ModelGridCell ApplyNewCell = NewCell;
	NewCellModifierFunc(Existing, ApplyNewCell);

	if (ApplyNewCell.IsSame(Existing, FlagsMask))
		return false;

	UpdateCell(CellIndex, ApplyNewCell);
	return true;
}


bool ModelGridEditor::PaintCell(const Vector3i& CellIndex, const Color3b& NewColor)
{
	bool bIsInGrid = false;
	ModelGridCell CurCell = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || CurCell.CellType == EModelGridCellType::Empty)
		return false;

	if (CurCell.MaterialType != EGridCellMaterialType::SolidColor || CurCell.CellMaterial.AsColor3b() != NewColor)
	{
		ModelGridCell NewCell = CurCell;
		NewCell.SetToSolidColor(NewColor);
		UpdateCell(CellIndex, NewCell);
		return true;
	}
	return false;
}



bool ModelGridEditor::PaintCell(const Vector3i& CellIndex, uint8_t NewMaterialIndex)
{
	bool bIsInGrid = false;
	ModelGridCell CurCell = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || CurCell.CellType == EModelGridCellType::Empty) 
		return false;

	if (CurCell.MaterialType != EGridCellMaterialType::SolidRGBIndex || CurCell.CellMaterial.GetIndex8() != NewMaterialIndex)
	{
		ModelGridCell NewCell = CurCell;
		if (CurCell.MaterialType == EGridCellMaterialType::SolidColor)
			NewCell.SetToSolidRGBIndex(CurCell.CellMaterial.AsColor3b(), NewMaterialIndex);
		else if (CurCell.MaterialType == EGridCellMaterialType::SolidRGBIndex)
			NewCell.SetToSolidRGBIndex(CurCell.CellMaterial.AsColor3b(), NewMaterialIndex);
		else
			NewCell.SetToSolidRGBIndex(Color3b::Black(), NewMaterialIndex);

		UpdateCell(CellIndex, NewCell);
		return true;
	}
	return false;
}



bool ModelGridEditor::PaintCell_Complex(const Vector3i& CellIndex,
	FunctionRef<Color3b(const ModelGridCell& Cell)> GenerateColorFunc)
{
	bool bIsInGrid = false;
	ModelGridCell CurCell = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || CurCell.CellType == EModelGridCellType::Empty)
		return false;

	Color3b NewColor = GenerateColorFunc(CurCell);
	if (CurCell.MaterialType != EGridCellMaterialType::SolidColor || NewColor != CurCell.CellMaterial.AsColor3b() )
	{
		ModelGridCell NewCell = CurCell;
		NewCell.SetToSolidColor(NewColor);
		UpdateCell(CellIndex, NewCell);
		return true;
	}
	return false;
}


bool ModelGridEditor::PaintCellFace(const Vector3i& CellIndex, uint8_t CellFaceIndex, const Color4b& NewColor)
{
	// these values correspond to BoxIndexing.h indexing
	const int GroupID_PlusX = 0;
	const int GroupID_MinusX = 1;
	const int GroupID_PlusY = 2;
	const int GroupID_MinusY = 3;
	const int GroupID_PlusZ = 4;
	const int GroupID_MinusZ = 5;

	bool bIsInGrid = false;
	ModelGridCell CurCell = Grid->GetCellInfo(CellIndex, bIsInGrid);
	if (bIsInGrid == false || CurCell.CellType == EModelGridCellType::Empty) 
		return false;

	int UseFaceIndex = GS::Min((int)CellFaceIndex, CellFaceMaterials::MaxFaces);

	// todo: FaceIndexToNormal/NormalToFaceIndex should be based off the actual cell type here,
	// this will basically lose non-box-aligned face indexing schemes (eg like cutcorner)

	// tbh this transform should be done when we find the face index, not here...

	// Map UseFaceIndex into the space of the local cell by applying inverse transform.
	// (Possibly need to avoid scaling here...)
	Vector3d FaceNormal = GS::FaceIndexToNormal<double>(UseFaceIndex);
	GS::TransformListd TransformList;
	GS::GetUnitCellTransform(CurCell, Vector3d::One(), TransformList);
	FaceNormal = TransformList.InverseTransformNormal(FaceNormal);
	UseFaceIndex = GS::NormalToFaceIndex(FaceNormal);

	// todo: this should probably be generalized to some utility function (that maybe even does the transform?)
	if (CurCell.CellType == EModelGridCellType::Ramp_Parametric && UseFaceIndex == GroupID_PlusY)
		UseFaceIndex = GroupID_PlusZ;
	else if (CurCell.CellType == EModelGridCellType::Corner_Parametric && (UseFaceIndex == GroupID_PlusY || UseFaceIndex == GroupID_PlusX))
		UseFaceIndex = GroupID_PlusZ;
	else if (CurCell.CellType == EModelGridCellType::Peak_Parametric && UseFaceIndex == GroupID_PlusZ)
		UseFaceIndex = GroupID_PlusY;
	else if (CurCell.CellType == EModelGridCellType::Pyramid_Parametric && UseFaceIndex == GroupID_PlusZ)
		UseFaceIndex = GroupID_PlusY;


	if (CurCell.MaterialType == EGridCellMaterialType::FaceColors
		&& CurCell.FaceMaterials[UseFaceIndex].AsColor4b() == NewColor)
		return false;

	ModelGridCell NewCell = CurCell;

	// if cell was solid, copy solid color to faces
	if (NewCell.MaterialType == EGridCellMaterialType::SolidColor) {
		for (int k = 0; k < CellFaceMaterials::MaxFaces; ++k)
			NewCell.FaceMaterials[k] = CurCell.CellMaterial;
	}

	NewCell.MaterialType = EGridCellMaterialType::FaceColors;
	NewCell.FaceMaterials[UseFaceIndex] = GridMaterial(NewColor);

	UpdateCell(CellIndex, NewCell);
	return true;
}




void ModelGridEditor::FlipX()
{
	unsafe_vector<Vector3i> FlipCells;
	Grid->EnumerateFilledCells([&](Vector3i CellIndex, const ModelGridCell& CellInfo, AxisBox3d LocalBounds) {
		Vector3i FlippedIndex(-(CellIndex.X + 1), CellIndex.Y, CellIndex.Z);
		if (Grid->IsCellEmpty(FlippedIndex) || CellIndex.X >= 0) 
			FlipCells.add(CellIndex);
	});

	for (Vector3i CellIndex : FlipCells)
	{
		bool bIsInGrid = false;
		ModelGridCell CurCell = Grid->GetCellInfo(CellIndex, bIsInGrid);

		Vector3i FlippedIndex(-(CellIndex.X + 1), CellIndex.Y, CellIndex.Z);
		ModelGridCell FlipCell = Grid->GetCellInfo(FlippedIndex, bIsInGrid);
		gs_debug_assert(bIsInGrid);

		if (FlipCell.IsEmpty() == false) {
			UpdateCell(FlippedIndex, CurCell);
			UpdateCell(CellIndex, FlipCell);
		}
		else {
			UpdateCell(FlippedIndex, CurCell);
			EraseCell(CellIndex);
		}
	}
}