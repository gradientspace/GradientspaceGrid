// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Color/GSColor3b.h"
#include "Color/GSIntColor4.h"
#include "Color/GSColorConversion.h"
#include "Math/GSVector3.h"
#include "Math/GSVector4.h"
#include "Math/GSTransformList.h"
#include "ModelGrid/ModelGridTypes.h"

namespace GS
{



enum class EModelGridCellTransformType : uint8_t
{
	StandardRST = 0				// only possible option currently
};


enum class EModelGridCellDimensionType : uint8_t
{
	Quarters = 0,				// cell dimensions values are 1/16ths
	Thirds = 1,					// cell dimension values are 1/12ths, with extra 4 values used to represent 0.0125 and 0.025 (and one-minus's)
	ReservedForFutureUse = 2,	// may be used by system in future
	ClientDefined = 3			// somehow client could define what these dimensions are (via editing StandardRSTDimensionToScale, currently...)
};


enum class EGridCellMaterialType
{
	SolidColor = 0,				// 8-bit RGBA color
	SolidRGBIndex = 1,			// 8-bit RGB color plus 8-bit index

	BeginPerFaceTypes = 8,
	FaceColors = 8,				// 8-bit RGBA color for each of the 6 cell faces
};

// a GridMaterial is 32-bit storage for material info. There are 3 variants,
// which are packed into the struct in different ways:
// 1) 8-bit RGBA color
// 2) 8-bit RGB color plus an 8-bit index
// 3) 32-bit index
// Struct does not know which of these it is, it will depend on the EGridCellMaterialType
// of the owning grid cell
struct GridMaterial
{
	union
	{
		struct
		{
			uint8_t Red;
			uint8_t Green;
			uint8_t Blue;
			uint8_t Alpha;
		} RGBAColor;
		struct
		{
			uint8_t Red;
			uint8_t Green;
			uint8_t Blue;
			uint8_t Index;
		} RGBColorIndex;
		struct
		{
			uint32_t Value;
		} SingleIndex;
	};

	constexpr GridMaterial() { SingleIndex.Value = 0; }
	explicit constexpr GridMaterial(uint32_t PackedValue) {
		SingleIndex.Value = PackedValue;
	}
	explicit constexpr GridMaterial(const Color3b& ColorIn) 
	{
		RGBAColor.Red = ColorIn.R; RGBAColor.Green = ColorIn.G; RGBAColor.Blue = ColorIn.B;
	}
	explicit constexpr GridMaterial(const Color4b& ColorIn) 
	{
		RGBAColor.Red = ColorIn.R; RGBAColor.Green = ColorIn.G; RGBAColor.Blue = ColorIn.B; RGBAColor.Alpha = ColorIn.A;
	}
	explicit constexpr GridMaterial(const Color3b& ColorIn, uint8_t IndexIn)
	{
		RGBAColor.Red = ColorIn.R; RGBAColor.Green = ColorIn.G; RGBAColor.Blue = ColorIn.B;
		RGBColorIndex.Index = IndexIn;
	}

	constexpr Color3b AsColor3b() const { return Color3b(RGBAColor.Red, RGBAColor.Green, RGBAColor.Blue); }
	constexpr Color4b AsColor4b() const { return Color4b(RGBAColor.Red, RGBAColor.Green, RGBAColor.Blue, RGBAColor.Alpha); }
	constexpr uint8_t GetIndex8() const { return RGBColorIndex.Index; }
	constexpr uint32_t GetIndex32() const { return SingleIndex.Value; }
	constexpr uint32_t PackedValue() const { return SingleIndex.Value; }

	Vector3f AsVector3f(bool bConvertFromSRGBToLinear) const { 
		if (bConvertFromSRGBToLinear)
			return GS::SRGBToLinear( Color3b(RGBAColor.Red, RGBAColor.Green, RGBAColor.Blue) );
		else
			return (Vector3f)Color3b(RGBAColor.Red, RGBAColor.Green, RGBAColor.Blue);
	}

