// Copyright Gradientspace Corp. All Rights Reserved.
#include "ModelGrid/ModelGridCell.h"
#include "ModelGrid/ModelGridUtil.h"

using namespace GS;


static_assert(sizeof(GS::ModelGridCellData_StandardRST) == sizeof(uint64_t), "sizeof(ModelGridCellData_StandardRST) != sizeof(uint64_t)");
static_assert(sizeof(GS::GridMaterial) == sizeof(uint32_t), "sizeof(GridMaterial) != sizeof(uint32_t)");


bool ModelGridCellData_StandardRST::IsSubType(EModelGridCellType CellType)
{
	return CellType == EModelGridCellType::Slab_Parametric
		|| CellType == EModelGridCellType::Ramp_Parametric
		|| CellType == EModelGridCellType::Corner_Parametric
		|| CellType == EModelGridCellType::Pyramid_Parametric
		|| CellType == EModelGridCellType::Peak_Parametric
		|| CellType == EModelGridCellType::Cylinder_Parametric
		|| CellType == EModelGridCellType::CutCorner_Parametric;
}



MGCell_Slab MGCell_Slab::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.DimensionZ = (uint8_t)(ModelGridCellData_StandardRST::MaxDimension / 2);
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Slab{ Tmp };
}


MGCell_Ramp MGCell_Ramp::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Ramp{ Tmp };
}

MGCell_Corner MGCell_Corner::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Corner{ Tmp };
}

MGCell_CutCorner MGCell_CutCorner::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_CutCorner{ Tmp };
}

MGCell_Pyramid MGCell_Pyramid::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Pyramid{ Tmp };
}

MGCell_Peak MGCell_Peak::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Peak{ Tmp };
}

MGCell_Cylinder MGCell_Cylinder::GetDefaultCellParams()
{
	ModelGridCellData_StandardRST::Parameters Tmp;
	Tmp.Fields = 0;
	Tmp.TransformType = (uint8_t)EModelGridCellTransformType::StandardRST;
	Tmp.AxisDirection = Tmp.AxisRotation = 0;
	Tmp.DimensionMode = (uint8_t)EModelGridCellDimensionType::Quarters;
	Tmp.DimensionX = Tmp.DimensionY = Tmp.DimensionZ = (uint8_t)ModelGridCellData_StandardRST::MaxDimension;
	Tmp.TranslateX = Tmp.TranslateY = Tmp.TranslateZ = 0;
	return MGCell_Cylinder{ Tmp };
}

void MGCell_Slab::DetermineOrientationFromAxis(const Vector3d& AxisZ, uint8_t& Axis)
{
	for (uint8_t k = 0; k <= ModelGridCellData_StandardRST::MaxRotationAxis; ++k)
	{
		Quaterniond TargetOrientation = MakeCubeOrientation(CubeOrientation(k, 0));
		if (TargetOrientation.AxisZ().Dot(AxisZ) > 0.99)
		{
			Axis = k; return;
		}
	}
	Axis = 0;
}



void MGCell_Ramp::DetermineOrientationFromAxes(const Vector3d& AxisZ, const Vector3d& AxisY, uint8_t& Axis, uint8_t& Angle)
{
	for (uint8_t k = 0; k <= ModelGridCellData_StandardRST::MaxRotationAxis; ++k)
	{
		for (uint8_t j = 0; j <= ModelGridCellData_StandardRST::MaxRotationAngle; ++j)
		{
			Quaterniond TargetOrientation = MakeCubeOrientation(CubeOrientation(k, j));
			if (TargetOrientation.AxisZ().Dot(AxisZ) > 0.99 && TargetOrientation.AxisY().Dot(AxisY) > 0.99)
			{
				Axis = k; Angle = j; return;
			}
		}
	}
	Axis = 0; Angle = 0;
}
void MGCell_Ramp::OrientFromAxes(const Vector3d& UpAxis, const Vector3d& ForwardAxis)
{
	uint8_t Axis, Angle;
	DetermineOrientationFromAxes(UpAxis, ForwardAxis, Axis, Angle);
	Params.AxisDirection = Axis;
	Params.AxisRotation = Angle;
}

