// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"

#include <vector>

namespace GS
{

struct GRADIENTSPACEGRID_API ModelGridAxisMirrorInfo
{
	bool bMirror = false;
	int MirrorOrigin = 0;
	bool bCenterColumn = false;
};


class GRADIENTSPACEGRID_API ModelGridCellEditSet
{
public:

	struct EditCell
	{
		Vector3i CellIndex;
		Vector3i SourceCellIndex = Vector3i::MaxInt();
		int8_t FaceIndex = -1;
		bool bFlipX = false;
		bool bFlipY = false;
	};

	std::vector<EditCell> Cells;

	void Reset() { Cells.clear(); }
	size_t Size() const { return Cells.size(); }
	void ReserveAdditional(int N);

	void AppendCell(Vector3i CellIndex, bool bFlipY = false, bool bFlipZ = false);
	void AppendCell(Vector3i CellIndex, int8_t FaceIndex, bool bFlipY = false, bool bFlipZ = false);
	void AppendCell(Vector3i CellIndex, Vector3i SourceCellIndex);
	void AppendCell(const EditCell& NewCell);
	void RemoveDuplicates();

	EditCell GetCell(int i) const { return Cells[i]; }
	Vector3i GetCellIndex(int i) const { return Cells[i].CellIndex; }

	bool ContainsCell(Vector3i CellIndex) const;

	template<typename EnumerateFunc>
	void EnumerateCells(EnumerateFunc func) const {
		for (const EditCell& Cell : Cells)
			func(Cell);
	}


	void AppendMirroredCells(
		const ModelGridAxisMirrorInfo& MirrorX,
		const ModelGridAxisMirrorInfo& MirrorY, 
		bool bRemoveDuplicates = true);

};




} // end namespace GS
