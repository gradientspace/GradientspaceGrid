// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Math/GSIntVector3.h"

#ifdef _MSC_VER
#include <xhash>
#else
#include <functional>
#endif


namespace GS
{


template<typename GridType>
struct GridIndex3
{
	union {
		struct {
			int X;
			int Y;
			int Z;
		};
		int XYZ[3] = { {}, {}, {} };
	};

	explicit constexpr GridIndex3(Vector3i VecIndex) : X(VecIndex.X), Y(VecIndex.Y), Z(VecIndex.Z) {}
	explicit constexpr GridIndex3(int Xin, int Yin, int Zin) : X(Xin), Y(Yin), Z(Zin) {}

	explicit constexpr operator Vector3i() const { return Vector3i(X,Y,Z); }

	constexpr bool operator==(const GridIndex3& OtherIndex) const { return X == OtherIndex.X && Y == OtherIndex.Y && Z == OtherIndex.Z; }
	constexpr bool operator==(const Vector3i& VecIndex) const { return X == VecIndex.X && Y == VecIndex.Y && Z == VecIndex.Z; }

	constexpr bool operator<(const GridIndex3& OtherIndex) const {
		if (X != OtherIndex.X)		return X < OtherIndex.X;
		else if (Y != OtherIndex.Y)	return Y < OtherIndex.Y;
		else if (Z != OtherIndex.Z)	return Z < OtherIndex.Z;
		else return false;
	}

	constexpr GridIndex3& operator+=(const Vector3i& VecOffset) { X += VecOffset.X; Y += VecOffset.Y; Z += VecOffset.Z; return *this; }
	constexpr GridIndex3& operator-=(const Vector3i& VecOffset) { X -= VecOffset.X; Y -= VecOffset.Y; Z -= VecOffset.Z; return *this; }

	constexpr int64_t CityBlockDistance(const GridIndex3& Vec) const {
		int64_t dx = ((int64_t)Vec.X - (int64_t)X), dy = ((int64_t)Vec.Y - (int64_t)Y), dz = ((int64_t)Vec.Z - (int64_t)Z);
		return GS::Abs(dx) + GS::Abs(dy) + GS::Abs(dz);
	}
	constexpr int64_t CityBlockDistance(const Vector3i& Vec) const {
		int64_t dx = ((int64_t)Vec.X - (int64_t)X), dy = ((int64_t)Vec.Y - (int64_t)Y), dz = ((int64_t)Vec.Z - (int64_t)Z);
		return GS::Abs(dx) + GS::Abs(dy) + GS::Abs(dz);
	}
};


template<typename GridIndex3Type>
GridIndex3Type GridAdd(const GridIndex3Type& Index, const Vector3i& Offset)
{
	return GridIndex3Type(Index.X + Offset.X, Index.Y + Offset.Y, Index.Z + Offset.Z);
}



template<typename GridType>
uint32_t GetTypeHash(const GridIndex3<GridType>& GridIndex)
{
	return GS::HashVector3(GridIndex.X, GridIndex.Y, GridIndex.Z);
}

}


