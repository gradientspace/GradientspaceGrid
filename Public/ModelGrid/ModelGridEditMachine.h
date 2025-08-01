// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridEditor.h"
#include "ModelGrid/ModelGridEditUtil.h"
#include "GenericGrid/GridAdapter.h"
#include "Math/GSRandom.h"

#include <memory>
#include <vector>

namespace GS
{


class GRADIENTSPACEGRID_API IGridColorModifier
{
public:
	virtual ~IGridColorModifier() {}

	//! Inputs will be SRGB-encoded, output also should be
	virtual Color3b GetPaintColor(
		const Color3b& PrimaryColor,
		const Color3b& SecondaryColor,
		const ModelGridCell& CurrentCell) = 0;
};



//
// TODO: 
//  - move hover support here, just updating current cursor, then be able to preview hover
//  - refactor operations into classes like IGridColorModifier? easier to extend then...
//  - apply color modifier to draw ops
//  - 
//
class GRADIENTSPACEGRID_API ModelGridEditMachine
{
public:
	virtual ~ModelGridEditMachine();

	enum class EditState
	{
		NotEditing,

		SculptCells_Pencil = 100,
		SculptCells_Brush2D = 101,
		SculptCells_Brush3D = 102,
		SculptCells_FillLayer = 103,
		SculptCells_FloodFillPlanar = 104,

		SculptCells_FillLayerStack_Parametric = 120,
		SculptCells_Rectangle2D_Parametric = 121,

		BEGIN_PAINT_EDITS = 200,

		PaintCells_Single = 201,
		PaintCells_Brush2D = 202,
		PaintCells_Brush3D = 203,
		PaintCells_FillLayer = 204,
		PaintCells_FillConnected = 205,

		PaintCells_Rectangle2D_Parametric = 221,

		BEGIN_PAINT_FACE_EDITS = 250,
		PaintCellFaces_Single = 251,

		BEGIN_MISC_EDITS = 500,

		ExternalEdit = 501
	};

	enum class ESculptMode
	{
		Add,
		Replace,
		Erase
	};

	enum class EBrushShape
	{
		Round = 0,
		Square = 1
	};

	enum class EMaterialMode
	{
		ColorRGB,
		MaterialIndex
	};

	enum class ERegionFillMode
	{
		All = 0,
		Border = 1,
		Interior = 2
	};
	enum class ERegionFillOperation
	{
		FillWithCurrentDrawCell = 0,
		FillByCloningBase = 1
	};
	enum class ERegionFillFilter
	{
		NoFilter = 0,
		OnlySolidCells = 1
	};

	void Initialize(ModelGrid& Grid);
	void SetCurrentGrid(ModelGrid& Grid);

	virtual void SetCurrentDrawCellType(EModelGridCellType CellType);
	EModelGridCellType GetCurrentDrawCellType() const { return CurrentDrawCellType; }

	//! returns the Cell parameters that will be drawn on UpdateCellCursor/etc  (ie from last-cell cache/etc)
	virtual ModelGridCell GetCurrentDrawCellPreview(EModelGridCellType CellType, 
		const Vector3d& PlacementFaceNormal = Vector3d::UnitZ()) const;

	//! explicitly update the last-cached-cell state (used for drawing, when not auto-orientating)
	virtual void UpdateDrawCellDefaultsForType(const ModelGridCell& Cell);

	virtual void SetCurrentMaterialMode(EMaterialMode MaterialMode);
	EMaterialMode GetCurrentMaterialMode() const { return CurrentMaterialMode; }

	virtual void SetCurrentPrimaryColor(Color3b Color);
	Color3b GetCurrentPrimaryColor() const { return CurrentPrimaryColor; }

	virtual void SetCurrentSecondaryColor(Color3b Color);
	Color3b GetCurrentSecondaryColor() const { return CurrentSecondaryColor; }

