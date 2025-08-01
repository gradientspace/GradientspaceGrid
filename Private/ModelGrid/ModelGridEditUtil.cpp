// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridEditUtil.h"

using namespace GS;

void ModelGridCellEditSet::ReserveAdditional(int N) 
{
	Cells.reserve(Cells.size() + (size_t)N); 
}

void ModelGridCellEditSet::AppendCell(Vector3i CellIndex, bool bFlipX, bool bFlipY)
{
	EditCell cell;
	cell.CellIndex = CellIndex;
	cell.bFlipX = bFlipX;
	cell.bFlipY = bFlipY;
	Cells.push_back(cell);
}
void ModelGridCellEditSet::AppendCell(Vector3i CellIndex, int8_t FaceIndex, bool bFlipX, bool bFlipY)
{
	EditCell cell;
	cell.CellIndex = CellIndex;
	cell.FaceIndex = FaceIndex;
	cell.bFlipX = bFlipX;
	cell.bFlipY = bFlipY;
	Cells.push_back(cell);
}
void ModelGridCellEditSet::AppendCell(Vector3i CellIndex, Vector3i SourceCellIndex)
{
	EditCell cell;
	cell.CellIndex = CellIndex;
	cell.SourceCellIndex = SourceCellIndex;
	Cells.push_back(cell);
}

void ModelGridCellEditSet::AppendCell(const EditCell& NewCell)
{
	Cells.push_back(NewCell);
}


void ModelGridCellEditSet::RemoveDuplicates()
{
	std::sort(Cells.begin(), Cells.end(), [](const EditCell& a, const EditCell& b)
	{
		return a.CellIndex < b.CellIndex;
	});
	auto new_last = std::unique(Cells.begin(), Cells.end(), [](const EditCell& a, const EditCell& b)
	{
		return a.CellIndex == b.CellIndex;
	});
	Cells.erase(new_last, Cells.end());
}


bool ModelGridCellEditSet::ContainsCell(Vector3i CellIndex) const
{
	int N = (int)Cells.size();
	for (int k = 0; k < N; ++k) {
		if (Cells[k].CellIndex == CellIndex)
			return true;
	}
	return false;
}



void ModelGridCellEditSet::AppendMirroredCells(
	const ModelGridAxisMirrorInfo& MirrorX,
	const ModelGridAxisMirrorInfo& MirrorY, 
	bool bRemoveDuplicates)
{
	bool bMirrorX = MirrorX.bMirror;
	bool bMirrorY = MirrorY.bMirror;
	int MirrorOriginX = MirrorX.MirrorOrigin;
	int CenterX = MirrorX.bCenterColumn ? 0 : 1;
	int MirrorOriginY = MirrorY.MirrorOrigin;
	int CenterY = MirrorY.bCenterColumn ? 0 : 1;

	if (bMirrorX == false && bMirrorY == false) return;
	bool bMirrorXY = (bMirrorX && bMirrorY);
	int N = (int)Cells.size();
	Cells.reserve(bMirrorXY ? (4 * N) : (2*N));
	for (int k = 0; k < N; ++k) {
		EditCell CurCell = Cells[k];
		EditCell MirrorCell = CurCell;
		MirrorCell.SourceCellIndex = CurCell.CellIndex;

		if (bMirrorX || bMirrorXY) {
			EditCell Tmp = MirrorCell; 
			// CenterX here is to deal with the fact that the flipped index of 0 could either be
			// 0 or -1, depending on if we want to allow a shared "middle" row or not
			Tmp.CellIndex.X = -((Tmp.CellIndex.X-MirrorOriginX)+CenterX) + MirrorOriginX;
			Tmp.bFlipX = true; 
			Cells.push_back(Tmp);
			if (bMirrorXY) {
				Tmp.bFlipY = true;
				Tmp.CellIndex.Y = -((Tmp.CellIndex.Y-MirrorOriginY)+CenterY) + MirrorOriginY;
				Cells.push_back(Tmp);
			}
		}
		if (bMirrorY || bMirrorXY) {
			EditCell Tmp = MirrorCell; 
			Tmp.CellIndex.Y = -((Tmp.CellIndex.Y-MirrorOriginY)+CenterY) + MirrorOriginY;
			Tmp.bFlipY = true; 
			Cells.push_back(Tmp);
		}
	}

	if (bRemoveDuplicates)
		RemoveDuplicates();
}