	Vector4f AsVector4f(bool bConvertFromSRGBToLinear, bool bIncludeAlpha) const
	{ 
		GS::Color4b Color4(RGBAColor.Red, RGBAColor.Green, RGBAColor.Blue, (bIncludeAlpha) ? RGBAColor.Alpha : 255);
		return (bConvertFromSRGBToLinear) ? GS::SRGBToLinear(Color4) : (Vector4f)Color4;
	}

	static constexpr GridMaterial White() { return GridMaterial(Color4b::White()); }

	constexpr bool operator==(const GridMaterial& Other) const {
		return SingleIndex.Value == Other.SingleIndex.Value;
	}
};




// set of GridMaterial for each "face" of a cell (currently max 8 faces)
struct CellFaceMaterials
{
	static constexpr int MaxFaces = 8;

	GridMaterial Faces[MaxFaces];

	bool operator==(const CellFaceMaterials& Other) const {
		return memcmp(&Faces[0], &Other.Faces[0], sizeof(GridMaterial[MaxFaces])) == 0;
	}
	bool operator!=(const CellFaceMaterials& Other) const {
		return !(*this == Other);
	}
	GridMaterial& operator[](int index) { return Faces[index]; }
	const GridMaterial& operator[](int index) const { return Faces[index]; }
};




struct ModelGridCell
{
	EModelGridCellType CellType = EModelGridCellType::Empty;
	uint64_t CellData = 0;

	EGridCellMaterialType MaterialType = EGridCellMaterialType::SolidColor;
	// single material for the entire cell
	GridMaterial CellMaterial;
	// per-face materials for the cell
	CellFaceMaterials FaceMaterials;

	bool IsSame(const ModelGridCell& Other, uint64_t DataMask = 0xFFFFFFFFFFFFFFFF) const
	{
		if (CellType != Other.CellType) return false;
		if ((int)CellType > (int)EModelGridCellType::Filled && (CellData & DataMask) != (Other.CellData & DataMask)) return false;
		if (MaterialType != Other.MaterialType) return false;
		if ((int)MaterialType >= (int)EGridCellMaterialType::FaceColors)
		{
			if (FaceMaterials != Other.FaceMaterials) return false;
		}
		else
		{
			if (CellMaterial != Other.CellMaterial) return false;
		}
		return true;
	}

	bool operator==(const ModelGridCell& Other) const
	{
		return IsSame(Other);
	}

	bool IsEmpty() const { return CellType == EModelGridCellType::Empty; }

	void SetToSolidColor(const Color3b& Color)
	{
		MaterialType = EGridCellMaterialType::SolidColor;
		CellMaterial = GridMaterial(Color);
	}

	void SetToSolidRGBIndex(const Color3b& Color, uint8_t MaterialIndex)
	{
		MaterialType = EGridCellMaterialType::SolidRGBIndex;
		CellMaterial = GridMaterial(Color, MaterialIndex);
	}

