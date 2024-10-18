// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class Teleport : ModuleRules
{
	public Teleport(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;
		PCHUsage = PCHUsageMode.NoPCHs;
		MinSourceFilesForUnityBuildOverride=100000; 
		PublicDefinitions.Add("NOMINMAX");
		PublicDefinitions.Add("TELEPORT_INTERNAL_CHECKS=1");
		PublicDefinitions.Add("TELEPORT_STDCALL=__stdcall");
		PublicDefinitions.Add("TELEPORT_EXPORT_SERVER_DLL=1");
		// TELEPORT_INTERNAL_INTEROP must be defined as zero outside of the Teleport dll.
		PublicDefinitions.Add("TELEPORT_INTERNAL_INTEROP=0");

		PrivateIncludePaths.AddRange(
			new string[] {
				"Teleport/Private",
			}
			);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "InputCore",
                "Sockets",
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
                "RHI",
				"RawMesh",
				"Slate",
				"SlateCore",
                "RenderCore",
				"DeveloperSettings",
				"Projects",
                "Networking",
                "MeshDescription" //For reading ID string of imported mesh, so a change can be detected.
			}
			);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../../Include/"));
		Link_TeleportServer(Target);
	}

	private string GetPlatformName(ReadOnlyTargetRules Target)
	{
		if (Target.Platform==UnrealTargetPlatform.Win64)
		{
			return "Win64";
		}
		return "Unsupported";
	}
	private string GetConfigName(ReadOnlyTargetRules Target)
	{
		string LibDirName;
		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug);
		if (bDebug)
		{
			LibDirName = "Release";
		}
		else
		{
			LibDirName = "Release";
		}
		return LibDirName;
	}
    private void Link_libavstream(ReadOnlyTargetRules Target)
    {
	}

	public void Link_TeleportServer(ReadOnlyTargetRules Target)
    {
		PublicAdditionalLibraries.Add(Path.Combine(BinariesDirectory, "Win64/",  "TeleportServer.lib"));

	}
	//ModuleDirectory [GAME]/Plugins/Teleport/Source/Teleport
	private string LibrariesDirectory
    {
        get
        {
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Libraries/"));
        }
	}
	private string BinariesDirectory
	{
		get
		{
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Binaries/"));
		}
	}

	private string TeleportRootDirectory
	{
		get
		{
			string sdk_dir=System.Environment.GetEnvironmentVariable("TELEPORT_SDK_DIR");
			if(sdk_dir!= null&&sdk_dir.Length>0)
			{
				return sdk_dir;
			}
			Logger.LogInformation("TELEPORT_SDK_DIR not found.");
			return Path.GetFullPath(Path.Combine(LibrariesDirectory, "include"));
		}
	}
}

