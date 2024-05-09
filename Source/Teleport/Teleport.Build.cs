// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Diagnostics;

public class Teleport : ModuleRules
{
	public Teleport(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;
		PCHUsage = PCHUsageMode.NoPCHs;
		MinSourceFilesForUnityBuildOverride=100000;
		PublicDefinitions.Add("TELEPORT_INTERNAL_CHECKS=1");
		PublicDefinitions.Add("TELEPORT_STDCALL=__stdcall");
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
        Link_basisu(Target);
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
		//PublicIncludePaths.Add("C:/Teleport/plugins/UnrealDemo/Plugins/Teleport/Libraries/lib/Release");
		PublicAdditionalLibraries.Add(LibraryPath+"/libavstream.lib");

		// SRT:
		string ReleaseLibraryPath = Path.Combine(LibrariesDirectory, GetConfigName(Target));
		PublicIncludePaths.Add(ReleaseLibraryPath);
		//string PthreadsLibraryPath = Path.Combine(LibrariesDirectory, "thirdparty\\srt\\submodules\\pthread");
		//PublicIncludePaths.Add(PthreadsLibraryPath);
		//PublicAdditionalLibraries.Add("pthread_lib.lib");
		//PublicAdditionalLibraries.Add("ws2_32.lib");

        //set(PTHREAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/srt/submodules/pthread-win32)

        // EFP
        PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "efp.lib"));

        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));

		// Temporary path CUDA_PATH_V11_6
		string CudaLibraryPath="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.6/lib/x64";

        PublicAdditionalLibraries.Add(Path.Combine(CudaLibraryPath, "cudart.lib"));

		if (Target.Platform== UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add("dxgi.lib");
            PublicAdditionalLibraries.Add("d3d12.lib");
            string SystemPath = "C:/Windows/System32";
            RuntimeDependencies.Add(Path.Combine(SystemPath, "dxgi.dll"));
            RuntimeDependencies.Add(Path.Combine(SystemPath, "D3D12.dll"));
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
		bool DYNAMIC_TELEPORT_SERVER=false;
		if (!DYNAMIC_TELEPORT_SERVER)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "TeleportServer/", GetConfigName(Target), "TeleportServer.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "TeleportCore/", GetConfigName(Target), "TeleportCore.lib"));

			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "TeleportAudio/", GetConfigName(Target), "TeleportAudio.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "lib/", GetConfigName(Target), "libavstream.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "lib/", GetConfigName(Target), "efp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "firstparty/Platform/External/fmt", GetConfigName(Target), "fmt.lib"));
		
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/libdatachannel", GetConfigName(Target), "datachannel-static.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/libdatachannel/deps/libsrtp", GetConfigName(Target), "srtp2.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/libdatachannel/deps/usrsctp/usrsctplib", GetConfigName(Target), "usrsctp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TeleportRootDirectory, "thirdparty/openssl/x64/lib","libcrypto.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TeleportRootDirectory, "thirdparty/openssl/x64/lib","libssl.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/libdatachannel/deps/libjuice", GetConfigName(Target), "juice-static.lib"));

			PublicAdditionalLibraries.Add(Path.Combine(LibrariesDirectory, "thirdparty/draco", GetConfigName(Target), "draco.lib"));
		} else { 
			PublicAdditionalLibraries.Add(Path.Combine(BinariesDirectory, GetPlatformName(Target), "TeleportServer.lib"));
		}

	}

	//ModuleDirectory Teleport/plugins/UnrealDemo/Plugins/Teleport/Source/Teleport
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
				return sdk_dir;
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../../../"));
		}
	}
}

