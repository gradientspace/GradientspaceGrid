// Copyright Gradientspace Corp. All Rights Reserved.

#include "ModelGrid/ModelGridEditMachine.h"
#include "GenericGrid/BoxIndexing.h"
#include "Grid/GSGridUtil.h"
#include "Color/GSColorConversion.h"
#include "Core/gs_debug.h"

using namespace GS;





/**
 * tries to find the best orientation that will (1) align the "main" direction of the cell type with the view axis
 * while also (2) keeping the "up" (+Z) direction of the cell aligned with the provided adjacent-cell face normal
 */
static CubeOrientation ComputeBestBlockOrientationFromViewV2(EModelGridCellType CellType, Vector3d ViewVectorInLocalSpace, Vector3d PlacementFaceNormal)
{
	gs_debug_assert(ModelGridCellData_StandardRST::IsSubType(CellType));

	Vector3d NormalToView = -ViewVectorInLocalSpace;

	uint8_t UseAxis = 0, UseRotation = 0;
	double MaxAlignDot = -1.0;

	for (uint8_t Axis = 0; Axis <= ModelGridCellData_StandardRST::MaxRotationAxis; ++Axis)
	{

		bool bIsVertical = (UseAxis == (uint8_t)ECubeOrientationAxis::PositiveZ || UseAxis == (uint8_t)ECubeOrientationAxis::NegativeZ);
		Vector3d BlockAlignDir(0, 0, 1);
		switch (CellType)
		{
			case EModelGridCellType::Slab_Parametric:
			case EModelGridCellType::Pyramid_Parametric:
			case EModelGridCellType::Cylinder_Parametric:
				BlockAlignDir = Vector3d(0, 0, 1); break;
			case EModelGridCellType::Ramp_Parametric:	
				BlockAlignDir = Vector3d(0, 1, 1); break;
			case EModelGridCellType::Corner_Parametric:	
			case EModelGridCellType::CutCorner_Parametric:
				BlockAlignDir = Vector3d(1, 1, 1); break;
			case EModelGridCellType::Peak_Parametric:	
				BlockAlignDir = Vector3d(0, 1, 1); break;		// is this right?
		}
		BlockAlignDir = Normalized(BlockAlignDir);

		for (uint8_t Angle = 0; Angle <= ModelGridCellData_StandardRST::MaxRotationAngle; ++Angle)
		{
			Quaterniond TargetOrientation = (Quaterniond)MakeCubeOrientation(CubeOrientation(Axis, Angle));

			// respect face that we are placing on
			Vector3d UpAlignDir = TargetOrientation * Vector3d::UnitZ();
			if (UpAlignDir.Dot(PlacementFaceNormal) < 0.9)
				continue;

			Vector3d OrientedAlignDir = TargetOrientation * BlockAlignDir;
			double ForwardDot = OrientedAlignDir.Dot(NormalToView);
			if (ForwardDot > MaxAlignDot)
			{
				MaxAlignDot = ForwardDot;
				UseAxis = Axis; UseRotation = Angle;
			}
		}
	}

	return CubeOrientation(UseAxis, UseRotation);
}


static void ApplyRegionFillModeTo2DSelection(ModelGridCellEditSet& SelectedCells, ModelGridEditMachine::ERegionFillMode FillMode, int PlaneAxisIndex)
{
	int NumCells = (int)SelectedCells.Size();
	if (NumCells == 0 || FillMode == ModelGridEditMachine::ERegionFillMode::All)
		return;

	// this is O(N^2) and maybe could be faster? could at least sort?
	// (but lists are generally small...)

	const Vector3i* Nbrs = GS::GetGrid8NeighbourOffsetsPerpToAxis(PlaneAxisIndex);
	ModelGridCellEditSet NewCellSet;
	NewCellSet.ReserveAdditional(NumCells);
	for (int i = 0; i < NumCells; ++i)
	{
		Vector3i Idx = SelectedCells.GetCellIndex(i);
		int NbrCount = 0;
		for (int j = 0; j < 8; ++j)
		{
			Vector3i NbrIdx = Idx + Nbrs[j];
			if ( SelectedCells.ContainsCell(NbrIdx) )
				NbrCount++;
			else
				break;
		}
		if ((FillMode == ModelGridEditMachine::ERegionFillMode::Border && NbrCount < 8)
			|| (FillMode == ModelGridEditMachine::ERegionFillMode::Interior && NbrCount == 8))
			NewCellSet.AppendCell(Idx);
	}

	SelectedCells = std::move(NewCellSet);
}




ModelGridEditMachine::~ModelGridEditMachine()
{
	TargetGrid = nullptr;
	CurrentEditor.release();
}


bool ModelGridEditMachine::BeginTrackedChange()
{
	return CurrentEditor->BeginChange();
}

std::unique_ptr<ModelGridDeltaChange> ModelGridEditMachine::EndTrackedChange()
{
	return CurrentEditor->EndChange();
}

bool ModelGridEditMachine::IsTrackingChange() const
{
	return CurrentEditor->IsTrackingChange();
}

void ModelGridEditMachine::ReapplyChange(const ModelGridDeltaChange& Change, bool bRevert)
{
	CurrentEditor->ReapplyChange(Change, bRevert);

	if (Change.ChangeBounds != AxisBox3i::Empty())
	{
		GridChangeInfo ChangeInfo;
		ChangeInfo.bModified = true;
		ChangeInfo.ModifiedRegion.Contain(Change.ChangeBounds.Min);
		ChangeInfo.ModifiedRegion.Contain(Change.ChangeBounds.Max);

		CurrentAccumChange.AppendChange(ChangeInfo);
		ExternalIncrementalChange.AppendChange(ChangeInfo);
		if (ChangeInfo.bModified)
		{
			OnGridModifiedCallback();
			// emit change somehow...
		}
	}
}