void MGCell_Corner::DetermineOrientationFromDiagonal(const Vector3d& CornerDir, uint8_t& Axis, uint8_t& Angle)
{
	Vector3d CornerFaceNormal = Normalized(Vector3d::One());
	for (uint8_t k = 0; k <= ModelGridCellData_StandardRST::MaxRotationAxis; ++k)
	{
		for (uint8_t j = 0; j <= ModelGridCellData_StandardRST::MaxRotationAngle; ++j)
		{
			Quaterniond TargetOrientation = MakeCubeOrientation(CubeOrientation(k, j));
			Vector3d OrientedNormal = TargetOrientation * CornerFaceNormal;
			if ( OrientedNormal.Dot(CornerDir) > 0.9 )
			{
				Axis = k; Angle = j; return;
			}
		}
	}
	Axis = 0; Angle = 0;
}




static void ConstructStandardElementTransformSequence(
	const Vector3d& UnitBoxDimensions,
	uint8_t AxisDirection, uint8_t AxisRotation,
	const Vector3d& DimensionScale,
	const Vector3d& TranslationT,
	const Vector3i& AxisFlips,
	TransformListd& GSTransformSeq)
{
	bool bHaveRotation = (AxisDirection != 0) || (AxisRotation != 0);
	bool bHaveFlips = (AxisFlips.Dot(AxisFlips) > 0);
	bool bNonStandardOrientation = bHaveRotation || bHaveFlips;

	// todo could save a lot here if we can skip using TargetOrientation...

	Quaterniond TargetOrientation = MakeCubeOrientation(CubeOrientation(AxisDirection, AxisRotation));

	// if the dimensions are not all equal, then if the cell is rotated we need to know the
	// dimensions in the new frame. There ought to be a cheaper way to do this, perhaps that
	// doesn't produce negative dimensions...
	Vector3d RotatedDimensions = TargetOrientation.InverseMultiply(UnitBoxDimensions);
	RotatedDimensions.X = GS::Abs(RotatedDimensions.X);
	RotatedDimensions.Y = GS::Abs(RotatedDimensions.Y);
	RotatedDimensions.Z = GS::Abs(RotatedDimensions.Z);

	// these are the scaling factors we have to apply to the un-rotated unit box to scale it to the rotated dimensions
	double CellScaleX = RotatedDimensions.X / UnitBoxDimensions.X;
	double CellScaleY = RotatedDimensions.Y / UnitBoxDimensions.Y;
	double CellScaleZ = RotatedDimensions.Z / UnitBoxDimensions.Z;

	// this is the center of the rotated cell
	Vector3d ScaledUnitCellOrigin(
		CellScaleX * (UnitBoxDimensions.X / 2),
		CellScaleY * (UnitBoxDimensions.Y / 2),
		CellScaleZ * (UnitBoxDimensions.Z / 2));
	
	// Combine our externally-provided cell scaling with the dimensional scaling.
	// The cell origin is at the min-corner here, so we are scaling down towards (0,0,0), not around the center
	Vector3d Scale(CellScaleX * DimensionScale.X, CellScaleY * DimensionScale.Y, CellScaleZ * DimensionScale.Z);
	GSTransformSeq.AppendScale(Scale);

	// translation is interpreted as a fraction of the cell size, but we do not allow translating the scaled
	// unit-object outside of the cell, so the max we can translate is the "remaining" space in the unit-cell
	double RemainingX = (RotatedDimensions.X) - (DimensionScale.X * RotatedDimensions.X);
	double RemainingY = (RotatedDimensions.Y) - (DimensionScale.Y * RotatedDimensions.Y);
	double RemainingZ = (RotatedDimensions.Z) - (DimensionScale.Z * RotatedDimensions.Z);
	Vector3d CellTranslate(
		GS::Min(TranslationT.X * RotatedDimensions.X, RemainingX),
		GS::Min(TranslationT.Y * RotatedDimensions.Y, RemainingY),
		GS::Min(TranslationT.Z * RotatedDimensions.Z, RemainingZ));
	GSTransformSeq.AppendTranslation(CellTranslate);

	// now we can rotate & flip into the target cell orientation. We want to rotate around the cell center,
	// but the cell is [0,0,0] to [InitialDimensions], so we need to translate the RotateDimensions center
	// to the origin, then rotate, then translate back to the InitialDimensions center.
	// Flip is incorporated here, we are basically treating flipping as part of the orientation. Flipping
	// happens *after* rotation, so (eg) a Z-flip occurs along the grid Z axis, not the cell-local / pre-transform Z axis.
	// Note that this does mean that changing direction/rotation after a flip might be a bit weird...but the
	// direction/rotation parameters are /already/ weird and so it seems less problematic. 
	// Also if the scale-determinant is negative, rotation direction will be reversed, but this can be
	// handled at the UI level...
	if (bNonStandardOrientation)
	{
		// translate center of scaled box to origin so we can rotate around center
		GSTransformSeq.AppendTranslation(-ScaledUnitCellOrigin);

		if ( bHaveRotation)
			GSTransformSeq.AppendRotation(TargetOrientation);

		if ( bHaveFlips ) {
			GS::Vector3d FlipScale(AxisFlips.X ? -1.0 : 1.0, AxisFlips.Y ? -1.0 : 1.0, AxisFlips.Z ? -1.0 : 1.0);
			GSTransformSeq.AppendScale(FlipScale);
		}

		// translate back to unit cell origin
		GSTransformSeq.AppendTranslation(UnitBoxDimensions * 0.5);
	}
}



