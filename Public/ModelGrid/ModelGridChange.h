// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"

#include "Core/UniquePointer.h"
#include "Math/GSIntAxisBox3.h"

#include <vector>
#include <unordered_map>

namespace GS
{

struct GRADIENTSPACEGRID_API GridChangeInfo
{
	bool bModified = false;
	AxisBox3i ModifiedRegion = AxisBox3i::Empty();

	void AppendChange(const GridChangeInfo& Change)
	{
		bModified = bModified || Change.bModified;
		if (Change.bModified)
		{
			ModifiedRegion.Contain(Change.ModifiedRegion.Min);
			ModifiedRegion.Contain(Change.ModifiedRegion.Max);
		}
	}

	void AppendChange(const AxisBox3i& ModifiedRegionIn)
	{
		GridChangeInfo Tmp{ true, ModifiedRegionIn };
		AppendChange(Tmp);
	}

	void AppendChangedCell(const Vector3i& ModifiedCell) {
		bModified = true;
		ModifiedRegion.Contain(ModifiedCell);
	}

};


class GRADIENTSPACEGRID_API ModelGridDeltaChange
{
public:
	~ModelGridDeltaChange();

	std::vector<Vector3i> CellKeys;
	std::vector<ModelGridCell> CellsBefore;
	std::vector<ModelGridCell> CellsAfter;
	AxisBox3i ChangeBounds = AxisBox3i::Empty();
	bool IsEmpty() const { return CellKeys.size() == 0; }

	//! this is for external code to call to safely delete the std::vectors above that 
	//! were allocated/update inside a ModelGridDeltaChangeTracker, to avoid cross-dll-boundary issues
	static void DeleteChangeFromExternalDLL(ModelGridDeltaChange* Change);
};


class GRADIENTSPACEGRID_API ModelGridDeltaChangeTracker
{
public:
	~ModelGridDeltaChangeTracker();

	void AllocateNewChange();
	void Reset();
	UniquePtr<ModelGridDeltaChange> ExtractChange();
	GS::AxisBox3i GetCurrentChangeBounds() const;

	void AppendModifiedCell(const Vector3i& CellKey, const ModelGridCell& PreviousState, const ModelGridCell& NewState);

protected:
	std::unordered_map<Vector3i, int> KeyIndex;
	UniquePtr<ModelGridDeltaChange> Change;
};


} // end namespace GS
