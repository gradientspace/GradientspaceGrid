// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"


namespace GS
{

class IModelGridAdapter
{
public:
	virtual ~IModelGridAdapter() {}

	virtual Vector3i GetModelGridDimensions() const = 0;
	virtual Vector3d GetCellDimensions() const = 0;
	virtual AxisBox3i GetCellIndexRange() const = 0;
	virtual ModelGridCell GetCellAtIndex(Vector3i CellIndex, bool& bIsInGrid) const = 0;
};



class SimpleModelGridAdapter : public IModelGridAdapter
{
public:
	const ModelGrid* SourceGrid = nullptr;

	SimpleModelGridAdapter(const ModelGrid* SourceGridIn) { SourceGrid = SourceGridIn; }

	virtual Vector3i GetModelGridDimensions() const override
	{
		return SourceGrid->ModelGridDimensions();
	}
	virtual Vector3d GetCellDimensions() const override
	{
		return SourceGrid->GetCellDimensions();
	}
	virtual AxisBox3i GetCellIndexRange() const override
	{
		return SourceGrid->GetCellIndexRange();
	}
	virtual ModelGridCell GetCellAtIndex(Vector3i CellIndex, bool& bIsInGrid) const override
	{
		return SourceGrid->GetCellInfo(CellIndex, bIsInGrid);
	}
};



}
