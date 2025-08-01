// Copyright Gradientspace Corp. All Rights Reserved.

#include "ModelGrid/ModelGridMesher.h"
#include "GenericGrid/BoxIndexing.h"
#include "Math/GSAxisBox2.h"
#include "Math/GSFrame3.h"

using namespace GS;


// undefine this to enable some static mutexes in various functions, to try to avoid multithread issues
//#define GS_FORCE_SINGLE_THREAD


static Vector3f GetFaceNormal(const InlineVec3dList& FaceVerts)
{
	// not really sure if this is right...
	int NV = FaceVerts.Size();
	if (NV == 0) return Vector3f::Zero();
	Vector3d SumNormal = GS::Normal(FaceVerts[0], FaceVerts[1], FaceVerts[2]);
	for (int j = 2; j < NV - 1; ++j)
	{
		Vector3d N = GS::Normal(FaceVerts[0], FaceVerts[j], FaceVerts[j + 1]);
		SumNormal += N;
	}
	return Normalized((Vector3f)SumNormal);
}
static Vector3f GetFaceNormal(const PolyMesh& Mesh, int FaceIndex)
{
	PolyMesh::Face face = Mesh.GetFace(FaceIndex);
	InlineVec3dList FaceVerts;
	Mesh.GetFaceVertexPositions(face, FaceVerts);
	return GetFaceNormal(FaceVerts);
}

static void ComputeGroupsFromBoxFaces(PolyMesh& Mesh)
{
	int NumFaces = Mesh.GetFaceCount();
	for (int fid = 0; fid < NumFaces; ++fid)
	{
		Vector3f FaceNormal = GetFaceNormal(Mesh, fid);
		int GroupID = (int)GS::NormalToFaceIndex(FaceNormal);
		Mesh.SetFaceGroup(fid, GroupID);
	}
}


