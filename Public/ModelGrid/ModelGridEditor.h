// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridChange.h"
#include "ModelGrid/ModelGridEditUtil.h"

#include "Core/FunctionRef.h"


namespace GS
{

class GRADIENTSPACEGRID_API ModelGridEditor
{
protected:
	ModelGrid* Grid = nullptr;

	UniquePtr<ModelGridDeltaChangeTracker> ActiveChangeTracker;

public:
	ModelGridEditor(ModelGrid& GridIn)
	{
		this->Grid = &GridIn;
	}
	virtual ~ModelGridEditor();

	virtual bool BeginChange();
	virtual UniquePtr<ModelGridDeltaChange> EndChange();
	bool IsTrackingChange() const;
	virtual void ReapplyChange(const ModelGridDeltaChange& Change, bool bRevert);
	virtual AxisBox3i RevertInProgressChange();

	/**
	 * Main cell-edit function, other functions below all call this to actually modify grid cells.
	 * Updates any currently-active Change 
	 * returns false if the cell was not modified (ie was already NewCell)
	 */
	virtual bool UpdateCell(ModelGrid::CellKey Cell, const ModelGridCell& NewCell);

	bool EraseCell(const Vector3i& CellIndex);
	bool FillCell(const Vector3i& CellIndex, const ModelGridCell& NewCell,
		FunctionRef<bool(const ModelGridCell&)> CellFilterFunc,
		FunctionRef<void(const ModelGridCell& CurCell, ModelGridCell& NewCell)> NewCellModifierFunc);

	bool PaintCell(const Vector3i& CellIndex, const Color3b& NewColor);
	bool PaintCell_Complex(const Vector3i& CellIndex, FunctionRef<Color3b(const ModelGridCell& Cell)> GenerateColorFunc);
	bool PaintCell(const Vector3i& CellIndex, uint8_t NewMaterialIndex);
	bool PaintCellFace(const Vector3i& CellIndex, uint8_t CellFaceIndex, const Color4b& NewColor);

	//! flip entire grid in X direction, around X=0 origin (X=0 becomes X=-1)
	void FlipX();

	template<typename EnumerableType>
	GridChangeInfo EraseCells(const EnumerableType& Cells)
	{
		GridChangeInfo Result;
		for (Vector3i CellIndex : Cells) {
			if (EraseCell(CellIndex))
				Result.AppendChangedCell(CellIndex);
		}
		return Result;
	}
	GridChangeInfo EraseCells(const ModelGridCellEditSet& CellEditSet)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (EraseCell(Cell.CellIndex))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}


	template<typename EnumerableType>
	GridChangeInfo FillCells(
		const EnumerableType& Cells,
		const ModelGridCell& NewCell,
		FunctionRef<bool(const ModelGridCell&)> CellFilterFunc,
		FunctionRef<void(const ModelGridCell& CurCell, ModelGridCell& NewCell)> NewCellModifierFunc)
	{
		GridChangeInfo Result;
		for (Vector3i CellIndex : Cells) {
			if (FillCell(CellIndex, NewCell, CellFilterFunc, NewCellModifierFunc))
				Result.AppendChangedCell(CellIndex);
		}
		return Result;
	}
	GridChangeInfo FillCells(
		const ModelGridCellEditSet& CellEditSet,
		const ModelGridCell& NewCell,
		FunctionRef<bool(const ModelGridCell&)> CellFilterFunc,
		FunctionRef<void(const ModelGridCell& CurCell, ModelGridCell& NewCell)> NewCellModifierFunc)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (FillCell(Cell.CellIndex, NewCell, CellFilterFunc, NewCellModifierFunc))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}


	template<typename EnumerableType>
	GridChangeInfo PaintCells(const EnumerableType& Cells, const Color3b& NewColor)
	{
		GridChangeInfo Result;
		for (Vector3i CellIndex : Cells) {
			if (PaintCell(CellIndex, NewColor))
				Result.AppendChangedCell(CellIndex);
		}
		return Result;
	}
	GridChangeInfo PaintCells(const ModelGridCellEditSet& CellEditSet, const Color3b& NewColor)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (PaintCell(Cell.CellIndex, NewColor))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}


	template<typename EnumerableType>
	GridChangeInfo PaintCells_Complex(const EnumerableType& Cells, FunctionRef<Color3b(const ModelGridCell& Cell)> GenerateColorFunc)
	{
		GridChangeInfo Result;
		for (Vector3i CellIndex : Cells) {
			if (PaintCell_Complex(CellIndex, GenerateColorFunc))
				Result.AppendChangedCell(CellIndex);
		}
		return Result;
	}
	GridChangeInfo PaintCells_Complex(const ModelGridCellEditSet& CellEditSet, FunctionRef<Color3b(const ModelGridCell& Cell)> GenerateColorFunc)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (PaintCell_Complex(Cell.CellIndex, GenerateColorFunc))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}


	template<typename EnumerableType>
	GridChangeInfo PaintCells(const EnumerableType& Cells, uint8_t NewMaterialIndex)
	{
		GridChangeInfo Result;
		for (Vector3i CellIndex : Cells) {
			if (PaintCell(CellIndex, NewMaterialIndex))
				Result.AppendChangedCell(CellIndex);
		}
		return Result;
	}
	GridChangeInfo PaintCells(const ModelGridCellEditSet& CellEditSet, uint8_t NewMaterialIndex)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (PaintCell(Cell.CellIndex, NewMaterialIndex))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}


	template<typename GridCellFaceEnumerableType>
	GridChangeInfo PaintCellFaces(const GridCellFaceEnumerableType& CellFaces, const Color4b& NewColor)
	{
		GridChangeInfo Result;
		for (const GridCellFace& CellFace : CellFaces) {
			if (PaintCellFace(CellFace.CellIndex, (uint8_t)CellFace.FaceIndex, NewColor))
				Result.AppendChangedCell(CellFace.CellIndex);
		}
		return Result;
	}
	GridChangeInfo PaintCellFaces(const ModelGridCellEditSet& CellEditSet, const Color4b& NewColor)
	{
		GridChangeInfo Result;
		for (const ModelGridCellEditSet::EditCell& Cell : CellEditSet.Cells) {
			if (PaintCellFace(Cell.CellIndex, (uint8_t)Cell.FaceIndex, NewColor))
				Result.AppendChangedCell(Cell.CellIndex);
		}
		return Result;
	}
};


} // end namespace GS
