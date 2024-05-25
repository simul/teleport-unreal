// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class TeleportEditor : ModuleRules
{
	private string TeleportRootDirectory
	{
		get
		{
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../../../"));
		}
	}
	public TeleportEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		MinSourceFilesForUnityBuildOverride = 100000;
		PrivateIncludePaths.Add(TeleportRootDirectory);
		PrivateIncludePaths.AddRange(new string[] {
			"TeleportEditor/Private",
			"Teleport/Private"
		});
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
				"RHI",
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"InputCore",
				"RenderCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"ComponentVisualizers",
				"Teleport",
				"TimeManagement",
				"PropertyEditor",
				"TargetPlatform",
				"AppFramework",
				"Projects",
				"MainFrame",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"UnrealEd",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"Messaging",
				"LevelEditor",
				"AssetTools",
				"ContentBrowser",
				"DerivedDataCache",
				"Teleport"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Teleport",
				"Engine",
				"UnrealEd",
				"LevelEditor",
				"EditorFramework",
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"Teleport"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"WorkspaceMenuStructure", 
				}
			);
		PublicIncludePaths.Add("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.6/lib/x64");
	}
}