static void SetToPerFaceNormals(PolyMesh& Mesh)
{
	gs_debug_assert(Mesh.GetNumNormalSets() == 1);
	int NumFaces = Mesh.GetFaceCount();
	
	PolyMeshNormals& Normals = Mesh.GetNormalSets();
	for (int FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
	{
		PolyMesh::Face face = Mesh.GetFace(FaceIndex);
		InlineVec3dList FaceVerts;
		Mesh.GetFaceVertexPositions(face, FaceVerts);

		Vector3f FaceNormal = GetFaceNormal(FaceVerts);
		int ElementID = Normals.AppendElementToLastSet(FaceNormal);

		if (face.IsTriangle())
			Normals.SetTriangle(face.Index, Index3i(ElementID, ElementID, ElementID), 0);
		else if (face.IsQuad())
			Normals.SetQuad(face.Index, Index4i(ElementID, ElementID, ElementID, ElementID), 0);
		else if (face.IsPolygon()) {
			PolyMesh::Polygon& Poly = Mesh.GetEditablePolygon(face.Index);
			Poly.Normals.initialize(Poly.VertexCount, ElementID);
		}
	}
}


static void GenerateFaceUVs(PolyMesh& Mesh, bool bPreserveFaceAspectRatios = true)
{
	gs_debug_assert(Mesh.GetNumUVSets() == 1);
	int NumFaces = Mesh.GetFaceCount();

	PolyMeshUVs& UVs = Mesh.GetUVSets();
	for (int FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
	{
		PolyMesh::Face face = Mesh.GetFace(FaceIndex);
		InlineIndexList FaceIndices;
		Mesh.GetFaceVertexIndices(face, FaceIndices);
		InlineVec3dList FaceVerts;
		Mesh.GetFaceVertexPositions(face, FaceVerts);
		int NumFaceVerts = FaceVerts.Size();

		Vector3f FaceNormal = GetFaceNormal(FaceVerts);
		Frame3d ProjFrame( Mesh.GetPosition(FaceIndices[0]), (Vector3d)FaceNormal);

		AxisBox2d UVBounds = AxisBox2d::Empty();
		InlineVec2dList UVValues(NumFaceVerts);
		for (int i = 0; i < NumFaceVerts; ++i) {
			Vector2d UV = ProjFrame.ToPlaneXY(FaceVerts[i]);
			UVValues[i] = UV;
			UVBounds.Contain(UV);
		}

		// compute scaling factors
		AxisBox2d TargetUVBounds(Vector2d::Zero(), Vector2d::One());
		double WidthScale = TargetUVBounds.DimensionX() / UVBounds.DimensionX();
		double HeightScale = TargetUVBounds.DimensionY() / UVBounds.DimensionY();
		if (bPreserveFaceAspectRatios) {
			WidthScale = GS::Min(WidthScale, HeightScale);
			HeightScale = WidthScale;
		}

		// transform UVs
		Vector2d ScaleOrigin = UVBounds.Center();
		Vector2d Translation = TargetUVBounds.Center();
		for ( int i = 0; i < NumFaceVerts; ++i ) {
			Vector2d InitialUV = UVValues[i];
			Vector2d UVTransformed((InitialUV.X - ScaleOrigin.X) * WidthScale,
								   (InitialUV.Y - ScaleOrigin.Y) * HeightScale);
			UVValues[i] = UVTransformed + Translation;
		}

		// create in UV list
		InlineIndexList UVIndices(NumFaceVerts);
		for (int i = 0; i < NumFaceVerts; ++i) {
			UVIndices[i] = UVs.AppendElementToLastSet(UVValues[i]);
		}

		if (face.IsTriangle())
			UVs.SetTriangle(face.Index, Index3i(UVIndices[0], UVIndices[1], UVIndices[2]), 0);
		else if (face.IsQuad())
			UVs.SetQuad(face.Index, Index4i(UVIndices[0], UVIndices[1], UVIndices[2], UVIndices[3]), 0);
		else if (face.IsPolygon()) {
			PolyMesh::Polygon& Poly = Mesh.GetEditablePolygon(face.Index);
			Poly.UVs.initialize(Poly.VertexCount, UVIndices);
		}
	}


}


struct CylinderOptions
{
	//! if true, cylinder will be rotated by a quarter-slice turn, so that "flat" sides are aligned with the axes, instead of a vertex on the axis
	bool bShiftByHalfStep = false;
};

static void GenerateCylinder(PolyMesh& Mesh, 
	uint32_t Slices = 8, 
	double Radius = 0.5, 
	double Height = 1.0,
	CylinderOptions Options = CylinderOptions())
{
	Mesh.SetNumGroupSets(1);
	Mesh.AddNormalSet(Slices + 2);
	Mesh.ReserveVertices(2 * Slices);
	Mesh.ReserveFaces(Slices + 2);
	Mesh.ReservePolygons(2);
	Mesh.ReserveQuads(Slices);

	Mesh.SetNormal(0, -Vector3f::UnitZ());
	Mesh.SetNormal(1, Vector3f::UnitZ());

	PolyMesh::Polygon Bottom;
	Bottom.Vertices.reserve(Slices);
	Bottom.Normals.initialize(Slices, 0);

	double shift = (Options.bShiftByHalfStep) ? 0.5 : 0;
	for (uint32_t i = 0; i < Slices; ++i)
	{
		double t = ((double)i + shift) / (double)Slices;
		double angle = t * (2.0 * Mathd::Pi());
		double x = GS::Cos(angle), y = GS::Sin(angle);
		int vid = Mesh.AddVertex(Vector3d(Radius*x, Radius*y, 0));
		Bottom.Vertices.add(vid);
	}
	Bottom.VertexCount = (int)Bottom.Vertices.size();

	PolyMesh::Polygon Top = Bottom;
	Top.Normals.initialize(Slices, 1);
	for (uint32_t i = 0; i < Slices; ++i)
	{
		Vector3d V = Mesh.GetPosition(Bottom.Vertices[i]);
		V.Z = Height;
		Top.Vertices[Slices-i-1] = Mesh.AddVertex(V);	// have to reverse the orientation here
	}

	Mesh.AddPolygon(std::move(Bottom), /*groupid=*/0);
	Mesh.AddPolygon(std::move(Top), /*groupid=*/1);

	for (uint32_t i = 0; i < Slices; ++i)
	{
		int a = i, b = (i+1)%Slices;
		int c = b + Slices, d = a + Slices;
		Vector3f Normal = (Vector3f)GS::Normal(Mesh.GetPosition(a), Mesh.GetPosition(d), Mesh.GetPosition(c));
		Mesh.SetNormal(i+2, Normal);
		Index4i QuadNormal(i+2);
		int GroupID = i;
		Mesh.AddQuad( Index4i(a,d,c,b), GroupID, &QuadNormal);
	}
}


void ModelGridMesher::Initialize(Vector3d CellDimensions)
{
#ifdef GS_FORCE_SINGLE_THREAD
	static std::mutex threadLock;	const std::lock_guard<std::mutex> _scopedlock(threadLock);
#endif

	// these values correspond to BoxIndexing.h indexing
	const int GroupID_PlusX = 0;
	const int GroupID_MinusX = 1;
	const int GroupID_PlusY = 2;
	const int GroupID_MinusY = 3;
	const int GroupID_PlusZ = 4;
	const int GroupID_MinusZ = 5;
	const int GroupID_Six = 6;


	// Box
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int B = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int C = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, 0));
		int D = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int E = Mesh.AddVertex(Vector3d(0, 0, CellDimensions.Z));
		int F = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, CellDimensions.Z));
		int G = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, CellDimensions.Z));
		int H = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, CellDimensions.Z));
		PolyMesh::FaceInfo rightx = Mesh.AddQuad(Index4i(C, B, F, G), 4);
		PolyMesh::FaceInfo leftx = Mesh.AddQuad(Index4i(E, A, D, H), 3);
		PolyMesh::FaceInfo fronty = Mesh.AddQuad(Index4i(H, D, C, G), 6);
		PolyMesh::FaceInfo backy = Mesh.AddQuad(Index4i(A, E, F, B), 5);
		PolyMesh::FaceInfo topz = Mesh.AddQuad(Index4i(E, H, G, F), 2);
		PolyMesh::FaceInfo basez = Mesh.AddQuad(Index4i(A, B, C, D), 1);
		ComputeGroupsFromBoxFaces(Mesh);
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitBoxMesh_Poly = std::move(Mesh);

		UnitBoxMeshFaceDirections[0] = 0;
		UnitBoxMeshFaceDirections[1] = 1;
		UnitBoxMeshFaceDirections[2] = 2;
		UnitBoxMeshFaceDirections[3] = 3;
		UnitBoxMeshFaceDirections[4] = 4;
		UnitBoxMeshFaceDirections[5] = 5;
	}


	// Ramp
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int B = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int C = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, 0));
		int D = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int E = Mesh.AddVertex(Vector3d(0, 0, CellDimensions.Z));
		int F = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, CellDimensions.Z));
		PolyMesh::FaceInfo base = Mesh.AddQuad(Index4i(A, B, C, D), GroupID_MinusZ);
		PolyMesh::FaceInfo quadXZ = Mesh.AddQuad(Index4i(A, E, F, B), GroupID_MinusY);
		PolyMesh::FaceInfo ramp = Mesh.AddQuad(Index4i(F, E, D, C), GroupID_PlusZ);
		PolyMesh::FaceInfo tri1 = Mesh.AddTriangle(Index3i(E, A, D), GroupID_MinusX);
		PolyMesh::FaceInfo tri2 = Mesh.AddTriangle(Index3i(C, B, F), GroupID_PlusX);
		//ComputeGroupsFromBoxFaces(Mesh);		// groups are explicitly assigned
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitRampMesh_Poly = std::move(Mesh);
	}


	// Corner
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int B = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int C = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int D = Mesh.AddVertex(Vector3d(0, 0, CellDimensions.Z));
		PolyMesh::FaceInfo base = Mesh.AddTriangle(Index3i(A, B, C), GroupID_MinusZ);
		PolyMesh::FaceInfo side = Mesh.AddTriangle(Index3i(A, C, D), GroupID_MinusX);
		PolyMesh::FaceInfo back = Mesh.AddTriangle(Index3i(A, D, B), GroupID_MinusY);
		PolyMesh::FaceInfo ramp = Mesh.AddTriangle(Index3i(B, D, C), GroupID_PlusZ);
		//ComputeGroupsFromBoxFaces(Mesh);		// groups are explicitly assigned
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitCornerMesh_Poly = std::move(Mesh);
	}


	// Pyramid
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int X = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int XY = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, 0));
		int Y = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int TipV = Mesh.AddVertex(Vector3d(CellDimensions.X / 2, CellDimensions.Y / 2, CellDimensions.Z));
		PolyMesh::FaceInfo base = Mesh.AddQuad(Index4i(A, X, XY, Y), GroupID_MinusZ);
		PolyMesh::FaceInfo tSide0 = Mesh.AddTriangle(Index3i(X, A, TipV), GroupID_MinusY);
		PolyMesh::FaceInfo tSide1 = Mesh.AddTriangle(Index3i(XY, X, TipV), GroupID_PlusX);
		PolyMesh::FaceInfo tSide2 = Mesh.AddTriangle(Index3i(Y, XY, TipV), GroupID_PlusY);
		PolyMesh::FaceInfo tSide3 = Mesh.AddTriangle(Index3i(A, Y, TipV), GroupID_MinusX);
		//ComputeGroupsFromBoxFaces(Mesh);		// groups are explicitly assigned
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitPyramidMesh_Poly = std::move(Mesh);
	}


	// Peak
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int B = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int C = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, 0));
		int D = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int TipA = Mesh.AddVertex(Vector3d(0, CellDimensions.Y / 2, CellDimensions.Z));
		int TipB = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y / 2, CellDimensions.Z));
		PolyMesh::FaceInfo base = Mesh.AddQuad(Index4i(A, B, C, D), GroupID_MinusZ);
		PolyMesh::FaceInfo endA = Mesh.AddTriangle(Index3i(A, D, TipA), GroupID_MinusX);
		PolyMesh::FaceInfo endB = Mesh.AddTriangle(Index3i(C, B, TipB), GroupID_PlusX);
		PolyMesh::FaceInfo sideA = Mesh.AddQuad(Index4i(D, C, TipB, TipA), GroupID_PlusY);
		PolyMesh::FaceInfo sideB = Mesh.AddQuad(Index4i(B, A, TipA, TipB), GroupID_MinusY);
		//ComputeGroupsFromBoxFaces(Mesh);		// groups are explicitly assigned
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitPeakMesh_Poly = std::move(Mesh);
	}


	// CutCorner
	{
		PolyMesh Mesh;
		Mesh.SetNumGroupSets(1);	Mesh.AddNormalSet(0);	Mesh.AddUVSet(0);
		int A = Mesh.AddVertex(Vector3d::Zero());
		int B = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, 0));
		int C = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, 0));
		int D = Mesh.AddVertex(Vector3d(0, 0, CellDimensions.Z));
		int E = Mesh.AddVertex(Vector3d(CellDimensions.X, 0, CellDimensions.Z));
		int F = Mesh.AddVertex(Vector3d(0, CellDimensions.Y, CellDimensions.Z));
		int G = Mesh.AddVertex(Vector3d(CellDimensions.X, CellDimensions.Y, 0));
		PolyMesh::FaceInfo baseXY = Mesh.AddQuad(Index4i(A, B, G, C), GroupID_MinusZ);
		PolyMesh::FaceInfo sideYZ = Mesh.AddQuad(Index4i(A, C, F, D), GroupID_MinusX);
		PolyMesh::FaceInfo sideXZ = Mesh.AddQuad(Index4i(A, D, E, B), GroupID_MinusY);
		PolyMesh::FaceInfo top = Mesh.AddTriangle(Index3i(D, F, E), GroupID_PlusZ);
		PolyMesh::FaceInfo front = Mesh.AddTriangle(Index3i(F, C, G), GroupID_PlusY);
		PolyMesh::FaceInfo farside = Mesh.AddTriangle(Index3i(G, B, E), GroupID_PlusX);
		PolyMesh::FaceInfo cutface = Mesh.AddTriangle(Index3i(E, F, G), GroupID_Six); // cut face requires additional group outside the 0-5 standard faces range
		//ComputeGroupsFromBoxFaces(Mesh);		// groups are explicitly assigned
		//Mesh.SetFaceGroup(cutface.FaceIndex, 6);		// cut face requires additional group outside the 0-5 standard faces range
		SetToPerFaceNormals(Mesh);
		GenerateFaceUVs(Mesh);
		UnitCutCornerMesh_Poly = std::move(Mesh);
	}


	{
		PolyMesh Mesh;
		Mesh.AddUVSet(0);
		GenerateCylinder(Mesh, 8, 0.5, 1.0);
		Mesh.Scale(CellDimensions);
		Mesh.Translate(Vector3d(CellDimensions.X / 2, CellDimensions.Y / 2, 0));
		ComputeGroupsFromBoxFaces(Mesh);
		GenerateFaceUVs(Mesh);
		UnitCylinderMesh_Poly = std::move(Mesh);
	}
}


