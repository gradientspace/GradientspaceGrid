// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "Color/GSColor3b.h"
#include "Math/GSIntVector2.h"
#include "Math/GSAxisBox3.h"
#include "ModelGrid/MaterialReferenceSet.h"
#include "Mesh/GenericMeshAPI.h"
#include "Mesh/PolyMesh.h"

namespace GS
{

class GRADIENTSPACEGRID_API ModelGridMesher
{
public:
	bool bIncludeUVs = true;

	struct CellMaterials
	{
		EGridCellMaterialType CellType = EGridCellMaterialType::SolidColor;
		GridMaterial CellMaterial = GridMaterial::White();
		CellFaceMaterials FaceMaterials;
	};


public:
	PolyMesh UnitBoxMesh_Poly;
	int UnitBoxMeshFaceDirections[6];

	PolyMesh UnitRampMesh_Poly;
	PolyMesh UnitCornerMesh_Poly;
	PolyMesh UnitPyramidMesh_Poly;
	PolyMesh UnitPeakMesh_Poly;
	PolyMesh UnitCutCornerMesh_Poly;
	PolyMesh UnitCylinderMesh_Poly;

	struct AppendCache
	{
		static constexpr int CacheSize = 128;
		GS::unsafe_vector<int> VertexMap;
		GS::unsafe_vector<int> GroupMap;
		GS::unsafe_vector<int> NormalMap;
		GS::unsafe_vector<int> ColorMap;
		GS::unsafe_vector<int> UVMap;
	};
	void InitAppendCache(AppendCache& Cache) const;
	void ResetAppendCache(AppendCache& Cache, bool bOnlyAttribs) const;


	void Initialize(Vector3d CellDimensions);

	void AppendHitTestBox(const AxisBox3d& LocalBounds, IMeshBuilder& AppendToMesh, AppendCache& Cache);

	void AppendBox(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendRamp(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendCorner(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendPyramid(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendPeak(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendCylinder(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendCutCorner(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);

	void AppendVariableCutCorner(const AxisBox3d& LocalBounds, const CellMaterials& Materials,
		IMeshBuilder& AppendToMesh, TransformListd& Transforms,
		uint8_t ParamA, uint8_t ParamB, uint8_t ParamC);

	void AppendVariableCutEdge(const AxisBox3d& LocalBounds, const CellMaterials& Materials,
		IMeshBuilder& AppendToMesh, TransformListd& Transforms,
		uint8_t ParamA, uint8_t ParamB);

	void AppendBoxFaces(const AxisBox3d& LocalBounds, const CellMaterials& Materials, int VisibleFacesMask, IMeshBuilder& AppendToMesh, AppendCache& Cache);


protected:
	void AppendStandardCellMesh(const PolyMesh& UnitCellMesh, const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
		TransformListd& Transforms, AppendCache& Cache);
};








}