	static ModelGridCell EmptyCell() { return ModelGridCell{ EModelGridCellType::Empty, 0, EGridCellMaterialType::SolidColor, GridMaterial::White() }; }
	static ModelGridCell SolidCell() { return ModelGridCell{ EModelGridCellType::Filled, 0xFFFFFFFF, EGridCellMaterialType::SolidColor, GridMaterial::White() }; }
};


/**
 * Base type for ModelGrid CellData that stores rotation, scale/dimension, and translation parameters.
 * (still a bit of a WIP)
 *
 * WARNING: size of this structure must remain 64-bit!!
 * 
 * Here is how this transform behaves (implemented in ConstructStandardElementTransformSequence() in ModelGrid.cpp file):
 *   1) Dimension X/Y/Z parameters are used to scale *towards the origin*. So they essentially define the fraction-of-a-cell in
 *      each direction, and the (presumably initially full-cell) contents will be scaled down by that amount, towards (0,0,0).
 *      The amount of scaling is affected by DimensionMode, each of the 16 possible values is mapped to a specific fraction, which may not be equally spaced.
 *      For Quarters the spacing is in 1/16ths, for Thirds it's in 1/12ths with 0.0125 and 0.025 (and one-minus) on either end
 *   2) Translate X/Y/Z parameters are used to translate away from the origin. This translation is done by equally-spaced
 *      fractions ranging from 0 to 1. For "Quarters" this step size is 1/32, and for "Thirds" it is 1/23 - the latter is to
 *      allow for correct centering of the thirds-high cells.
 *      In both cases, the translation amount is not dependent on the dimension amount (ie it's not a translation of the "remaining" space).
 *      So if the translation would take the scaled-cell out of the box, it is clamped.
 *      Note that this can produce some weird behavior if changing Dimension after Translation...as shape will "grow backwards"
 *   3) If there is a Rotation or Flip, the cell midpoint is translated to origin, Rotation/Flip is applied, and then it's translated back.
 *       3a) Rotation is applied by first rotating +Z into the AxisDirection direction, and then Rotating by
 *           (AxisRotation*90) degrees. This second rotation ends up always being around the grid Z axis (always +Z?).
 *           (Note that it would /not/ make work to always rotate around the target Axis because then (eg) many orientations could never
 *           be achieved for something like a cylinder or Pyramid!!)
 *           Construction of the resulting rotation (currently a FQuaterniond, inefficiently) is implemented in GS::MakeCubeOrientation().
 *           Note that to apply an extrinsic rotation (ie "I'm looking at this cell and I want to rotate it 90 degrees around grid +X")
 *           both AxisRotation and AxisDirection will usually be involved. GS::ApplyRotationToCell() implements this operation.
 *       3b) Flips are now applied by -1 scales in the respective directions. Note that if the determinant of the flip transform is negative
 *           (ie scalex*y*z < 0) the AxisRotation direction will (effectively) be reversed, and the transform will invert the orientation
 *           of space. The TransformListd constructed by GetUnitCellTransform() has a bScaleInvertsOrientation flag that can be used
 *           to detect this and (eg) flip orientation on any generated mesh triangles/etc.
 *
 *  GS::GetUnitCellTransform() can be used to construct a sequence of scales/rotations/translations (as a FTransformListd) that implement
 *   this cell transform. 
 */
struct GRADIENTSPACEGRID_API ModelGridCellData_StandardRST
{
	union Parameters
	{
		// WARNING: bit-field element cannot cross byte-boundary, this will insert padding.
		// this struct must (currently) remain 64 bits!
		struct {
			// 8 bits for transform type and rotation
			uint8_t TransformType : 3;		// 0 is this transform below, 1-7 reserved for future use
											// (presumably in those types the following fields would be different)
			uint8_t AxisDirection : 3;		// +Z, +Y, +X, -X, -Y, -Z		(only values 0-5 are valid, 6/7 unused)
			uint8_t AxisRotation : 2;		// 0, 90, 180, 270

			// 8 bits for dimension mode and Z dimension (2 bits available)
			uint8_t DimensionMode : 2;		// this is a EModelGridCellDimensionType value - max 4 options
			uint8_t ReservedD : 2;
			uint8_t DimensionZ : 4;			// dimension has 16 possible values, interpreted relative to DimensionMode

			// 8 bits for X/Y dimension
			uint8_t DimensionX : 4;
			uint8_t DimensionY : 4;

			// 8 bits for X translation (3 bits available)
			// 5-bit translation provides 2x the # of steps as the minimum 4-bit dimension, which means things can be centered
			uint8_t TranslateX : 5;
			uint8_t FlipX : 1;
			uint8_t ReservedX : 2;		// future use - mirror/flip, etc

			// 8 bits for Y translation (3 bits available)
			uint8_t TranslateY : 5;
			uint8_t FlipY : 1;
			uint8_t ReservedY : 2;

			// 8 bits for Z translation (3 bits available)
			uint8_t TranslateZ : 5;
			uint8_t FlipZ : 1;
			uint8_t ReservedZ : 2;

			// at this point 48 bits are used, 16 bits are available for additional use
			uint16_t ExtendedData;
		};
		uint64_t Fields;
	};
	Parameters Params;


