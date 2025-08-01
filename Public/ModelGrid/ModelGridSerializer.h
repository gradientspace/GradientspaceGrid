// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "Core/gs_serializer.h"

namespace GS
{


class GRADIENTSPACEGRID_API ModelGridSerializer
{
public:
	static bool Serialize(const ModelGrid& Grid, GS::ISerializer& Serializer);
	static bool Restore(ModelGrid& Grid, GS::ISerializer& Serializer);

	static constexpr const char* SerializeVersionString() { return "ModelGrid_Version"; }

protected:

	static bool Serialize_V1(const ModelGrid& Grid, GS::ISerializer& Serializer);
	//static bool Restore_V1(ModelGrid& Grid, GS::ISerializer& Serializer);

	static bool Serialize_V2(const ModelGrid& Grid, GS::ISerializer& Serializer);
	//static bool Restore_V2(ModelGrid& Grid, GS::ISerializer& Serializer);
	static bool Restore_V1V2(ModelGrid& Grid, GS::ISerializer& Serializer, bool bIsV1);

	static bool Serialize_V3(const ModelGrid& Grid, GS::ISerializer& Serializer);
	static bool Restore_V3(ModelGrid& Grid, GS::ISerializer& Serializer);

	static bool Restore_Base_V1_V2(ModelGrid& Grid, GS::ISerializer& Serializer, bool bIsV1);
};

} // end namespace GS
