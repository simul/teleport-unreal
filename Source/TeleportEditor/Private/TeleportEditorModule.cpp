// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TeleportEditorModule.h"
#include "Teleport.h"
#include "TeleportSettings.h"
#include "TeleportEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IAssetTypeActions.h"
#include "Interfaces/IMainFrameModule.h"
#include "Framework/Commands/Commands.h"
#include "AssetToolsModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/ConfigCacheIni.h"
#include "ISettingsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "TeleportEditorMode.h"
#include "TeleportEditorModeCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "Visualizers/TeleportStreamableRootComponentVisualizer.h"
#include "LevelEditor.h"
#include "EditorModeRegistry.h"
#include "STeleportResourcePanel.h"
#include "Selection.h"

IMPLEMENT_MODULE(FTeleportEditorModule, TeleportEditor);

DEFINE_LOG_CATEGORY(LogTeleportEditor);
#define LOCTEXT_NAMESPACE "TeleportEditorModule"

EAssetTypeCategories::Type FTeleportEditorModule::TeleportAssetCategory;


FTeleportEditorModule::FTeleportEditorModule()
	
{
}

class FTeleportPluginCommands : public TCommands<FTeleportPluginCommands>
{
public:
	FTeleportPluginCommands()
		: TCommands<FTeleportPluginCommands>(
			  TEXT("Teleport"),										// Context name for fast lookup
			  NSLOCTEXT("Contexts", "TrueSkyCmd", "Teleport"),		// Localized context name for displaying
			  NAME_None,											// Parent context name.
			  FAppStyle::GetAppStyleSetName())						// Parent
	{
	}
	virtual void RegisterCommands() override
	{
		UI_COMMAND(OpenResourceWindow, "OpenResourceWindow", "Show the Resource Window", EUserInterfaceActionType::Button, FInputChord());
	}

public:
	TSharedPtr<FUICommandInfo> OpenResourceWindow;
};
#include "StreamableNodeDetailCustomizations.h"
static const FName TeleportResourceWindowTabName("trueSky World");
void FTeleportEditorModule::StartupModule()
{
	FTeleportEditorModeCommands::Register();
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TeleportAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Teleport")), LOCTEXT("TeleportAssetsCategory", "Teleport"));

	UTeleportSettings::OnSettingsChanged().AddRaw(this, &FTeleportEditorModule::OnTeleportSettingsChangedEvent);

	if(FModuleManager::Get().IsModuleLoaded("MainFrame") )
	{
		FTeleportPluginCommands::Register();
		IMainFrameModule &MainFrameModule = IMainFrameModule::Get();
		const TSharedRef<FUICommandList> &CommandList = MainFrameModule.GetMainFrameCommandBindings();
		CommandList->MapAction(FTeleportPluginCommands::Get().OpenResourceWindow,
							   FExecuteAction::CreateRaw(this, &FTeleportEditorModule::OpenResourceWindow),
							   FCanExecuteAction());
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TeleportResourceWindowTabName, FOnSpawnTab::CreateRaw(this, &FTeleportEditorModule::OnSpawnResourcesTab)).SetDisplayName(LOCTEXT("Teleport Resources", "Teleport Resources View")).SetMenuType(ETabSpawnerMenuType::Hidden);
		MenuExtender = MakeShareable(new FExtender);
		MenuExtender->AddMenuExtension("DataValidation", EExtensionHook::After, CommandList, FMenuExtensionDelegate::CreateRaw(this, &FTeleportEditorModule::FillMenu));
		if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
		{
			FLevelEditorModule &LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
			LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
		}
	}
	// Customizations for class Details panels:
	FPropertyEditorModule& PropertyModule=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UStreamableNode::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FStreamableNodeDetailCustomization::MakeInstance)
	);
	PropertyModule.RegisterCustomClassLayout(
		UStreamableRootComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FStreamableNodeDetailCustomization::MakeInstance)
	);
	PropertyModule.RegisterCustomClassLayout(
		USceneComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FStreamableNodeDetailCustomization::MakeInstance)
	);
	//PropertyModule.RegisterCustomPropertyTypeLayout(UStreamableRootComponent::StaticClass()->GetFName(),FOnGetDetailLayoutInstance::CreateRaw(&FStreamableNodeDetailCustomization::MakeInstance));

}
#include "GeometrySource.h"
void FTeleportEditorModule::ExtractResources()
{
	GeometrySource *geometrySource = ITeleport::Get().GetGeometrySource();
	USelection *SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor *> Actors;
	TArray<ULevel *> UniqueLevels;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor *Actor = CastChecked<AActor>(*Iter);
		
		UStreamableRootComponent *root=Actor->GetComponentByClass<UStreamableRootComponent>();
		if(!root)
			continue;
		root->InitializeStreamableNodes();
		const auto &nodes=root->GetStreamableNodes();
		for(const auto &n:nodes)
		{
			geometrySource->ExtractResourcesForNode(n.Value.Get(),true);
			geometrySource->AddNode(n.Value.Get(),true);
		}
	/*	TArray<UMeshComponent*> meshc;
		Actor->GetComponents(meshc);
		for(int m=0;m<meshc.Num();m++)
		{
			UMeshComponent *meshComponent = meshc[m];
			avs::uid dataID = geometrySource->AddMesh(meshComponent,true);
			TArray<UMaterialInterface*> materials=meshComponent->GetMaterials();
		}*/
	}
	geometrySource->CompressTextures();
}

void FTeleportEditorModule::FillMenu(FMenuBuilder &MenuBuilder)
{
	MenuBuilder.BeginSection("Teleport Plugin", FText::FromString(TEXT("Teleport")));
	{
		try
		{
			MenuBuilder.AddMenuEntry(FTeleportPluginCommands::Get().OpenResourceWindow);
		}
		catch (...)
		{
		}
	}
	MenuBuilder.EndSection();
}

void FTeleportEditorModule::OpenResourceWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TeleportResourceWindowTabName);
}

TSharedRef<SDockTab> FTeleportEditorModule::OnSpawnResourcesTab(const FSpawnTabArgs &SpawnTabArgs)
{
	TSharedRef<STeleportResourcePanel> widget = SNew(STeleportResourcePanel);

	TSharedRef<SDockTab> tab = SNew(SDockTab)
								   .TabRole(ETabRole::NomadTab)
									   [SNew(SHorizontalBox) + SHorizontalBox::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[widget]
	];
	return tab;
}



void FTeleportEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FTeleportEditorModeCommands::Unregister();
}

FTeleportEditorModule& FTeleportEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FTeleportEditorModule>("TeleportEditor");
}

void FTeleportEditorModule::OnTeleportSettingsChangedEvent(const FString& PropertyName, const UTeleportSettings* Settings)
{
}

#undef LOCTEXT_NAMESPACE
