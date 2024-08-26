// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class Teleport : ModuleRules
{
	bool DYNAMIC_TELEPORT_SERVER = true;
	public Teleport(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;
		PCHUsage = PCHUsageMode.NoPCHs;
		MinSourceFilesForUnityBuildOverride=100000;
		PublicDefinitions.Add("TELEPORT_INTERNAL_CHECKS=1");
		PublicDefinitions.Add("TELEPORT_STDCALL=__stdcall");
		PublicDefinitions.Add("TELEPORT_EXPORT_SERVER_DLL=1"); 
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

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

		PublicIncludePaths.Add(TeleportRootDirectory);
		PublicIncludePaths.Add(Path.Combine(TeleportRootDirectory,"thirdparty/libdatachannel/deps/json/single_include"));
		PublicIncludePaths.Add(Path.Combine(LibrariesDirectory, "safe/include"));
		PublicIncludePaths.Add(Path.Combine(TeleportRootDirectory, "firstparty/Platform/External/fmt/include" ));
		PublicIncludePaths.Add(Path.Combine(TeleportRootDirectory, "firstparty"));
		Link_libavstream(Target);
        //Link_basisu(Target);
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
        PublicIncludePaths.Add(TeleportRootDirectory + "/libavstream/Include");
		string LibraryPath = Path.Combine(LibrariesDirectory, "lib/", GetConfigName(Target));
		PublicIncludePaths.Add(LibraryPath);

		string ReleaseLibraryPath = Path.Combine(LibrariesDirectory, GetConfigName(Target));
		PublicIncludePaths.Add(ReleaseLibraryPath);
		// EFP

        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));

		// Temporary path CUDA_PATH_V11_6
		if (!DYNAMIC_TELEPORT_SERVER)
		{
			string CudaLibraryPath = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.6/lib/x64";
			PublicAdditionalLibraries.Add(LibraryPath + "/libavstream.lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "efp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(CudaLibraryPath, "cudart.lib"));

			if (Target.Platform== UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add("dxgi.lib");
				PublicAdditionalLibraries.Add("d3d12.lib");
				string SystemPath = "C:/Windows/System32";
				RuntimeDependencies.Add(Path.Combine(SystemPath, "dxgi.dll"));
				RuntimeDependencies.Add(Path.Combine(SystemPath, "D3D12.dll"));
				string TracyLibraryPath = Path.Combine(LibrariesDirectory, "_deps/tracy-build/Release");
				//PublicAdditionalLibraries.Add(Path.Combine(TracyLibraryPath, "TracyClient.lib"));
			}
		}
	}

    private void Link_basisu(ReadOnlyTargetRules Target)
    {
        PrivateIncludePaths.Add(Path.Combine(TeleportRootDirectory, "thirdparty/basis_universal"));

        PublicIncludePaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal", GetConfigName(Target)));
		//PublicIncludePaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal", GetConfigName(Target)));

		PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal/", GetConfigName(Target),"basisu.lib"));

        //PublicDelayLoadDLLs.Add("basisu_MD.dll");
        //RuntimeDependencies.Add(Path.Combine(LibraryPath, "basisu_MD.dll"));
    }

	public void Link_TeleportServer(ReadOnlyTargetRules Target)
    {
		PrivateIncludePaths.Add(Path.Combine(TeleportRootDirectory, "TeleportServer"));

		PublicIncludePaths.Add(Path.Combine(LibrariesDirectory, "TeleportServer", GetConfigName(Target)));
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