void ModelGridEditMachine::ApplySingleCellUpdate(ModelGrid::CellKey Cell, const ModelGridCell& NewCell)
{
	// this isn't super-efficient but this function should not be getting called that often...
	ModelGridCellEditSet EditSet;
	EditSet.AppendCell(Cell);
	if (MirrorXState.bMirror || MirrorYState.bMirror) {
		EditSet.AppendMirroredCells(MirrorXState, MirrorYState, true);
	}

	GridChangeInfo ChangeInfo;
	ChangeInfo.bModified = false;
	for (const ModelGridCellEditSet::EditCell& EditCellInfo : EditSet.Cells)
	{
		ModelGridCell SetCell = NewCell;
		if (EditCellInfo.bFlipX || EditCellInfo.bFlipY)
			GS::ApplyFlipToCell(SetCell, EditCellInfo.bFlipX, EditCellInfo.bFlipY, false);

		if (CurrentEditor->UpdateCell(EditCellInfo.CellIndex, SetCell)) {
			ChangeInfo.ModifiedRegion.Contain((Vector3i)EditCellInfo.CellIndex);
			ChangeInfo.bModified = true;
		}
	}
	if (!ChangeInfo.bModified)
		return;

	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}

void ModelGridEditMachine::SetCurrentGrid(ModelGrid& Grid)
{
	Initialize(Grid);
}
void ModelGridEditMachine::Initialize(ModelGrid& Grid)
{
	TargetGrid = &Grid;
	CurrentEditor = std::make_unique<ModelGridEditor>(*TargetGrid);

	InitializeLastCellTypeCache();

	CurrentAccumChange = GridChangeInfo();
	ExternalIncrementalChange = GridChangeInfo();
	CurrentCursor = CellCursorState();
}

void ModelGridEditMachine::SetCurrentDrawCellType(EModelGridCellType CellType)
{
	CurrentDrawCellType = CellType;
}

ModelGridCell ModelGridEditMachine::GetCurrentDrawCellPreview(EModelGridCellType CellType, const Vector3d& PlacementFaceNormal) const
{
	int nIndex = (int)CellType;
	if (nIndex < 0 || nIndex >= LastCellTypeCache.size()) {
		return ModelGridCell::EmptyCell();
	}

	ModelGridCell DrawCell = LastCellTypeCache[nIndex];
	bool bIsRSTCell = ModelGridCellData_StandardRST::IsSubType(DrawCell.CellType);
	if (bIsRSTCell && bHaveViewInformation && bAutoOrientPlacedBlocksToCamera)
	{
		Vector3d LocalViewDir = CameraFrame.Z();
		CubeOrientation FindRotation = ComputeBestBlockOrientationFromViewV2(DrawCell.CellType, LocalViewDir, PlacementFaceNormal);
		ModelGridCellData_StandardRST CurrentCellData;
		CurrentCellData.Params.Fields = DrawCell.CellData;
		CurrentCellData.Params.AxisDirection = FindRotation.Direction;
		CurrentCellData.Params.AxisRotation = FindRotation.Rotation;
		DrawCell.CellData = CurrentCellData.Params.Fields;
	}

	return DrawCell;
}


void ModelGridEditMachine::UpdateDrawCellDefaultsForType(const ModelGridCell& Cell)
{
	int nIndex = (int)Cell.CellType;
	if (nIndex < 0 || nIndex >= LastCellTypeCache.size())
		return;

	LastCellTypeCache[nIndex] = Cell;
}


void ModelGridEditMachine::SetCurrentMaterialMode(EMaterialMode MaterialMode)
{
	CurrentMaterialMode = MaterialMode;
}

void ModelGridEditMachine::SetCurrentPrimaryColor(Color3b Color)
{
	CurrentPrimaryColor = Color;
}

void ModelGridEditMachine::SetCurrentSecondaryColor(Color3b Color)
{
	CurrentSecondaryColor = Color;
}

void ModelGridEditMachine::SetCurrentMaterialIndex(uint32_t Index)
{
	CurrentMaterialIndex = Index;
}

void ModelGridEditMachine::SetPaintWithSecondaryColor(bool bEnable)
{
	bPaintWithSecondaryColor = bEnable;
}

void ModelGridEditMachine::SetCurrentBrushParameters(double Extent, EBrushShape BrushShape)
{
	CurrentBrushExtent = Extent;
	CurrentBrushShape = BrushShape;
}

void ModelGridEditMachine::SetCurrentSculptMode(ESculptMode NewMode)
{
	CurrentSculptMode = NewMode;
}

void ModelGridEditMachine::SetCurrentColorModifier(IGridColorModifier* ColorModifier)
{
	CurrentColorModifier = ColorModifier;
}

void ModelGridEditMachine::ClearCurrentColorModifier()
{
	CurrentColorModifier = nullptr;
}


void ModelGridEditMachine::SetActiveDrawPlaneNormal(Vector3d LocalNormal)
{
	Vector3d AxisDots(
		Abs(LocalNormal.Dot(Vector3d::UnitX())),
		Abs(LocalNormal.Dot(Vector3d::UnitY())),
		Abs(LocalNormal.Dot(Vector3d::UnitZ())));
	int MaxAxisIndex = 2;
	if (AxisDots.X > AxisDots.Y && AxisDots.X > AxisDots.Z)
		MaxAxisIndex = 0;
	else if (AxisDots.Y > AxisDots.X && AxisDots.Y > AxisDots.Z)
		MaxAxisIndex = 1;

	this->CurrentDrawPlaneNormal = Vector3i::Zero();
	CurrentDrawPlaneNormal[MaxAxisIndex] = (GS::Sign(LocalNormal[MaxAxisIndex]) >= 0) ? 1 : -1;
	this->CurrentDrawPlaneAxisIndex = MaxAxisIndex;
}

void ModelGridEditMachine::SetEnableAutoOrientPlacedBlocksToView(bool bEnable)
{
	bAutoOrientPlacedBlocksToCamera = bEnable;
}

