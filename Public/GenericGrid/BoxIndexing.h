// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Math/GSMath.h"
#include "Math/GSVector3.h"
#include "Math/GSIntVector3.h"

namespace GS
{

//
// Box indexing works like this:
//   +/- X faces  = indices 0/1,   +/- Y = 2/3,  +/- Z = 4,5
//   

template<typename RealType>
Vector3<RealType> FaceIndexToNormal(int Index)
{
	switch (Index)
	{
		case 0: return Vector3<RealType>(1, 0, 0);
		case 1: return Vector3<RealType>(-1, 0, 0);
		case 2: return Vector3<RealType>(0, 1, 0);
		case 3: return Vector3<RealType>(0, -1, 0);
		case 4: return Vector3<RealType>(0, 0, 1);
		case 5: return Vector3<RealType>(0, 0, -1);
		default: return Vector3<RealType>(0, 0, 1);
	}
}

//! return delta/offset vector to cell index across the specified face (same as normal, but an IntVector)
//! face ordering is +/- X faces  = 0/1,  +/- Y = 2/3,  +/- Z = 4,5
inline Vector3i FaceIndexToOffset(uint32_t Index)
{
	switch (Index){
		case 0: return Vector3i(1, 0, 0);
		case 1: return Vector3i(-1, 0, 0);
		case 2: return Vector3i(0, 1, 0);
		case 3: return Vector3i(0, -1, 0);
		case 4: return Vector3i(0, 0, 1);
		case 5: return Vector3i(0, 0, -1);
		default: return Vector3i(0, 0, 1);
	}
}

//! return the X/Y/Z axis associated with the FaceIndex 0/1=x, 2/3=y, 4/5=z
inline Vector3i FaceIndexToAxis(uint32_t Index)
{
	switch (Index) {
		case 0: case 1: return Vector3i(1, 0, 0);
		case 2: case 3: return Vector3i(0, 1, 0);
		case 4: case 5: return Vector3i(0, 0, 1);
		default: return Vector3i::Zero();
	}
}


template<typename VectorType>
uint32_t NormalToFaceIndex(const VectorType& Normal)
{
	double AbsX = (double)Abs(Normal.X), AbsY = (double)Abs(Normal.Y), AbsZ = (double)Abs(Normal.Z);

	if (AbsX >= AbsY)
	{
		if (AbsX >= AbsZ) 
			return (Normal.X > 0 ? 0 : 1);
		else 
			return (Normal.Z > 0 ? 4 : 5);
	}
	else
	{
		if (AbsY >= AbsZ) 
			return (Normal.Y > 0 ? 2 : 3);
		else 
			return (Normal.Z < 0 ? 5 : 4);
	}
}



} // end namespace GS
