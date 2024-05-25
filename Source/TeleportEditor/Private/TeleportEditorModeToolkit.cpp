// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeleportEditorModeToolkit.h"
#include "TeleportEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"
#include "TeleportEditorModeCommands.h"
#include "TeleportEditorModule.h"

#define LOCTEXT_NAMESPACE "TeleportEditorModeToolkit"

FTeleportEditorModeToolkit::FTeleportEditorModeToolkit()
{
}

void FTeleportEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	const FTeleportEditorModeCommands &Commands = FTeleportEditorModeCommands::Get();

	const TSharedRef<FUICommandList> &UICommandList = GetToolkitCommands();
	UICommandList->MapAction(Commands.ExtractResources,
							 FExecuteAction::CreateRaw(&FTeleportEditorModule::Get(), &FTeleportEditorModule::ExtractResources),
							FCanExecuteAction());
}

void FTeleportEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
	PaletteNames.Add(UTeleportEditorMode::TeleportEditorMode_Resources);
	PaletteNames.Add(UTeleportEditorMode::TeleportEditorMode_Debug);
}

FText FTeleportEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{
	if (Palette == NAME_Default)
	{
		return LOCTEXT("Default", "Default");
	}
	if (Palette == UTeleportEditorMode::TeleportEditorMode_Resources)
	{
		return LOCTEXT("TeleportEditorMode_Resources", "Resources");
	}
	if (Palette == UTeleportEditorMode::TeleportEditorMode_Debug)
	{
		return LOCTEXT("TeleportEditorMode_Debug", "Debug");
	}
	return FText();
}


FName FTeleportEditorModeToolkit::GetToolkitFName() const
{
	return FName("TeleportEditorMode");
}

FText FTeleportEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "TeleportEditorMode Toolkit");
}

#undef LOCTEXT_NAMESPACE