void ModelGridEditMachine::SetFillLayerSettings(ERegionFillMode FillMode, ERegionFillOperation FillOp, ERegionFillFilter FillFilter)
{
	FillLayer_FillMode = FillMode;
	FillLayer_OpMode = FillOp;
	FillLayer_Filter = FillFilter;
}

void ModelGridEditMachine::SetSymmetryState(const ModelGridAxisMirrorInfo& MirrorX, const ModelGridAxisMirrorInfo& MirrorY)
{
	MirrorXState = MirrorX;
	MirrorYState = MirrorY;
}

void ModelGridEditMachine::SetCurrentCameraFrame(const Frame3d& CameraFrameLocal)
{
	CameraFrame = CameraFrameLocal;
	bHaveViewInformation = true;
}

void ModelGridEditMachine::ClearCurrentCameraFrame()
{
	bHaveViewInformation = false;
}


void ModelGridEditMachine::SetInitialCellCursor(ModelGrid::CellKey Key, const Vector3d& LocalPosition, const Vector3d& LocalNormal)
{
	InitialCursor = CellCursorState{ Key,LocalPosition, LocalNormal };
}

void ModelGridEditMachine::UpdateCellCursor(ModelGrid::CellKey Key)
{
	Vector3d CellCenterPoint = TargetGrid->GetCellLocalBounds(CurrentCursor.CellIndex).Center();
	UpdateCellCursor(Key, CellCenterPoint, Vector3d::UnitZ());
}

void ModelGridEditMachine::UpdateCellCursor(ModelGrid::CellKey Key, const Vector3d& LocalPosition, const Vector3d& LocalNormal)
{
	CurrentCursor = CellCursorState{ Key,LocalPosition, LocalNormal };

	// in a parametric edit, we want to give the effect of a live-update by reverting the previous change
	// (todo: is this the right way? what if we just had a fully separate grid, like a delta-grid, and the meshing takes both into account?)
	if (IsCurrentInteractionParametric())
	{
		if (IsTrackingChange()) {
			GS::AxisBox3i ModifiedRegion = CurrentEditor->RevertInProgressChange();
			if (ModifiedRegion.VolumeCount() > 0) {
				//CurrentAccumChange.AppendChange(ModifiedRegion);   // don't need this as we must have already included it to revert it
				ExternalIncrementalChange.AppendChange(ModifiedRegion);
				// TODO: this will get called twice most of the time, because ProcessCurrentEditCells functions will do it.
				// But not *all* of the time because (eg) we might revert something but then not change anything.
				// Should figure out a better way to do this, eg maybe a different signal based on 
				// the state of ExternalIncrementalChange...
				OnGridModifiedCallback();
			}
		}
	}


	bool bImmediateProcessCells = false;
	switch (CurrentEditState)
	{
	case EditState::SculptCells_Pencil: ComputeEditCellsFromCursor_Pencil(); bImmediateProcessCells = true; break;
	case EditState::SculptCells_Brush2D: ComputeEditCellsFromCursor_Brush2D(); bImmediateProcessCells = true; break;
	case EditState::SculptCells_Brush3D: ComputeEditCellsFromCursor_Brush3D(); bImmediateProcessCells = true; break;
	case EditState::SculptCells_FillLayer: ComputeEditCellsFromCursor_TopLayer(false);  bImmediateProcessCells = true; break;
	case EditState::SculptCells_FloodFillPlanar: ComputeEditCellsFromCursor_FloodFillPlanar(); bImmediateProcessCells = true; break;

	case EditState::SculptCells_FillLayerStack_Parametric: ComputeEditCellsFromCursor_TopLayer(true); bImmediateProcessCells = true; break;
	case EditState::SculptCells_Rectangle2D_Parametric: ComputeEditCellsFromCursor_Rect2D(); bImmediateProcessCells = true; break;

	case EditState::PaintCells_Single: ComputeEditCellsFromCursor_Pencil(); bImmediateProcessCells = true; break;
	case EditState::PaintCells_Brush2D: ComputeEditCellsFromCursor_Brush2D(); bImmediateProcessCells = true; break;
	case EditState::PaintCells_Brush3D: ComputeEditCellsFromCursor_Brush3D(); bImmediateProcessCells = true; break;
	case EditState::PaintCells_FillLayer: ComputeEditCellsFromCursor_TopLayer(false); bImmediateProcessCells = true; break;
	case EditState::PaintCells_FillConnected: ComputeEditCellsFromCursor_AllConnected(); bImmediateProcessCells = true; break;

	case EditState::PaintCells_Rectangle2D_Parametric: ComputeEditCellsFromCursor_Rect2D(); bImmediateProcessCells = true; break;

	case EditState::PaintCellFaces_Single: ComputeEditCellFacesFromCursor_Pencil(); bImmediateProcessCells = true; break;
	}

	if (MirrorXState.bMirror || MirrorYState.bMirror)
		CurrentEditCellSet.AppendMirroredCells(MirrorXState, MirrorYState, true);

	if (bImmediateProcessCells)
	{
		ProcessCurrentEditCells();
	}
}


