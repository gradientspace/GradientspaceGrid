// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridUtil.h"

#include "Core/gs_debug.h"

using namespace GS;



Vector3d CubeDirectionToAxis[8] =
{
	Vector3d::UnitZ(),
	Vector3d::UnitY(),
	Vector3d::UnitX(),
	-Vector3d::UnitX(),
	-Vector3d::UnitY(),
	-Vector3d::UnitZ(),

	// should never be used, set to invalid axes to detect bugs
	Vector3d(1,1,0),
	Vector3d(0,1,1)
};

double CubeDirectionRotationSigns[8] =
{
	1.0,
	-1.0,
	-1.0,
	-1.0,
	-1.0,
	1.0,
	1.0,
	1.0
};


Quaterniond GS::MakeCubeOrientation(CubeOrientation Orientation)
{
	gs_runtime_assert(Orientation.Direction < 6);
	gs_runtime_assert(Orientation.Rotation < 4);

	Vector3d OrientationAxis = CubeDirectionToAxis[Orientation.Direction];
	double RotationSign = CubeDirectionRotationSigns[Orientation.Direction];

	Quaterniond AxisAlign(Vector3d::UnitZ(), OrientationAxis);
	Quaterniond AxisRotation(OrientationAxis, (double)Orientation.Rotation * RotationSign * GS::Mathd::Pi()/2.0, false);
	Quaterniond Result = AxisAlign * AxisRotation;
	return Result;
}

Vector3d GS::GetAxisDirectionFromIndex(uint8_t RotationAxis)
{
	gs_runtime_assert(RotationAxis < 6);
	return CubeDirectionToAxis[RotationAxis];
}



bool GS::GetRotatedCubeOrientation(CubeOrientation CurOrientation, int RotationAxis, int RotationTurns, CubeOrientation& NewOrientation)
{
	Quaterniond CurQuat = MakeCubeOrientation(CurOrientation);

	Vector3d RotAxis(0, 0, 0);
	RotAxis[RotationAxis] = 1.0;
	Quaterniond QuatRotation(RotAxis, (double)RotationTurns * GS::Mathd::Pi()/2.0, false);

	Quaterniond RotatedQuat = QuatRotation * CurQuat;

	NewOrientation.Direction = CurOrientation.Direction;
	NewOrientation.Rotation = CurOrientation.Rotation;

	bool bFound = false;
	for (uint8_t Axis = 0; Axis < 6 && !bFound; ++Axis)
	{
		for (uint8_t Rot = 0; Rot < 4; ++Rot)
		{
			// have to test both Q and -Q, see https://gamedev.stackexchange.com/questions/75072/how-can-i-compare-two-quaternions-for-logical-equality

			Quaterniond Tmp = MakeCubeOrientation( CubeOrientation(Axis, Rot) );
			if (RotatedQuat.IsSameOrientation(Tmp))
			{
				NewOrientation.Direction = Axis;
				NewOrientation.Rotation = Rot;
				bFound = true;
				break;
			}

			//// this is computing sum-squared-differences and should be a function in quat...
			//double dx = Tmp.X - RotatedQuat.X;
			//double dy = Tmp.Y - RotatedQuat.Y;
			//double dz = Tmp.Z - RotatedQuat.Z;
			//double dw = Tmp.W - RotatedQuat.W;
			//if ( (dx * dx + dy * dy + dz * dz + dw * dw) < FMathf::ZeroTolerance)
			//{
			//	NewOrientation.Direction = Axis;
			//	NewOrientation.Rotation = Rot;
			//	bFound = true;
			//	break;
			//}

			//Tmp = -Tmp;
			//dx = Tmp.X - RotatedQuat.X;
			//dy = Tmp.Y - RotatedQuat.Y;
			//dz = Tmp.Z - RotatedQuat.Z;
			//dw = Tmp.W - RotatedQuat.W;
			//if (dx * dx + dy * dy + dz * dz + dw * dw < FMathf::ZeroTolerance)
			//{
			//	NewOrientation.Direction = Axis;
			//	NewOrientation.Rotation = Rot;
			//	bFound = true;
			//	break;
			//}
		}
	}
	if (!bFound)
	{
		gs_runtime_assert(false);
		//UE_LOG(LogTemp, Warning, TEXT("COULD NOT FIND NEW ORIENTATION FOR (%f,%f,%f,%f)"), RotatedQuat.X, RotatedQuat.Y, RotatedQuat.Z, RotatedQuat.W);
		return false;
	}
	return true;

}



template<typename SubCellType>
void ApplyRotationToSubCell(SubCellType& SubCell, int Axis, int Steps)
{
	CubeOrientation CurOrientation{ SubCell.Params.AxisDirection, SubCell.Params.AxisRotation };
	CubeOrientation NewOrientation;
	GS::GetRotatedCubeOrientation(CurOrientation, Axis, Steps, NewOrientation);
	SubCell.Params.AxisDirection = NewOrientation.Direction;
	SubCell.Params.AxisRotation = NewOrientation.Rotation;
}




bool GS::ApplyRotationToCell(ModelGridCell& GridCell, int RotationAxis, int RotationTurns)
{
	switch (GridCell.CellType)
	{
	case EModelGridCellType::Slab_Parametric:
	case EModelGridCellType::Ramp_Parametric:
	case EModelGridCellType::Corner_Parametric:
	case EModelGridCellType::Pyramid_Parametric:
	case EModelGridCellType::Peak_Parametric:
	case EModelGridCellType::Cylinder_Parametric:
	{
			ModelGridCellData_StandardRST SubCellParams;
			InitializeSubCellFromGridCell(GridCell, SubCellParams);
			ApplyRotationToSubCell(SubCellParams, RotationAxis, RotationTurns);
			UpdateGridCellParamsFromSubCell(GridCell, SubCellParams);
		} return true;

	default:
		break;
	}
	return false;
}


bool GS::ApplyFlipToCell(ModelGridCell& GridCell, bool bFlipX, bool bFlipY, bool bFlipZ)
{
	if (!(bFlipX || bFlipY || bFlipZ))
		return false;
	if (ModelGridCellData_StandardRST::IsSubType(GridCell.CellType))
	{
		ModelGridCellData_StandardRST CellData;
		CellData.Params.Fields = GridCell.CellData;
		CellData.Params.FlipX = (bFlipX) ? 1 : 0;
		CellData.Params.FlipY = (bFlipY) ? 1 : 0;
		CellData.Params.FlipZ = (bFlipZ) ? 1 : 0;
		GridCell.CellData = CellData.Params.Fields;
		return true;
	}
	return false;
}