static Vector3d StandardRSTDimensionToScale(const ModelGridCellData_StandardRST& Cell)
{
	static_assert(ModelGridCellData_StandardRST::MaxDimension == 15);

	//static const double Quarters[16] = { 1.0/16.0, 2.0/16.0, 3.0/16.0, 4.0/16.0, 5.0/16.0, 6.0/16.0, 7.0/16.0, 8.0/16.0, 
	//									9.0/16.0, 10.0/16.0, 11.0/16.0, 12.0/16.0, 13.0/16.0, 14.0/16.0, 15.0/16.0, 1.0 };
	static const double Thirds[16] = { 0.0125, 0.025, 1.0/12.0, 2.0/12.0, 3.0/12.0, 4.0/12.0, 5.0/12.0, 6.0/12.0, 7.0/12.0, 
                                                      8.0/12.0, 9.0/12.0, 10.0/12.0, 11.0/12.0, 1-0.025, 1-0.0125, 1.0 };

	if (Cell.Params.DimensionMode == (uint8_t)EModelGridCellDimensionType::Thirds) {
		return Vector3d(
			Thirds[Cell.Params.DimensionX], Thirds[Cell.Params.DimensionY], Thirds[Cell.Params.DimensionZ]);
	}
	else {		
		// Quarters
		//return Vector3d(
		//	Quarters[Cell.Params.DimensionX], Quarters[Cell.Params.DimensionY], Quarters[Cell.Params.DimensionZ]);
		constexpr double mul = 1.0 / (double)(ModelGridCellData_StandardRST::MaxDimension + 1);
		return Vector3d(
			(double)(Cell.Params.DimensionX+1) * mul, 
			(double)(Cell.Params.DimensionY+1) * mul, 
			(double)(Cell.Params.DimensionZ+1) * mul);
	}
}