void ModelGridMesher::InitAppendCache(AppendCache& Cache) const
{
	// could this just be done in AppendCache constructor ??
	// need to ensure these are enough for all unit meshes
	Cache.VertexMap.resize(AppendCache::CacheSize);
	Cache.GroupMap.resize(AppendCache::CacheSize);
	Cache.NormalMap.resize(AppendCache::CacheSize);
	Cache.ColorMap.resize(AppendCache::CacheSize);
	Cache.UVMap.resize(AppendCache::CacheSize);
}

void ModelGridMesher::ResetAppendCache(AppendCache& Cache, bool bOnlyAttribs) const
{
	if (!bOnlyAttribs)
	{
		Cache.VertexMap.initialize(AppendCache::CacheSize, -1);
		Cache.GroupMap.initialize(AppendCache::CacheSize, -1);
	}
	Cache.NormalMap.initialize(AppendCache::CacheSize, -1);
	Cache.ColorMap.initialize(AppendCache::CacheSize, -1);
	Cache.UVMap.initialize(AppendCache::CacheSize, -1);
}


void ModelGridMesher::AppendHitTestBox(const AxisBox3d& LocalBounds, IMeshBuilder& AppendToMesh, AppendCache& Cache)
{
	for (int vid = 0; vid < 8; ++vid)
	{
		Vector3d P = UnitBoxMesh_Poly.GetPosition(vid);
		int new_vid = AppendToMesh.AppendVertex(LocalBounds.Min + P);
		Cache.VertexMap[vid] = new_vid;
	}
	int GroupID = 0;
	for (int qid = 0; qid < 6; ++qid)
	{
		Index4i Quad = UnitBoxMesh_Poly.GetQuad(qid);
		Index3i Tri(Cache.VertexMap[Quad.A], Cache.VertexMap[Quad.B], Cache.VertexMap[Quad.C]);
		AppendToMesh.AppendTriangle(Tri, GroupID);
		Tri.B = Tri.C; Tri.C = Cache.VertexMap[Quad.D];
		AppendToMesh.AppendTriangle(Tri, GroupID);
	}
}


