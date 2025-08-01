// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "WorldGrid/WorldGridDB.h"

#include <vector>

namespace GS
{

class GRADIENTSPACEGRID_API WorldGridGenerator
{
public:
	virtual ~WorldGridGenerator();

	struct BlockInfo
	{
		WorldGridRegionIndex WorldRegionIndex;
		Vector3i OriginCell;

		AxisBox3i WorldCellIndexRange;

		// this is modelgrid min index (ie signed index range)
		Vector3i BlockMinIndex;
	};


	virtual void PopulateRegionBlocks_Blocking(BlockInfo BlockInfo, ModelGrid& RegionGrid, const std::vector<GridRegionHandle>& PendingHandles, WorldRegionModelGridInfo& ExtendedInfo);



	std::vector<Vector3i> ModifiedModelBlocksOut;

protected:
};



} // end namespace GS
