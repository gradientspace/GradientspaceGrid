// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGridCell.h"
#include "WorldGrid/WorldGridInterfaces.h"
#include "Math/GSIntVector3.h"
#include <vector>

namespace GS
{

class WorldGridSystem;

class GRADIENTSPACEGRID_API WorldGridHistory
{
public:
	WorldGridHistory();

	void PushPlaceBlock(const WorldGridCellIndex& CellIndex, const ModelGridCell& NewCell);
	void PushRemoveBlock(const WorldGridCellIndex& CellIndex, const ModelGridCell& RemovedCell);

	bool UndoOneStep(WorldGridSystem* TargetSystem);
	bool RedoOneStep(WorldGridSystem* TargetSystem);

	void ClearHistory();

protected:

	enum class OpType
	{
		AddBlock,
		RemoveBlock
	};

	struct Operation
	{
		OpType Type;
		WorldGridCellIndex CellIndex;
		ModelGridCell CellData;
	};

	std::vector<Operation> HistoryStack;

	uint32_t Cursor;
	void DiscardFromCursor();


	void UndoRedo(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo);
	void UndoRedo_Add(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo);
	void UndoRedo_Remove(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo);
};


} // end namespace GS