void ModelGridMesher::AppendBox(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
	TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitBoxMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendRamp(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitRampMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendCorner(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitCornerMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendPyramid(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitPyramidMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendPeak(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitPeakMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendCylinder(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitCylinderMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}

void ModelGridMesher::AppendCutCorner(const AxisBox3d& LocalBounds, const CellMaterials& Materials, IMeshBuilder& AppendToMesh, TransformListd& Transforms, AppendCache& Cache)
{
	AppendStandardCellMesh(UnitCutCornerMesh_Poly, LocalBounds, Materials, AppendToMesh, Transforms, Cache);
}





void ModelGridMesher::AppendStandardCellMesh(
	const PolyMesh& UnitCellMesh, const AxisBox3d& LocalBounds,
	const CellMaterials& Materials, IMeshBuilder& AppendToMesh,
	TransformListd& Transforms, AppendCache& Cache)
{
#ifdef GS_FORCE_SINGLE_THREAD
	static std::mutex threadLock;	const std::lock_guard<std::mutex> _scopedlock(threadLock);
#endif

	bool bHaveCellMatIndex = (Materials.CellType == EGridCellMaterialType::SolidRGBIndex);
	int CellMatIndex = (bHaveCellMatIndex) ? Materials.CellMaterial.GetIndex8() : 0;
	bool bUseFaceColors = (Materials.CellType == EGridCellMaterialType::FaceColors);
	Vector4f CellColor = (!bUseFaceColors) ? Materials.CellMaterial.AsVector4f(true,!bHaveCellMatIndex) : Vector4f::One();

	bool bReverseOrientation = Transforms.bScaleInvertsOrientation;

	ResetAppendCache(Cache, false);

	int VertexCount = UnitCellMesh.GetVertexCount();
	for (int vid = 0; vid < VertexCount; ++vid)
	{
		Vector3d P = UnitCellMesh.GetPosition(vid);
		P = Transforms.TransformPosition(P);

		int new_vid = AppendToMesh.AppendVertex(LocalBounds.Min + P);
		Cache.VertexMap[vid] = new_vid;

		if (!bUseFaceColors) {
			int new_cid = AppendToMesh.AppendColor(CellColor, true);
			Cache.ColorMap[vid] = new_cid;
		}
	}

	int FaceCount = UnitCellMesh.GetFaceCount();
	for (int fid = 0; fid < FaceCount; ++fid ) {
		int GroupID = UnitCellMesh.GetFaceGroup(fid);
		if (Cache.GroupMap[GroupID] == -1)
			Cache.GroupMap[GroupID] = AppendToMesh.AllocateGroupID();
	}

	for (int fid = 0; fid < FaceCount; ++fid)
	{
		PolyMesh::Face face = UnitCellMesh.GetFace(fid);
		int UnitGroupID = UnitCellMesh.GetFaceGroup(fid);
		int AppendGroupID = Cache.GroupMap[UnitGroupID];

		InlineIndexList Vertices;
		bool bOK = UnitCellMesh.GetFaceVertexIndices(face, Vertices);
		gs_debug_assert(bOK);
		int NV = Vertices.Size();

		Vector3f FaceNormal = UnitCellMesh.GetFaceVertexNormal(face, 0);
		FaceNormal = (Vector3f)Transforms.TransformNormal((Vector3d)FaceNormal);
		InlineIndexList NormalIndices(NV);
		for (int j = 0; j < NV; ++j)
			NormalIndices[j] = AppendToMesh.AppendNormal(FaceNormal);

		int d1 = 0, d2 = 1;
		if (bReverseOrientation) {
			d1 = 1; d2 = 0;
		}

		int NT = (NV-2);
		InlineIndexList Triangles; int TriCount = 0;
		InlineIndex3List PolyTriangles;
		for (int j = 1; j < NV-1; ++j)
		{
			Index3i NewTri(Cache.VertexMap[Vertices[0]], Cache.VertexMap[Vertices[j + d1]], Cache.VertexMap[Vertices[j + d2]]);
			int NewTriID = AppendToMesh.AppendTriangle(NewTri, AppendGroupID);
			if (NewTriID >= 0)
			{
				Triangles.AddValue(NewTriID);
				PolyTriangles.AddValue( Index3i(0, j + d1, j + d2) );
				AppendToMesh.SetTriangleNormals(NewTriID, Index3i(NormalIndices[0], NormalIndices[j + d1], NormalIndices[j + d2]));
			}
		}
		NT = Triangles.Size();		// if geo was bad we might have lost some triangles
		
		if (bUseFaceColors)
		{
			// HAAACK - assuming material 0 is the face color material?
			for ( int j = 0; j < NT; ++j )
				AppendToMesh.SetMaterialID(Triangles[j], 0);		// HAACK

			Index3i ColorTri;
			int UseFaceMaterialIndex = (UnitGroupID >= 0 && UnitGroupID < CellFaceMaterials::MaxFaces) ? UnitGroupID : 0;
			Vector4f FaceColor = Materials.FaceMaterials.Faces[UseFaceMaterialIndex].AsVector4f(true, true);
			InlineIndexList ColorIndices(NV);
			for (int j = 0; j < NV; ++j)
				ColorIndices[j] = AppendToMesh.AppendColor(FaceColor, true);
			for (int j = 1; j < NV - 1; ++j) {
				AppendToMesh.SetTriangleColors(Triangles[j-1], Index3i(ColorIndices[0], ColorIndices[j + d1], ColorIndices[j + d2]));
			}
		}
		else
		{
			for (int j = 0; j < NT; ++j)
				AppendToMesh.SetMaterialID(Triangles[j], CellMatIndex);

			for (int j = 1; j < NV-1; ++j) {
				int a = Vertices[0], b = Vertices[j + d1], c = Vertices[j + d2];
				Index3i ColorTri(Cache.ColorMap[a], Cache.ColorMap[b], Cache.ColorMap[c]);
				AppendToMesh.SetTriangleColors(Triangles[j-1], ColorTri);
			}
		}

		if (bIncludeUVs && UnitCellMesh.GetNumUVSets() == 1)
		{
			InlineIndexList UVIndices(NV);
			for (int j = 0; j < NV; ++j) {
				Vector2d UV = UnitCellMesh.GetFaceVertexUV(face, j, 0);
				UVIndices[j] = AppendToMesh.AppendUV((Vector2f)UV);
			}
			for (int j = 0; j < NT; ++j) {
				Index3i PolyTri = PolyTriangles[j];
				Index3i UVTri( UVIndices[PolyTri.A], UVIndices[PolyTri.B], UVIndices[PolyTri.C] );
				AppendToMesh.SetTriangleUVs( Triangles[j], UVTri );
			}
		}

	}
}




void ModelGridMesher::AppendBoxFaces(const AxisBox3d& LocalBounds, const CellMaterials& Materials, int VisibleFacesMask, IMeshBuilder& AppendToMesh, AppendCache& Cache)
{
#ifdef GS_FORCE_SINGLE_THREAD
	static std::mutex threadLock;	const std::lock_guard<std::mutex> _scopedlock(threadLock);
#endif

	bool bHaveCellMatIndex = (Materials.CellType == EGridCellMaterialType::SolidRGBIndex);
	int CellMatIndex = (bHaveCellMatIndex) ? Materials.CellMaterial.GetIndex8() : 0;
	bool bUseFaceColors = (Materials.CellType == EGridCellMaterialType::FaceColors);
	Vector4f CellColor = (!bUseFaceColors) ? Materials.CellMaterial.AsVector4f(true, !bHaveCellMatIndex) : Vector4f::One();

	ResetAppendCache(Cache, false);

	// todo we are appending things here that will not be used!! should
	// only append verts of unmasked faces!

	int VertexCount = UnitBoxMesh_Poly.GetVertexCount();		// 8!
	for (int vid = 0; vid < VertexCount; ++vid)
	{
		Vector3d P = UnitBoxMesh_Poly.GetPosition(vid);
		//P = Transforms.TransformPosition(P);
		int new_vid = AppendToMesh.AppendVertex(LocalBounds.Min + P);
		Cache.VertexMap[vid] = new_vid;

		if (!bUseFaceColors) {
			int new_cid = AppendToMesh.AppendColor(CellColor, true);
			Cache.ColorMap[vid] = new_cid;
		}
	}

	int FaceCount = UnitBoxMesh_Poly.GetFaceCount();		// 6! and faces are unique!
	for (int fid = 0; fid < FaceCount; ++fid) {
		int GroupID = UnitBoxMesh_Poly.GetFaceGroup(fid);
		if (Cache.GroupMap[GroupID] == -1)
			Cache.GroupMap[GroupID] = AppendToMesh.AllocateGroupID();
	}

	for (int fid = 0; fid < FaceCount; ++fid)
	{
		PolyMesh::Face face = UnitBoxMesh_Poly.GetFace(fid);
		int UnitGroupID = UnitBoxMesh_Poly.GetFaceGroup(fid);
		int AppendGroupID = Cache.GroupMap[UnitGroupID];

		int FaceDir = fid; // UnitBoxMeshFaceDirections[fid];	// box faces match up w/ other face ordering stuff
		if ((VisibleFacesMask & (1 << FaceDir)) == 0) continue;

		InlineIndexList Vertices;
		bool bOK = UnitBoxMesh_Poly.GetFaceVertexIndices(face, Vertices);
		gs_debug_assert(bOK);
		int NV = Vertices.Size();

		Vector3f FaceNormal = UnitBoxMesh_Poly.GetFaceVertexNormal(face, 0);
		//FaceNormal = (Vector3f)Transforms.TransformNormal((Vector3d)FaceNormal);
		InlineIndexList NormalIndices(NV);
		for (int j = 0; j < NV; ++j)
			NormalIndices[j] = AppendToMesh.AppendNormal(FaceNormal);

		int NT = (NV - 2);
		InlineIndexList Triangles; int TriCount = 0;
		InlineIndex3List PolyTriangles;
		for (int j = 1; j < NV - 1; ++j)
		{
			int a = Vertices[0], b = Vertices[j], c = Vertices[j + 1];
			Index3i NewTri(Cache.VertexMap[a], Cache.VertexMap[b], Cache.VertexMap[c]);
			int NewTriID = AppendToMesh.AppendTriangle(NewTri, AppendGroupID);
			Triangles.AddValue(NewTriID);
			PolyTriangles.AddValue(Index3i(0, j, j + 1));
			AppendToMesh.SetTriangleNormals(NewTriID, Index3i(NormalIndices[0], NormalIndices[j], NormalIndices[j + 1]));
		}
		gs_debug_assert(Triangles.Size() == NT);

		if (bUseFaceColors)
		{
			// HAAACK - assuming material 0 is the face color material?
			for (int j = 0; j < NT; ++j)
				AppendToMesh.SetMaterialID(Triangles[j], 0);		// HAACK

			Index3i ColorTri;
			int UseFaceMaterialIndex = (UnitGroupID >= 0 && UnitGroupID < CellFaceMaterials::MaxFaces) ? UnitGroupID : 0;
			Vector4f FaceColor = Materials.FaceMaterials.Faces[UseFaceMaterialIndex].AsVector4f(true, true);
			InlineIndexList ColorIndices(NV);
			for (int j = 0; j < NV; ++j)
				ColorIndices[j] = AppendToMesh.AppendColor(FaceColor, true);
			for (int j = 1; j < NV - 1; ++j)
				AppendToMesh.SetTriangleColors(Triangles[j - 1], Index3i(ColorIndices[0], ColorIndices[j], ColorIndices[j + 1]));
		}
		else
		{
			for (int j = 0; j < NT; ++j)
				AppendToMesh.SetMaterialID(Triangles[j], CellMatIndex);

			for (int j = 1; j < NV - 1; ++j) {
				int a = Vertices[0], b = Vertices[j], c = Vertices[j + 1];
				Index3i ColorTri(Cache.ColorMap[a], Cache.ColorMap[b], Cache.ColorMap[c]);
				AppendToMesh.SetTriangleColors(Triangles[j - 1], ColorTri);
			}
		}

		if (bIncludeUVs && UnitBoxMesh_Poly.GetNumUVSets() == 1)
		{
			InlineIndexList UVIndices(NV);
			for (int j = 0; j < NV; ++j) {
				Vector2d UV = UnitBoxMesh_Poly.GetFaceVertexUV(face, j, 0);
				int NewUVID = AppendToMesh.AppendUV((Vector2f)UV);
				UVIndices[j] = NewUVID;
			}
			for (int j = 0; j < NT; ++j) {
				Index3i PolyTri = PolyTriangles[j];
				Index3i UVTri(UVIndices[PolyTri.A], UVIndices[PolyTri.B], UVIndices[PolyTri.C]);
				AppendToMesh.SetTriangleUVs(Triangles[j], UVTri);
			}
		}
	}
}