void GS::GetUnitCellTransform(const ModelGridCellData_StandardRST& SubCell, const Vector3d& UnitCellDimensions, TransformListd& TransformSeqOut, bool bIgnoreSubCellDimensions)
{
	Vector3d DimensionScale = (bIgnoreSubCellDimensions) ? Vector3d::One() : StandardRSTDimensionToScale(SubCell);

	// cell translation values in range [0,1]
	Vector3d TranslationT(
		(double)(SubCell.Params.TranslateX) / (double)(ModelGridCellData_StandardRST::MaxTranslate+1),
		(double)(SubCell.Params.TranslateY) / (double)(ModelGridCellData_StandardRST::MaxTranslate+1),
		(double)(SubCell.Params.TranslateZ) / (double)(ModelGridCellData_StandardRST::MaxTranslate+1));
	
	// if dimensions are thirds then we want to use a smaller range of translations so that things can
	// be centered in the cell. This does result in slightly weird behavior if things are switched, though...
	if (SubCell.Params.DimensionMode == (uint8_t)EModelGridCellDimensionType::Thirds)
	{
		TranslationT = Vector3d(
			GS::Clamp( (double)(SubCell.Params.TranslateX) / (double)(ModelGridCellData_StandardRST::MaxTranslate_Thirds+1), 0.0, 1.0),
			GS::Clamp( (double)(SubCell.Params.TranslateY) / (double)(ModelGridCellData_StandardRST::MaxTranslate_Thirds+1), 0.0, 1.0),
			GS::Clamp( (double)(SubCell.Params.TranslateZ) / (double)(ModelGridCellData_StandardRST::MaxTranslate_Thirds+1), 0.0, 1.0)
		);
	}

	Vector3i AxisFlips(SubCell.Params.FlipX, SubCell.Params.FlipY, SubCell.Params.FlipZ);

	ConstructStandardElementTransformSequence(
		UnitCellDimensions, SubCell.Params.AxisDirection, SubCell.Params.AxisRotation, DimensionScale, TranslationT, AxisFlips, TransformSeqOut);
}


void GS::GetUnitCellTransform(const ModelGridCell& CellInfo, const Vector3d& UnitCellDimensions, TransformListd& TransformSeqOut, bool bIgnoreSubCellDimensions)
{
	switch (CellInfo.CellType)
	{
	case EModelGridCellType::Slab_Parametric:
	case EModelGridCellType::Ramp_Parametric:
	case EModelGridCellType::Corner_Parametric:
	case EModelGridCellType::Pyramid_Parametric:
	case EModelGridCellType::Peak_Parametric:
	case EModelGridCellType::Cylinder_Parametric:
	case EModelGridCellType::CutCorner_Parametric:
		{
		ModelGridCellData_StandardRST SubCellParams;
		InitializeSubCellFromGridCell(CellInfo, SubCellParams);
		GetUnitCellTransform(SubCellParams, UnitCellDimensions, TransformSeqOut, bIgnoreSubCellDimensions);
		} break;

	default:
		break;
	}
}


GS::ModelGridCell GS::MakeDefaultCellFromType(EModelGridCellType CellType)
{
	switch (CellType)
	{
	case EModelGridCellType::Filled:				return ModelGridCell::SolidCell();
	case EModelGridCellType::Slab_Parametric:		return MakeDefaultCell<MGCell_Slab>();
	case EModelGridCellType::Ramp_Parametric:		return MakeDefaultCell<MGCell_Ramp>();
	case EModelGridCellType::Corner_Parametric:		return MakeDefaultCell<MGCell_Corner>();
	case EModelGridCellType::Pyramid_Parametric:	return MakeDefaultCell<MGCell_Pyramid>();
	case EModelGridCellType::Peak_Parametric:		return MakeDefaultCell<MGCell_Peak>();
	case EModelGridCellType::Cylinder_Parametric:	return MakeDefaultCell<MGCell_Cylinder>();
	case EModelGridCellType::CutCorner_Parametric:	return MakeDefaultCell<MGCell_CutCorner>();
	}
	return ModelGridCell::SolidCell();
}

