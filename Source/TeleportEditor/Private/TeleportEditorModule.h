// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "AssetTypeCategories.h"
#include "IAssetTools.h"
#include "Logging/LogMacros.h"
class UTeleportSettings;

/** Teleport Editor module */
class FTeleportEditorModule : public IModuleInterface
{
public:
public:
	FTeleportEditorModule();

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the instance of this module. */
	TELEPORTEDITOR_API static FTeleportEditorModule& Get();

	/** Register/unregister niagara editor settings. */
	void RegisterSettings();
	void UnregisterSettings();

	static EAssetTypeCategories::Type GetAssetCategory() { return TeleportAssetCategory; }


	/** Get the  UI commands. */
	TELEPORTEDITOR_API const class FTeleportEditorCommands &Commands();
	
	void OpenResourceWindow();
	TSharedRef<SDockTab> OnSpawnResourcesTab(const FSpawnTabArgs &SpawnTabArgs);
	void FillMenu(FMenuBuilder &MenuBuilder);

	void ExtractResources();
private:
	TSharedPtr<FExtender> MenuExtender;
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	void OnTeleportSettingsChangedEvent(const FString& PropertyName, const UTeleportSettings* Settings);
	void OnPreGarbageCollection();

private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;


	static EAssetTypeCategories::Type TeleportAssetCategory;

};



DECLARE_LOG_CATEGORY_EXTERN(LogTeleportEditor,Log,All);