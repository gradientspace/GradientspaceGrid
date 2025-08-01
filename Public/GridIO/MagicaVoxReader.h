// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspaceGridPlatform.h"
#include "ModelGrid/ModelGrid.h"
#include "Math/GSFrame3.h"


namespace GS
{

class GRADIENTSPACEGRID_API MagicaVoxReader
{
public:

	struct VOXReadOptions
	{
		bool bIgnoreColors = false;
		bool bCombineAllObjects = true;
		bool bIgnoreTransforms = false;
	};

	struct VOXTransform
	{
		uint8_t PackedRotation;
		Vector3i Translation;
	};

	struct VOXGridObject
	{
		ModelGrid Grid;
		// optional transform on object
		VOXTransform Transform;
	};

	static bool Read( 
		const std::string& Path,
		FunctionRef<VOXGridObject*()> RequestNewGridObjectFunc,
		const VOXReadOptions& Options = VOXReadOptions());

};


}