void ModelGridEditMachine::GetPreviewOfCellEdit(EditState PreviewEditState, 
	ModelGrid::CellKey Key, const Vector3d& LocalPosition, const Vector3d& LocalNormal,
	FunctionRef<void(const ModelGridCellEditSet::EditCell&)> EnumerateTargetCellsCallbackFunc)
{
	// todo implement push/pop for this state?
	CellCursorState SavedInitialCursor = InitialCursor, SavedCurrentCursor = CurrentCursor;
	CurrentCursor = CellCursorState{ Key, LocalPosition, LocalNormal };
	InitialCursor = CellCursorState{ Key, LocalPosition, LocalNormal };

	CurrentEditCellSet.Reset();

	switch (PreviewEditState)
	{
	case EditState::SculptCells_Pencil: ComputeEditCellsFromCursor_Pencil(); break;
	case EditState::SculptCells_Brush2D: ComputeEditCellsFromCursor_Brush2D(); break;
	case EditState::SculptCells_Brush3D: ComputeEditCellsFromCursor_Brush3D(); break;
	case EditState::SculptCells_FillLayer: ComputeEditCellsFromCursor_TopLayer(false); break;
	case EditState::SculptCells_FloodFillPlanar: ComputeEditCellsFromCursor_FloodFillPlanar(); break;

	case EditState::PaintCells_Single: ComputeEditCellsFromCursor_Pencil(); break;
	case EditState::PaintCells_Brush2D: ComputeEditCellsFromCursor_Brush2D(); break;
	case EditState::PaintCells_Brush3D: ComputeEditCellsFromCursor_Brush3D(); break;
	case EditState::PaintCells_FillLayer: ComputeEditCellsFromCursor_TopLayer(false); break;
	case EditState::PaintCells_FillConnected: ComputeEditCellsFromCursor_AllConnected(); break;
	}

	CurrentEditCellSet.EnumerateCells([&](const ModelGridCellEditSet::EditCell& cell) { 
		EnumerateTargetCellsCallbackFunc(cell); 
	});

	// restore state
	CurrentCursor = SavedCurrentCursor;
	InitialCursor = SavedInitialCursor;
}


bool ModelGridEditMachine::BeginStandardEditState(EditState NewState)
{
	gs_debug_assert(CurrentEditState == EditState::NotEditing);
	if (!(CurrentEditState == EditState::NotEditing)) return false;

	CurrentEditState = NewState;
	CurrentEditCellSet.Reset();

	CurrentAccumChange = GridChangeInfo();
	ExternalIncrementalChange = GridChangeInfo();

	return true;
}



bool ModelGridEditMachine::IsInCurrentInteraction() const 
{
	return CurrentEditState != EditState::NotEditing;
}

bool ModelGridEditMachine::EndCurrentInteraction()
{
	if (CurrentEditState == EditState::NotEditing) { return false; }

	// special processing for some states?

	CurrentEditState = EditState::NotEditing;
	return true;
}


bool ModelGridEditMachine::IsCurrentInteractionParametric() const
{
	return (CurrentEditState == EditState::SculptCells_FillLayerStack_Parametric)
		|| (CurrentEditState == EditState::SculptCells_Rectangle2D_Parametric)
		|| (CurrentEditState == EditState::PaintCells_Rectangle2D_Parametric)
		;
}


GridChangeInfo ModelGridEditMachine::GetIncrementalChange(bool bReset)
{
	GridChangeInfo Result = ExternalIncrementalChange;
	if (bReset)
	{
		ExternalIncrementalChange = GridChangeInfo();
	}
	return Result;
}


void ModelGridEditMachine::ProcessCurrentEditCells()
{
	if ((int)CurrentEditState < (int)EditState::BEGIN_PAINT_EDITS)
	{
		if (CurrentSculptMode == ESculptMode::Erase) {
			EraseCurrentEditCells();
		}
		else if (CurrentSculptMode == ESculptMode::Replace) {
			ReplaceCurrentEditCells();
		}
		else {
			FillCurrentEditCells();
		}
	}
	else
	{
		if ((int)CurrentEditState < (int)EditState::BEGIN_PAINT_FACE_EDITS)
		{
			PaintCurrentEditCells();
		}
		else
		{
			PaintCurrentEditCellFaces();
		}
	}
}


void ModelGridEditMachine::FillCurrentEditCells()
{
	ModelGridCell InitCell = LastCellTypeCache[(int)CurrentDrawCellType];
	InitCell.CellType = CurrentDrawCellType;

	bool bCloneIfPossible = (FillLayer_OpMode == ERegionFillOperation::FillByCloningBase);

	bool bIsRSTCell = ModelGridCellData_StandardRST::IsSubType(CurrentDrawCellType);
	if (bIsRSTCell && bHaveViewInformation && bAutoOrientPlacedBlocksToCamera)
	{
		Vector3d LocalViewDir = CameraFrame.Z();
		CubeOrientation FindRotation = ComputeBestBlockOrientationFromViewV2(CurrentDrawCellType, LocalViewDir, CurrentCursor.Normal);
		ModelGridCellData_StandardRST CurrentCellData;
		CurrentCellData.Params.Fields = InitCell.CellData; // MakeDefaultCellFromType(CurrentDrawCellType).CellData;
		CurrentCellData.Params.AxisDirection = FindRotation.Direction;
		CurrentCellData.Params.AxisRotation = FindRotation.Rotation;
		InitCell.CellData = CurrentCellData.Params.Fields;
	}

	InitCell.SetToSolidColor( CurrentPrimaryColor );

	//GridChangeInfo ChangeInfo = CurrentEditor->FillCells(CurrentEditCellSet, InitCell,
	//	[](const ModelGridCell& Cell) { return Cell.IsEmpty() == true; },
	//	[&](const ModelGridCell& ExistingCell, ModelGridCell& NewCell) {
	//		if (CurrentColorModifier != nullptr)
	//			NewCell.SetToSolidColor( CurrentColorModifier->GetPaintColor(CurrentPrimaryColor, CurrentPrimaryColor, ExistingCell) );
	//	});

	auto CellFilterFunc = [](const ModelGridCell& Cell) { return Cell.IsEmpty() == true; };
	auto NewCellModifierFunc = [&](const ModelGridCell& ExistingCell, ModelGridCell& NewCell) {
		if (CurrentColorModifier != nullptr)
			NewCell.SetToSolidColor(CurrentColorModifier->GetPaintColor(CurrentPrimaryColor, CurrentPrimaryColor, ExistingCell));
	};

	GridChangeInfo ChangeInfo;
	for (const ModelGridCellEditSet::EditCell& Cell : CurrentEditCellSet.Cells) {
		ModelGridCell NewCell = InitCell;

		// clone source cell if we have it and want to use it
		if (bCloneIfPossible && Cell.SourceCellIndex != Vector3i::MaxInt()) {
			TargetGrid->GetCellInfoIfValid(Cell.SourceCellIndex, NewCell);
		}

		if (Cell.bFlipX || Cell.bFlipY) {
			GS::ApplyFlipToCell(NewCell, Cell.bFlipX, Cell.bFlipY, false);
		}

		if ( CurrentEditor->FillCell(Cell.CellIndex, NewCell, CellFilterFunc, NewCellModifierFunc) )
			ChangeInfo.AppendChangedCell(Cell.CellIndex);
	}

	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	CurrentEditCellSet.Reset();

	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}

