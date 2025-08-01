// Copyright Gradientspace Corp. All Rights Reserved.
#include "GradientspaceGridModule.h"

#ifdef WITH_ENGINE

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FGradientspaceGridModule"


void FGradientspaceGridModule::StartupModule()
{
#ifndef GSGRID_EMBEDDED_UE_BUILD
	FString BaseDir = IPluginManager::Get().FindPlugin("GradientspaceUEToolbox")->GetBaseDir();
#ifdef GSGRID_USING_DEBUG
	FString DLLPath = FPaths::Combine(*BaseDir, "gradientspace_distrib/Win64/Debug/gradientspace_grid.dll");
#else
	FString DLLPath = FPaths::Combine(*BaseDir, "gradientspace_distrib/Win64/Release/gradientspace_grid.dll");
#endif
	if (FPaths::FileExists(*DLLPath))
		PrecompiledDLLHandle = FPlatformProcess::GetDllHandle(*DLLPath);
	else
		UE_LOG(LogTemp, Error, TEXT("Could not find GradientspaceGrid DLL at %s"), *DLLPath);
#endif
}

void FGradientspaceGridModule::ShutdownModule()
{
	if (PrecompiledDLLHandle != nullptr)
		FPlatformProcess::FreeDllHandle(PrecompiledDLLHandle);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGradientspaceGridModule, GradientspaceGrid)

#endif
