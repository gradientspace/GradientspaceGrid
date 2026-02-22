// Copyright Gradientspace Corp. All Rights Reserved.
#pragma once

#include "GradientspacePlatform.h"


#ifdef GSGRID_EMBEDDED_UE_BUILD

#include "HAL/Platform.h"

#else   // ! GSGRID_EMBEDDED_UE_BUILD

#if defined(__linux__) || defined(__APPLE__)
	#ifdef GRADIENTSPACEGRID_EXPORTS
	#define GRADIENTSPACEGRID_API __attribute__((visibility("default")))
	#else
	#define GRADIENTSPACEGRID_API
	#endif
#else
	#ifdef GRADIENTSPACEGRID_EXPORTS
	#define GRADIENTSPACEGRID_API __declspec(dllexport)
	#else
	#define GRADIENTSPACEGRID_API __declspec(dllimport)
	#endif
#endif


// disable some warnings (UE disables these warnings too)
#pragma warning(disable: 4251)		// X needs to have dll-interface to be used by clients of class X. 
									// This warning happens when an exported class/struct/etc has template members
									// conceivably it's possible to export the template instantiations...but messy


#endif