void ModelGridEditMachine::ReplaceCurrentEditCells()
{
	ModelGridCell InitCell = LastCellTypeCache[(int)CurrentDrawCellType];
	InitCell.CellType = CurrentDrawCellType;
	InitCell.SetToSolidColor(CurrentPrimaryColor);

	GridChangeInfo ChangeInfo = CurrentEditor->FillCells(CurrentEditCellSet, InitCell,
		[](const ModelGridCell& Cell) { return true; },
		[&](const ModelGridCell& ExistingCell, ModelGridCell& NewCell) {
			if (CurrentColorModifier != nullptr)
				NewCell.SetToSolidColor( CurrentColorModifier->GetPaintColor(CurrentPrimaryColor, CurrentPrimaryColor, ExistingCell) );
		});
	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	CurrentEditCellSet.Reset();

	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}



void ModelGridEditMachine::EraseCurrentEditCells()
{
	GridChangeInfo ChangeInfo = CurrentEditor->EraseCells(CurrentEditCellSet);
	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	CurrentEditCellSet.Reset();

	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}


void ModelGridEditMachine::PaintCurrentEditCells()
{
	GridChangeInfo ChangeInfo;

	if (CurrentMaterialMode == EMaterialMode::ColorRGB)
	{
		bool bUseScondaryColor = bPaintWithSecondaryColor;
		Color3b PaintColor = (bUseScondaryColor) ? CurrentSecondaryColor : CurrentPrimaryColor;

		if (CurrentColorModifier != nullptr)
		{
			ChangeInfo = CurrentEditor->PaintCells_Complex(CurrentEditCellSet, [&](const ModelGridCell& CellInfo)
			{
				return CurrentColorModifier->GetPaintColor(CurrentPrimaryColor, CurrentSecondaryColor, CellInfo);
			});
		}
		else
		{
			ChangeInfo = CurrentEditor->PaintCells(CurrentEditCellSet, PaintColor);
		}
	}
	else
	{
		ChangeInfo = CurrentEditor->PaintCells(CurrentEditCellSet, (uint8_t)CurrentMaterialIndex);
	}

	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	CurrentEditCellSet.Reset();

	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}



void ModelGridEditMachine::PaintCurrentEditCellFaces()
{
	bool bUseScondaryColor = bPaintWithSecondaryColor;
	Color3b PaintColor3 = (bUseScondaryColor) ? CurrentSecondaryColor : CurrentPrimaryColor;
	// todo expose and support alpha?
	Color4b PaintColor(PaintColor3.R, PaintColor3.G, PaintColor3.B, 255);

	GridChangeInfo ChangeInfo;
	if (CurrentColorModifier != nullptr)
	{
		// TODO complex paint function..
		ChangeInfo = CurrentEditor->PaintCellFaces(CurrentEditCellSet, PaintColor);
	}
	else
	{
		ChangeInfo = CurrentEditor->PaintCellFaces(CurrentEditCellSet, PaintColor);
	}

	CurrentAccumChange.AppendChange(ChangeInfo);
	ExternalIncrementalChange.AppendChange(ChangeInfo);
	CurrentEditCellSet.Reset();

	if (ChangeInfo.bModified)
	{
		OnGridModifiedCallback();
		// emit change somehow...
	}
}



bool ModelGridEditMachine::PickDrawCellFromCursorLocation()
{
	bool bIsInGrid = false;
	ModelGridCell CellInfo = TargetGrid->GetCellInfo(CurrentCursor.CellIndex, bIsInGrid);
	if (bIsInGrid && CellInfo.CellType != EModelGridCellType::Empty)
	{
		CurrentDrawCellType = CellInfo.CellType;
		CurrentPrimaryColor = CellInfo.CellMaterial.AsColor3b();
		LastCellTypeCache[(int)CurrentDrawCellType].CellData = CellInfo.CellData;
		return true;
	}
	return false;
}

void ModelGridEditMachine::InitializeLastCellTypeCache()
{
	LastCellTypeCache.clear();

	int CacheSize = (int)EModelGridCellType::MaxKnownCellType;
	LastCellTypeCache.resize(CacheSize);
	for (int k = 0; k < CacheSize; ++k)
	{
		ModelGridCell Cell;
		Cell.CellType = (EModelGridCellType)k;
		LastCellTypeCache[k] = Cell;
	}
	LastCellTypeCache[(int)EModelGridCellType::Slab_Parametric] = MakeDefaultCell<MGCell_Slab>();
	LastCellTypeCache[(int)EModelGridCellType::Ramp_Parametric] = MakeDefaultCell<MGCell_Ramp>();
	LastCellTypeCache[(int)EModelGridCellType::Corner_Parametric] = MakeDefaultCell<MGCell_Corner>();
	LastCellTypeCache[(int)EModelGridCellType::CutCorner_Parametric] = MakeDefaultCell<MGCell_CutCorner>();
	LastCellTypeCache[(int)EModelGridCellType::Pyramid_Parametric] = MakeDefaultCell<MGCell_Pyramid>();
	LastCellTypeCache[(int)EModelGridCellType::Peak_Parametric] = MakeDefaultCell<MGCell_Peak>();
	LastCellTypeCache[(int)EModelGridCellType::Cylinder_Parametric] = MakeDefaultCell<MGCell_Cylinder>();

}








void ModelGridEditMachine::ComputeEditCellsFromCursor_Pencil()
{
	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex);
}

