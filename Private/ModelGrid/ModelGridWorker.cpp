// Copyright Gradientspace Corp. All Rights Reserved.

#include "ModelGrid/ModelGridWorker.h"
#include "Core/ParallelFor.h"
#include "Core/gs_debug.h"

using namespace GS;

void ModelGridWorkManager::Initialize(ModelGrid& GridIn)
{
	Grid = &GridIn;
}


void ModelGridWorkManager::CollectAllRegionHandles()
{
	gs_debug_assert(PendingHandles.size() == 0);

	Grid->EnumerateBlockHandles([&](GridRegionHandle RegionHandle)
	{
		PendingHandles.push_back(RegionHandle);

	}, bOnlyAllocatedBlocks);
}


void ModelGridWorkManager::AddRegionHandle(GridRegionHandle Handle)
{
	gs_debug_assert(Handle.GridHandle == (void*)Grid);
	PendingHandles.push_back(Handle);
}


void ModelGridWorkManager::EditRegions_Immediate(
	FunctionRef<void(const GridRegionHandle& RegionHandle)> EditFunc)
{
	if (bParallelProcess)
	{
		GS::ParallelFor((uint32_t)PendingHandles.size(), [&](int k)
		{
			const GridRegionHandle& Handle = PendingHandles[k];
			EditFunc(Handle);
		});
	}
	else
	{
		for (const GridRegionHandle& Handle : PendingHandles)
		{
			EditFunc(Handle);
		}
	}
}
