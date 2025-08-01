// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridChange.h"

using namespace GS;


ModelGridDeltaChange::~ModelGridDeltaChange()
{
	CellKeys = std::vector<Vector3i>();
	CellsBefore = std::vector<ModelGridCell>();
	CellsAfter = std::vector<ModelGridCell>();
}

void ModelGridDeltaChange::DeleteChangeFromExternalDLL(ModelGridDeltaChange* Change)
{
	if (Change == nullptr) return;

	Change->CellKeys = std::vector<Vector3i>();
	Change->CellsBefore = std::vector<ModelGridCell>();
	Change->CellsAfter = std::vector<ModelGridCell>();
	delete Change;
}


ModelGridDeltaChangeTracker::~ModelGridDeltaChangeTracker()
{
	if (Change)
		Change.reset();
	KeyIndex.clear();
}

void ModelGridDeltaChangeTracker::AllocateNewChange()
{
	KeyIndex.clear();		// any existing is no longer valid
	Change = GSMakeUniquePtr<ModelGridDeltaChange>();
}

UniquePtr<ModelGridDeltaChange> ModelGridDeltaChangeTracker::ExtractChange()
{
	return std::move(Change);
}

GS::AxisBox3i ModelGridDeltaChangeTracker::GetCurrentChangeBounds() const
{
	return Change->ChangeBounds;
}

void ModelGridDeltaChangeTracker::Reset()
{
	KeyIndex.clear();
	if (Change)
	{
		Change->CellKeys.clear();
		Change->CellsBefore.clear();
		Change->CellsAfter.clear();
		Change->ChangeBounds = AxisBox3i::Empty();
	}
}

void ModelGridDeltaChangeTracker::AppendModifiedCell(const Vector3i& CellKey, const ModelGridCell& PreviousState, const ModelGridCell& NewState)
{
	if (!Change) return;

	Change->ChangeBounds.Contain(CellKey);

	auto found_itr = KeyIndex.find(CellKey);
	if (found_itr == KeyIndex.end())
	{
		int NewIndex = (int)Change->CellKeys.size();
		Change->CellKeys.push_back(CellKey);
		Change->CellsBefore.push_back(PreviousState);
		Change->CellsAfter.push_back(NewState);
		KeyIndex.insert({ CellKey, NewIndex });
	}
	else
	{
		int index = found_itr->second;
		Change->CellsAfter[index] = NewState;
	}
}