void ModelGridEditMachine::ComputeEditCellsFromCursor_Brush2D()
{
	double DistScale = GS::Max(TargetGrid->CellSize().X, TargetGrid->CellSize().Y);
	Vector3d CenterPointXY = TargetGrid->GetCellLocalBounds(CurrentCursor.CellIndex).Center();
	CenterPointXY[CurrentDrawPlaneAxisIndex] = 0;

	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex);
	Vector3i BrushCenter = (Vector3i)CurrentCursor.CellIndex;
	int IntBrushRadius = (int)CurrentBrushExtent;
	Vector3i BrushBoundsExtent(IntBrushRadius, IntBrushRadius, IntBrushRadius);
	BrushBoundsExtent[CurrentDrawPlaneAxisIndex] = 0;
	if (CurrentBrushShape == EBrushShape::Square)
	{
		TargetGrid->EnumerateAdjacentCells(BrushCenter, BrushBoundsExtent, false,
			[&](ModelGrid::CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo) { CurrentEditCellSet.AppendCell(Key); });
	}
	else
	{
		// todo just use a DDA circle here? ...
		double BrushRadiusF = DistScale * CurrentBrushExtent;
		TargetGrid->EnumerateAdjacentCells(BrushCenter, BrushBoundsExtent, false,
			[&](ModelGrid::CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo) { 

				AxisBox3d LocalBounds = TargetGrid->GetCellLocalBounds(Key);
				int NumInside = 0;
				for (int k = 0; k <= 3; ++k)
				{
					Vector3d BoxCornerXY = LocalBounds.BoxCorner(k);
					BoxCornerXY[CurrentDrawPlaneAxisIndex] = 0;
					if ( Distance(BoxCornerXY, CenterPointXY) < BrushRadiusF) NumInside++;
				}
				if (NumInside >= 2)
					CurrentEditCellSet.AppendCell(Key);
			});
	}
}

void ModelGridEditMachine::ComputeEditCellsFromCursor_Brush3D()
{
	Vector3d CenterPoint = TargetGrid->GetCellLocalBounds(CurrentCursor.CellIndex).Center();
	double DistScale = ((Vector3d)TargetGrid->CellSize()).AbsMax();

	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex);
	Vector3i BrushCenter = (Vector3i)CurrentCursor.CellIndex;
	int IntBrushRadius = (int)CurrentBrushExtent;
	if (CurrentBrushShape == EBrushShape::Square)
	{
		TargetGrid->EnumerateAdjacentCells(BrushCenter, Vector3i(IntBrushRadius, IntBrushRadius, IntBrushRadius), false,
			[&](ModelGrid::CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo) { CurrentEditCellSet.AppendCell(Key); });
	}
	else
	{
		double BrushRadiusF = DistScale * CurrentBrushExtent;
		TargetGrid->EnumerateAdjacentCells(BrushCenter, Vector3i(IntBrushRadius, IntBrushRadius, IntBrushRadius), false,
			[&](ModelGrid::CellKey Key, Vector3i Offset, const ModelGridCell& CellInfo) {
				
				AxisBox3d LocalBounds = TargetGrid->GetCellLocalBounds(Key);
				int NumInside = 0;
				for (int k = 0; k <= 7; ++k)
				{
					if (Distance(LocalBounds.BoxCorner(k), CenterPoint)  < BrushRadiusF) NumInside++;
				}
				// 4 also kinda good...
				if (NumInside >= 3)
					CurrentEditCellSet.AppendCell(Key);
			});
	}
}