	virtual void SetCurrentMaterialIndex(uint32_t Index);
	int GetCurrentMaterialIndex() const { return CurrentMaterialIndex; }

	virtual void SetCurrentColorModifier(IGridColorModifier* ColorModifier);
	virtual void ClearCurrentColorModifier();

	virtual void SetPaintWithSecondaryColor(bool bEnable);
	bool GetPaintWithSecondaryColor() const { return bPaintWithSecondaryColor; }


	virtual void SetCurrentBrushParameters(double Extent, EBrushShape BrushShape);
	virtual void SetCurrentSculptMode(ESculptMode NewMode);

	virtual void SetActiveDrawPlaneNormal(Vector3d LocalNormal);
	virtual Vector3d GetActiveDrawPlaneNormal() const { return (Vector3d)CurrentDrawPlaneNormal; }

	virtual void SetEnableAutoOrientPlacedBlocksToView(bool bEnable);
	virtual void SetFillLayerSettings(ERegionFillMode FillMode, ERegionFillOperation FillOp, ERegionFillFilter FillFilter);

	virtual void SetSymmetryState(const ModelGridAxisMirrorInfo& MirrorX, const ModelGridAxisMirrorInfo& MirrorY);

	//! +X => right, +Y => up, +Z => forward
	virtual void SetCurrentCameraFrame(const Frame3d& CameraFrameLocal);
	virtual void ClearCurrentCameraFrame();

	//! some operations use an initial position and a current position, use this to set the initial position
	virtual void SetInitialCellCursor(ModelGrid::CellKey Key, const Vector3d& LocalPosition, const Vector3d& LocalNormal);

	//! set the active current cell cursor
	virtual void UpdateCellCursor(ModelGrid::CellKey Key);
	virtual void UpdateCellCursor(ModelGrid::CellKey Key, const Vector3d& LocalPosition, const Vector3d& LocalNormal);

	//! should set a state like BeginExternalEdit() before calling this 
	virtual void ApplySingleCellUpdate(ModelGrid::CellKey Cell, const ModelGridCell& NewCell);

	virtual bool BeginExternalEdit();

	virtual bool BeginSculptCells_Pencil();
	virtual bool BeginSculptCells_Brush2D();
	virtual bool BeginSculptCells_Brush3D();
	virtual bool BeginSculptCells_FillLayer(bool bParametric);
	virtual bool BeginSculptCells_Rect2D();
	virtual bool BeginSculptCells_FloodFillPlanar();

	virtual bool BeginPaintCells_Single();
	virtual bool BeginPaintCells_Brush2D();
	virtual bool BeginPaintCells_Brush3D();
	virtual bool BeginPaintCells_FillLayer();
	virtual bool BeginPaintCells_FillConnected();
	virtual bool BeginPaintCells_Rect2D();

	virtual bool BeginPaintCellFaces_Single();

	virtual bool PickDrawCellFromCursorLocation();

	//! compute list of cells that would be affected if we were to apply a single-cell update to a cell
	//! with the active settings/parameters, and calls the Func argument for each cell.
	//! (weird callback API is used to get around passing vector/etc across DLL boundaries)
	virtual void GetPreviewOfCellEdit(EditState PreviewEditState,
		ModelGrid::CellKey CellIndex, const Vector3d& LocalPosition, const Vector3d& LocalNormal,
		FunctionRef<void(const ModelGridCellEditSet::EditCell&)> EnumerateTargetCellsCallbackFunc);


	virtual bool IsInCurrentInteraction() const;
	virtual bool EndCurrentInteraction();

	//! returns true if we are in a "parametric" interaction, ie one where the
	//! active interaction won't bake in a change until commit
	virtual bool IsCurrentInteractionParametric() const;

	virtual bool BeginTrackedChange();
	virtual std::unique_ptr<ModelGridDeltaChange> EndTrackedChange();
	bool IsTrackingChange() const;
	virtual void ReapplyChange(const ModelGridDeltaChange& Change, bool bRevert);

