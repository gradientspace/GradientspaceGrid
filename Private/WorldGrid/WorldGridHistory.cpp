// Copyright Gradientspace Corp. All Rights Reserved.
#include "WorldGrid/WorldGridHistory.h"
#include "WorldGrid/WorldGridSystem.h"
#include "Core/gs_debug.h"

using namespace GS;


WorldGridHistory::WorldGridHistory()
{
	ClearHistory();
}

void WorldGridHistory::PushPlaceBlock(const WorldGridCellIndex& CellIndex, const ModelGridCell& NewCell)
{
	DiscardFromCursor();

	Operation Op;
	Op.Type = OpType::AddBlock;
	Op.CellIndex = CellIndex;
	Op.CellData = NewCell;
	HistoryStack.push_back(Op);
	Cursor++;
}

void WorldGridHistory::PushRemoveBlock(const WorldGridCellIndex& CellIndex, const ModelGridCell& RemovedCell)
{
	DiscardFromCursor();

	Operation Op;
	Op.Type = OpType::RemoveBlock;
	Op.CellIndex = CellIndex;
	Op.CellData = RemovedCell;
	HistoryStack.push_back(Op);
	Cursor++;
}


bool WorldGridHistory::UndoOneStep(WorldGridSystem* TargetSystem)
{
	if (Cursor == 0) return false;

	uint32_t PrevIndex = Cursor - 1;
	const Operation& Op = HistoryStack[PrevIndex];
	UndoRedo(Op, TargetSystem, true);

	Cursor = PrevIndex;
	return true;
}

bool WorldGridHistory::RedoOneStep(WorldGridSystem* TargetSystem)
{
	if (Cursor == HistoryStack.size()) return false;

	const Operation& Op = HistoryStack[Cursor];
	UndoRedo(Op, TargetSystem, false);

	Cursor = Cursor + 1;
	return true;
}

void WorldGridHistory::ClearHistory()
{
	HistoryStack.clear();
	Cursor = 0;
}


void WorldGridHistory::DiscardFromCursor()
{
	if (Cursor != HistoryStack.size())
		HistoryStack.resize(Cursor);
}


void WorldGridHistory::UndoRedo(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo)
{
	switch (Op.Type)
	{
	case OpType::AddBlock:
		UndoRedo_Add(Op, TargetSystem, bUndo); break;
	case OpType::RemoveBlock:
		UndoRedo_Remove(Op, TargetSystem, bUndo); break;
	default: 
		//UE_LOG(LogTemp, Warning, TEXT("WorldGridHistory::UndoRedo - unknown block!"));
		gs_debug_assert(false);
		break;
	}
}



void WorldGridHistory::UndoRedo_Add(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo)
{
	if (bUndo)
		TargetSystem->TryDeleteBlock_Async(Op.CellIndex);
	else
		TargetSystem->TryPlaceBlock_Async(Op.CellIndex, Op.CellData);
}

void WorldGridHistory::UndoRedo_Remove(const Operation& Op, WorldGridSystem* TargetSystem, bool bUndo)
{
	if (bUndo)
		TargetSystem->TryPlaceBlock_Async(Op.CellIndex, Op.CellData);
	else
		TargetSystem->TryDeleteBlock_Async(Op.CellIndex);
}