void ModelGridEditMachine::ComputeEditCellsFromCursor_TopLayer(bool bParametric)
{
	Vector3i FirstLayerCellIndex = (bParametric) ? InitialCursor.CellIndex : CurrentCursor.CellIndex;

	bool bApplyFilter = (FillLayer_Filter != ERegionFillFilter::NoFilter);
	auto CellFilterFunc = [this](Vector3i CellIndex) {
		ModelGridCell Cell; TargetGrid->GetCellInfoIfValid(CellIndex, Cell);
		if (FillLayer_Filter == ERegionFillFilter::OnlySolidCells)
			return Cell.CellType == EModelGridCellType::Filled;
		return true;
	};

	bool bCurrentIsEmpty = TargetGrid->IsCellEmpty(FirstLayerCellIndex);
	if (bCurrentIsEmpty == false)
	{
		CurrentEditCellSet.AppendCell(FirstLayerCellIndex);

		// erasing path?
		TargetGrid->EnumerateConnectedPlanarCells(FirstLayerCellIndex, CurrentDrawPlaneAxisIndex,
			[&](ModelGrid::CellKey KeyFrom, ModelGrid::CellKey KeyTo) 
			{ 
				ModelGrid::CellKey AboveKey(KeyTo + CurrentDrawPlaneNormal);
				return TargetGrid->IsCellEmpty(AboveKey) || TargetGrid->IsValidCell(AboveKey) == false;
			},
			[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo) 
			{ 
				if ( bApplyFilter == false || CellFilterFunc(Key) )
					CurrentEditCellSet.AppendCell(Key);
			}, true);
	}
	else
	{
		// layer-filling an empty cell, so we look at the layer below, and fill all valid connected 
		// cells in that layer, where 'valid' means cell above is also empty

		Vector3i BelowKey = FirstLayerCellIndex - CurrentDrawPlaneNormal;
		if (TargetGrid->IsCellEmpty(BelowKey) == false)
		{
			CurrentEditCellSet.AppendCell(FirstLayerCellIndex, BelowKey);

			TargetGrid->EnumerateConnectedPlanarCells(BelowKey, CurrentDrawPlaneAxisIndex,
				[&](ModelGrid::CellKey KeyFrom, ModelGrid::CellKey KeyTo) 
				{ 
					ModelGrid::CellKey AboveKey(KeyTo + CurrentDrawPlaneNormal);
					return TargetGrid->IsCellEmpty(AboveKey) || TargetGrid->IsValidCell(AboveKey) == false;
				},
				[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo) 
				{
					ModelGrid::CellKey AboveKey(Key + CurrentDrawPlaneNormal);
					if (bApplyFilter == false || CellFilterFunc(Key))
						CurrentEditCellSet.AppendCell(AboveKey, Key);
				}, true);
		}
		else
			CurrentEditCellSet.AppendCell(FirstLayerCellIndex);		// we will just fill this one cell...
	}

	ApplyRegionFillModeTo2DSelection(CurrentEditCellSet, FillLayer_FillMode, CurrentDrawPlaneAxisIndex);

	// yikes this is a big hack right here...

	// repeat-fill   (todo improve handling of going back to start position...)
	if (bParametric)
	{
		ModelGridCellEditSet FirstLayerCells = CurrentEditCellSet;
		int N = (int)FirstLayerCells.Size();
		ModelGridCellEditSet& AccumCells = CurrentEditCellSet;
		bool bSkipStartCellLayer = true;

		int idx = CurrentDrawPlaneAxisIndex;
		Vector3i StartCell = FirstLayerCellIndex;
		if (CurrentSculptMode == ESculptMode::Add)
			StartCell -= CurrentDrawPlaneNormal;		// bump start back to initial cell
		Vector3i EndCell = StartCell;
		EndCell[idx] = CurrentCursor.CellIndex[idx];

		int dt = EndCell[idx] - StartCell[idx];
		bool positive_dir = (dt * CurrentDrawPlaneNormal[idx]) > 0 ? true : false;
		
		if ((int)CurrentEditState < (int)EditState::BEGIN_PAINT_EDITS)
		{
			// in any interactive modification mode, we need to have at least one cell of delta to
			// determine what to do
			if (dt == 0 && CurrentSculptMode != ESculptMode::Replace) {
				CurrentEditCellSet.Reset();
				return;
			}
			if (CurrentSculptMode == ESculptMode::Erase)
			{
				// in erase, we do not want to erase the initial layer if dragging away from it ("positive")
				if (positive_dir) {
					CurrentEditCellSet.Reset();
					StartCell += CurrentDrawPlaneNormal;
					bSkipStartCellLayer = false;
				}
				else {
					EndCell += CurrentDrawPlaneNormal;	// otherwise first step erases two layers, because of <= below
				}
			}
			else if (CurrentSculptMode == ESculptMode::Add)
			{
				// in add, we do not want to add the initial layer if dragging away from it ("negative")
				// (note: really what we want in this case is just 'Replace' behavior....)
				if (!positive_dir) {
					CurrentEditCellSet.Reset();
					StartCell -= CurrentDrawPlaneNormal;
					bSkipStartCellLayer = false;
				}
			}
		}
			
		int MinIdx = GS::Min(StartCell[idx], EndCell[idx]);
		int MaxIdx = GS::Max(StartCell[idx], EndCell[idx]);
		for (int j = MinIdx; j <= MaxIdx; ++j)
		{
			int LayerIdx = j;
			if (bSkipStartCellLayer && LayerIdx == StartCell[idx])
				continue;		// these cells are already in the output list
			for (int k = 0; k < N; ++k) {
				ModelGridCellEditSet::EditCell CopyCell = FirstLayerCells.GetCell(k);
				CopyCell.SourceCellIndex = CopyCell.SourceCellIndex;
				CopyCell.CellIndex[idx] = LayerIdx;
				AccumCells.AppendCell(CopyCell);
			}
		}
	}

}


void ModelGridEditMachine::ComputeEditCellsFromCursor_FloodFillPlanar()
{
	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex);

	bool bCurrentIsEmpty = TargetGrid->IsCellEmpty(CurrentCursor.CellIndex);
	if (bCurrentIsEmpty)
	{
		TargetGrid->EnumerateConnectedPlanarCells(CurrentCursor.CellIndex, CurrentDrawPlaneAxisIndex,
			[&](ModelGrid::CellKey KeyFrom, ModelGrid::CellKey KeyTo)
			{
				return TargetGrid->IsCellEmpty(KeyTo);
			},
			[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo) 
			{ 
			CurrentEditCellSet.AppendCell(Key);
			}, false);
	}
	else
	{
		TargetGrid->EnumerateConnectedPlanarCells(CurrentCursor.CellIndex, CurrentDrawPlaneAxisIndex,
			[&](ModelGrid::CellKey KeyFrom, ModelGrid::CellKey KeyTo)
			{
				return ! TargetGrid->IsCellEmpty(KeyTo);
			},
			[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo) 
			{ 
			CurrentEditCellSet.AppendCell(Key);
			}, true);
	}

}



void ModelGridEditMachine::ComputeEditCellsFromCursor_Rect2D()
{
	Vector3i FirstCornerCellIndex = InitialCursor.CellIndex;
	Vector3i SecondCornerCellIndex = CurrentCursor.CellIndex;
	SecondCornerCellIndex[CurrentDrawPlaneAxisIndex] = FirstCornerCellIndex[CurrentDrawPlaneAxisIndex];

	AxisBox3i CellRange(FirstCornerCellIndex, FirstCornerCellIndex);
	CellRange.Contain(SecondCornerCellIndex);
	GS::EnumerateCellsInRangeInclusive(CellRange.Min, CellRange.Max, [&](Vector3i CellIndex) {
		CurrentEditCellSet.AppendCell(CellIndex);
	});

	//ApplyRegionFillModeTo2DSelection(CurrentEditTargetCells, FillLayer_FillMode, CurrentDrawPlaneAxisIndex);

}


void ModelGridEditMachine::ComputeEditCellsFromCursor_AllConnected()
{
	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex);
	TargetGrid->EnumerateConnectedCells(CurrentCursor.CellIndex,
		[&](ModelGrid::CellKey Key, const ModelGridCell& CellInfo) { CurrentEditCellSet.AppendCell(Key); });
}


