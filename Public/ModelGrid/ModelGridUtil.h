// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGridTypes.h"
#include "ModelGrid/ModelGridCell.h"
#include "Math/GSQuaternion.h"

namespace GS
{


enum class ECubeOrientationAxis : uint8_t
{
	PositiveZ = 0,
	PositiveY = 1,
	PositiveX = 2,
	NegativeX = 3,
	NegativeY = 4,
	NegativeZ = 5
};


GRADIENTSPACEGRID_API
Quaterniond MakeCubeOrientation(CubeOrientation Orientation);

GRADIENTSPACEGRID_API
Vector3d GetAxisDirectionFromIndex(uint8_t RotationAxis);



GRADIENTSPACEGRID_API
bool GetRotatedCubeOrientation(CubeOrientation CurOrientation, int RotationAxis, int RotationTurns, CubeOrientation& NewOrientation);



GRADIENTSPACEGRID_API
bool ApplyRotationToCell(ModelGridCell& GridCell, int RotationAxis, int RotationTurns);

GRADIENTSPACEGRID_API
bool ApplyFlipToCell(ModelGridCell& GridCell, bool bFlipX, bool bFlipY, bool bFlipZ);


}
