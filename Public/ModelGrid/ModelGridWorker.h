// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "ModelGrid/ModelGridTypes.h"
#include "Core/FunctionRef.h"

#include <vector>

namespace GS
{




class GRADIENTSPACEGRID_API ModelGridWorkManager
{
protected:
	ModelGrid* Grid = nullptr;

	std::vector<GridRegionHandle> PendingHandles;

public:
	bool bOnlyAllocatedBlocks = false;
	bool bParallelProcess = false;

	void Initialize(ModelGrid& Grid);

	void CollectAllRegionHandles();
	void AddRegionHandle(GridRegionHandle Handle);

	void EditRegions_Immediate(
		FunctionRef<void(const GridRegionHandle& RegionHandle)> EditFunc );

protected:

};




}