void ModelGridEditMachine::ComputeEditCellFacesFromCursor_Pencil()
{
	// todo this needs to be mapped into local cell orientation...
	// currently the mapping is done in ModelGridEditor::PaintCellFaces but by that point
	// we do not know the actual hit normal anymore and this prevents painting weird faces.
	uint32_t FaceIndex = GS::NormalToFaceIndex(CurrentCursor.Normal);

	CurrentEditCellSet.AppendCell(CurrentCursor.CellIndex, (int8_t)FaceIndex, false, false);
}



bool ModelGridEditMachine::BeginExternalEdit()
{
	return BeginStandardEditState(EditState::ExternalEdit);
}
bool ModelGridEditMachine::BeginSculptCells_Pencil()
{
	return BeginStandardEditState(EditState::SculptCells_Pencil);
}
bool ModelGridEditMachine::BeginSculptCells_Brush2D()
{
	return BeginStandardEditState(EditState::SculptCells_Brush2D);
}
bool ModelGridEditMachine::BeginSculptCells_Brush3D()
{
	return BeginStandardEditState(EditState::SculptCells_Brush3D);
}
bool ModelGridEditMachine::BeginSculptCells_FillLayer(bool bParametric)
{
	if (bParametric)
		return BeginStandardEditState(EditState::SculptCells_FillLayerStack_Parametric);
	else
		return BeginStandardEditState(EditState::SculptCells_FillLayer);
}
bool ModelGridEditMachine::BeginSculptCells_FloodFillPlanar()
{
	return BeginStandardEditState(EditState::SculptCells_FloodFillPlanar);
}
bool ModelGridEditMachine::BeginSculptCells_Rect2D()
{
	return BeginStandardEditState(EditState::SculptCells_Rectangle2D_Parametric);
}

bool ModelGridEditMachine::BeginPaintCells_Single()
{
	return BeginStandardEditState(EditState::PaintCells_Single);
}
bool ModelGridEditMachine::BeginPaintCells_Brush2D()
{
	return BeginStandardEditState(EditState::PaintCells_Brush2D);
}
bool ModelGridEditMachine::BeginPaintCells_Brush3D()
{
	return BeginStandardEditState(EditState::PaintCells_Brush3D);
}
bool ModelGridEditMachine::BeginPaintCells_FillLayer()
{
	return BeginStandardEditState(EditState::PaintCells_FillLayer);
}
bool ModelGridEditMachine::BeginPaintCells_FillConnected()
{
	return BeginStandardEditState(EditState::PaintCells_FillConnected);
}
bool ModelGridEditMachine::BeginPaintCells_Rect2D()
{
	return BeginStandardEditState(EditState::PaintCells_Rectangle2D_Parametric);
}
bool ModelGridEditMachine::BeginPaintCellFaces_Single()
{
	return BeginStandardEditState(EditState::PaintCellFaces_Single);
}


void ModelGridEditMachine::InitializeUniformGridAdapter(UniformGridAdapter& Adapter)
{
	Adapter.GetGridCellDimension = [this]() { return TargetGrid->GetCellDimensions(); };

	Adapter.IsValidIndex = [this](const Vector3i& CellIndex) { return TargetGrid->IsValidCell(CellIndex); };

	Adapter.GetGridIndexForPosition = [this](const Vector3d& Position, bool& bIsValidIndexOut) {
		return TargetGrid->GetCellAtPosition(Position, bIsValidIndexOut);
	};

	Adapter.GetCellState = [this](const Vector3i& CellIndex, GenericGridCellState& StateOut) -> bool
	{
		bool bIsInGrid = false;
		ModelGridCell CellInfo = TargetGrid->GetCellInfo(CellIndex, bIsInGrid);
		if (bIsInGrid)
		{
			StateOut.bFilled = (CellInfo.CellType != EModelGridCellType::Empty);
			StateOut.TypeValue = (uint64_t)CellInfo.CellType;
			StateOut.IntValues[0] = (uint64_t)CellInfo.CellData;
			StateOut.Color = CellInfo.CellMaterial.AsColor3b();
		}
		return bIsInGrid;
	};
	
	Adapter.SetCellState = [this](const Vector3i& CellIndex, const GenericGridCellState& NewState, bool& bCellWasModifiedOut) -> bool
	{
		bCellWasModifiedOut = false;
		bool bIsInGrid = false;
		ModelGridCell CurCellInfo = TargetGrid->GetCellInfo(CellIndex, bIsInGrid);
		if (bIsInGrid)
		{
			ModelGridCell NewCellInfo = CurCellInfo;
			NewCellInfo.CellType = (NewState.bFilled) ? EModelGridCellType::Filled : EModelGridCellType::Empty;
			NewCellInfo.SetToSolidColor( NewState.Color );
			if (NewCellInfo != CurCellInfo)
			{
				ApplySingleCellUpdate(CellIndex, NewCellInfo);
				bCellWasModifiedOut = true;
			}
		}
		return bIsInGrid;
	};

}



Color3b RandomizeColorModifier::GetPaintColor(const Color3b& PrimaryColor, const Color3b& SecondaryColor, const ModelGridCell& CurrentCell)
{
	Vector3f BaseLinear = GS::SRGBToLinear(PrimaryColor);
	Vector3f BaseHSV = GS::RGBtoHSV(BaseLinear);

	Vector3f RandomizedHSV = BaseHSV;
	RandomizedHSV.X = (float)GS::FMod(RandomizedHSV.X + RandomHelper.RealInRange(-HueRange, HueRange), 360.0);
	RandomizedHSV.Y = (float)GS::Clamp(RandomizedHSV.Y + RandomHelper.RealInRange(-SaturationRange, SaturationRange), 0.0, 1.0);
	RandomizedHSV.Z = (float)GS::Clamp(RandomizedHSV.Z + RandomHelper.RealInRange(-ValueRange, ValueRange), 0.0, 1.0);
	Vector3f RandomizedLinear = GS::HSVtoRGB(RandomizedHSV);

	return GS::LinearToSRGB(RandomizedLinear);
}





