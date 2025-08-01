// Copyright Gradientspace Corp. All Rights Reserved.
#include "WorldGrid/WorldGridStorage.h"

using namespace GS;

bool WorldGridMemoryStorage::HasWorldGridRegion(WorldGridRegionIndex RegionIndex, size_t& SizeInBytesOut) const
{
	std::lock_guard<std::mutex> _scopedlock(regions_lock);

	auto iterator = StoredRegions.find(RegionIndex);
	if (iterator != StoredRegions.end()) {
		const std::shared_ptr<RegionBuffer>& region = iterator->second;
		SizeInBytesOut = region->NumBytes;
		return true;
	}
	return false;
}

bool WorldGridMemoryStorage::FetchWorldGridRegion(WorldGridRegionIndex RegionIndex, uint8_t* DataBufferOut, size_t BufferSizeBytes)
{
	std::lock_guard<std::mutex> _scopedlock(regions_lock);

	const auto iterator = StoredRegions.find(RegionIndex);
	if (iterator != StoredRegions.end()) {
		const std::shared_ptr<RegionBuffer>& region = iterator->second;
		size_t NumBytes = region->NumBytes;
		if (NumBytes > BufferSizeBytes)
			return false;

		memcpy_s(DataBufferOut, BufferSizeBytes, region->Data, NumBytes);

		return true;
	}
	return false;
}

void WorldGridMemoryStorage::StoreWorldGridRegion(WorldGridRegionIndex RegionIndex, const uint8_t* DataBuffer, size_t NumBytes, bool bTakeOwnership)
{
	std::lock_guard<std::mutex> _scopedlock(regions_lock);

	std::shared_ptr<RegionBuffer> NewRegion = std::make_shared<RegionBuffer>();
	NewRegion->NumBytes = NumBytes;
	if (bTakeOwnership) {
		NewRegion->Data = DataBuffer;
	} else {
		uint8_t* buffer = new uint8_t[NumBytes];
		memcpy_s(buffer, NumBytes, DataBuffer, NumBytes);
		NewRegion->Data = buffer;
	}

	StoredRegions.insert_or_assign(RegionIndex, NewRegion);
}