	virtual GridChangeInfo GetIncrementalChange(bool bReset);

	void InitializeUniformGridAdapter(UniformGridAdapter& Adapter);

	// todo replace w/ something else...
	std::function<void()> OnGridModifiedCallback = []() {};

protected:
	ModelGrid* TargetGrid;
	std::unique_ptr<ModelGridEditor> CurrentEditor;

	EditState CurrentEditState = EditState::NotEditing;
	EModelGridCellType CurrentDrawCellType = EModelGridCellType::Filled;
	EMaterialMode CurrentMaterialMode = EMaterialMode::ColorRGB;
	bool bAutoOrientPlacedBlocksToCamera = false;
	
	//! These colors generally will be SRGB-encoded
	Color3b CurrentPrimaryColor = Color3b::Grey();
	Color3b CurrentSecondaryColor = Color3b::White();
	bool bPaintWithSecondaryColor = false;

	uint32_t CurrentMaterialIndex = 0;
	IGridColorModifier* CurrentColorModifier = nullptr;

	double CurrentBrushExtent = 0;
	EBrushShape CurrentBrushShape = EBrushShape::Round;

	Vector3i CurrentDrawPlaneNormal = Vector3i(0, 0, 1);
	int CurrentDrawPlaneAxisIndex = 2;

	bool bHaveViewInformation = false;
	Frame3d CameraFrame;

	ESculptMode CurrentSculptMode = ESculptMode::Add;

	ERegionFillMode FillLayer_FillMode = ERegionFillMode::All;
	ERegionFillOperation FillLayer_OpMode = ERegionFillOperation::FillWithCurrentDrawCell;
	ERegionFillFilter FillLayer_Filter = ERegionFillFilter::NoFilter;

	ModelGridAxisMirrorInfo MirrorXState;
	ModelGridAxisMirrorInfo MirrorYState;

	virtual bool BeginStandardEditState(EditState NewState);

	struct CellCursorState
	{
		ModelGrid::CellKey CellIndex = Vector3i::Zero();
		Vector3d Position = Vector3d::Zero();
		Vector3d Normal = Vector3d::UnitZ();
	};
	CellCursorState InitialCursor;		// some operations use this initial state
	CellCursorState CurrentCursor;

	ModelGridCellEditSet CurrentEditCellSet;

	GridChangeInfo CurrentAccumChange = GridChangeInfo();
	GridChangeInfo ExternalIncrementalChange = GridChangeInfo();

	virtual void ComputeEditCellsFromCursor_Pencil();
	virtual void ComputeEditCellsFromCursor_Brush2D();
	virtual void ComputeEditCellsFromCursor_Brush3D();
	virtual void ComputeEditCellsFromCursor_TopLayer(bool bParametric);
	virtual void ComputeEditCellsFromCursor_FloodFillPlanar();
	virtual void ComputeEditCellsFromCursor_AllConnected();
	virtual void ComputeEditCellsFromCursor_Rect2D();

	virtual void ComputeEditCellFacesFromCursor_Pencil();

	virtual void ProcessCurrentEditCells();
	virtual void FillCurrentEditCells();
	virtual void ReplaceCurrentEditCells();
	virtual void EraseCurrentEditCells();
	virtual void PaintCurrentEditCells();
	virtual void PaintCurrentEditCellFaces();

	std::vector<ModelGridCell> LastCellTypeCache;
	void InitializeLastCellTypeCache();
};



class GRADIENTSPACEGRID_API RandomizeColorModifier : public IGridColorModifier
{
public:
	double ValueRange = 0.1;
	double SaturationRange = 0.1;
	double HueRange = 15.0;
	GS::RandomStream RandomHelper;
	virtual Color3b GetPaintColor(const Color3b& PrimaryColor, const Color3b& SecondaryColor, const ModelGridCell& CurrentCell) override;
};




} // end namespace GS
