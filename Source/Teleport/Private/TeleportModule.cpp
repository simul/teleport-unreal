// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeleportModule.h"
#include "Interfaces/IPluginManager.h"
#include "GeometrySource.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "TeleportServer/ClientManager.h"

DEFINE_LOG_CATEGORY(LogTeleport);

#define LOCTEXT_NAMESPACE "TeleportModule"

// ONE Geometry source covering all of the geometry.
static GeometrySource geometrySource;
void FTeleportModule::StartupModule()
{
	teleport::server::SetOutputLogCallback(&FTeleportModule::OutputLogCallback);
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Teleport"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Teleport"), PluginShaderDir);
}

void FTeleportModule::ShutdownModule()
{
	teleport::server::SetOutputLogCallback(nullptr);
}

void FTeleportModule::OutputLogCallback(int severity,const char *txt)
{
	static FString fstr;
	// Prevent too much accumulation.
	if(fstr.Len()>4096)
		fstr.Empty();
	fstr+=txt;
	int max_len=0;
	for(int i=0;i<fstr.Len();i++)
	{
		if(fstr[i]==L'\n'||i>1000)
		{
			fstr[i]=L' ';
			max_len=i+1;
			break;
		}
	}
	if(max_len==0)
		return;
	FString substr=fstr.Left(max_len);
	fstr=fstr.RightChop(max_len);
	if(severity<=1)
	{
		UE_LOG(LogTeleport,Error,TEXT("%s"),*substr);
	}
	else if(severity==2)
	{
		UE_LOG(LogTeleport,Warning,TEXT("%s"),*substr);
	}
	else
	{
		UE_LOG(LogTeleport,Display,TEXT("%s"),*substr);
	}
}

FString FTeleportModule::GetPluginDir() const
{
	static const FString PluginDir = IPluginManager::Get().FindPlugin("Teleport")->GetBaseDir();
	return PluginDir;
}

GeometrySource *FTeleportModule::GetGeometrySource()
{
	return &geometrySource;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTeleportModule, TeleportEditorMode)