	// maximum-values for the various fields these are inclusive-max values, ie value must be <=
	static constexpr unsigned int MaxRotationAxis = 5;
	static constexpr unsigned int MaxRotationAngle = 3;
	static constexpr unsigned int MaxDimensionMode = 3;
	static constexpr unsigned int MaxDimension = 15;
	static constexpr unsigned int MaxTranslate = 31;
	static constexpr unsigned int MaxTranslate_Thirds = 23;

	//ModelGridCellData_StandardRST(const ModelGridCell& Cell)
	//{
	//	Params.Fields = Cell.CellData;
	//}

	static bool IsSubType(EModelGridCellType CellType);
};




struct GRADIENTSPACEGRID_API MGCell_Slab : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Slab_Parametric; }
	static MGCell_Slab GetDefaultCellParams();

	// todo get out of class somehow
	static void DetermineOrientationFromAxis(const Vector3d& AxisZ, uint8_t& Axis);
};


struct GRADIENTSPACEGRID_API MGCell_Ramp : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Ramp_Parametric; }
	static MGCell_Ramp GetDefaultCellParams();

	static void DetermineOrientationFromAxes(const Vector3d& UpAxis, const Vector3d& ForwardAxis, uint8_t& Axis, uint8_t& Angle);
	void OrientFromAxes(const Vector3d& UpAxis, const Vector3d& ForwardAxis);
};

struct GRADIENTSPACEGRID_API MGCell_Corner : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Corner_Parametric; }
	static MGCell_Corner GetDefaultCellParams();

	// todo get out of class somehow
	static void DetermineOrientationFromDiagonal(const Vector3d& CornerDir, uint8_t& Axis, uint8_t& Angle);
};

struct GRADIENTSPACEGRID_API MGCell_CutCorner : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::CutCorner_Parametric; }
	static MGCell_CutCorner GetDefaultCellParams();
};

struct GRADIENTSPACEGRID_API MGCell_Pyramid : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Pyramid_Parametric; }
	static MGCell_Pyramid GetDefaultCellParams();
};

struct GRADIENTSPACEGRID_API MGCell_Peak : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Peak_Parametric; }
	static MGCell_Peak GetDefaultCellParams();
};

struct GRADIENTSPACEGRID_API MGCell_Cylinder : public ModelGridCellData_StandardRST
{
	EModelGridCellType GetCellType() const { return EModelGridCellType::Cylinder_Parametric; }
	static MGCell_Cylinder GetDefaultCellParams();
};


template<typename SubCellType>
ModelGridCell MakeDefaultCell()
{
	SubCellType TypedSubCell = SubCellType::GetDefaultCellParams();
	return  ModelGridCell{ TypedSubCell.GetCellType(), TypedSubCell.Params.Fields, EGridCellMaterialType::SolidColor, GridMaterial::White() };
}

template<typename SubCellType>
void InitializeSubCellFromGridCell(const ModelGridCell& SourceCell, SubCellType& SubCellToInit)
{
	SubCellToInit.Params.Fields = SourceCell.CellData;
}


template<typename SubCellType>
void UpdateGridCellParamsFromSubCell(ModelGridCell& CellToUpdate, const SubCellType& SubCell)
{
	CellToUpdate.CellData = SubCell.Params.Fields;
}


template<typename SubCellType> 
void UpdateGridCellFromSubCell(ModelGridCell& CellToUpdate, const SubCellType& SubCell)
{
	CellToUpdate.CellType = SubCell.GetCellType();
	CellToUpdate.CellData = SubCell.Params.Fields;
}

GRADIENTSPACEGRID_API
void GetUnitCellTransform(const ModelGridCellData_StandardRST& SubCell, const Vector3d& UnitCellDimensions, TransformListd& TransformSeqOut, bool bIgnoreSubCellDimensions = false);

GRADIENTSPACEGRID_API
void GetUnitCellTransform(const ModelGridCell& CellInfo, const Vector3d& UnitCellDimensions, TransformListd& TransformSeqOut, bool bIgnoreSubCellDimensions = false);

GRADIENTSPACEGRID_API
ModelGridCell MakeDefaultCellFromType(EModelGridCellType CellType);

} // end namespace GS
