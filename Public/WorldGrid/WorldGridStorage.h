// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "WorldGrid/WorldGridDB.h"
#include "WorldGrid/WorldGridInterfaces.h"

#include <mutex>
#include <memory>
#include <vector>
#include <unordered_map>

namespace GS
{


class GRADIENTSPACEGRID_API WorldGridMemoryStorage
	: public IWorldGridStorageAPI
{
public:


	// IWorldGridStorageAPI
	virtual bool HasWorldGridRegion(WorldGridRegionIndex RegionIndex, size_t& SizeInBytesOut) const override;
	virtual bool FetchWorldGridRegion(WorldGridRegionIndex RegionIndex, uint8_t* DataBufferOut, size_t BufferSizeBytes) override;
	virtual void StoreWorldGridRegion(WorldGridRegionIndex RegionIndex, const uint8_t* DataBuffer, size_t NumBytes, bool bTakeOwnership) override;


protected:

	struct RegionBuffer
	{
		const uint8_t* Data = nullptr;
		size_t NumBytes;
	};

	std::unordered_map<WorldGridRegionIndex, std::shared_ptr<RegionBuffer>> StoredRegions;
	mutable std::mutex regions_lock;
};



}
