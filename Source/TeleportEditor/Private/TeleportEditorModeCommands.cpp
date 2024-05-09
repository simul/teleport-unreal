// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeleportEditorModeCommands.h"
#include "TeleportEditorMode.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "TeleportEditorModeCommands"

FTeleportEditorModeCommands::FTeleportEditorModeCommands()
	: TCommands<FTeleportEditorModeCommands>("TeleportEditorMode",
		NSLOCTEXT("TeleportEditorMode", "TeleportEditorModeCommands", "Teleport Editor Mode"),
		NAME_None, FAppStyle::GetAppStyleSetName())
{
}

void FTeleportEditorModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);

	UI_COMMAND(SimpleTool, "Show Actor Info", "Opens message box with info about a clicked actor", EUserInterfaceActionType::Button, FInputChord());
	ToolCommands.Add(SimpleTool);

	UI_COMMAND(InteractiveTool, "Measure Distance", "Measures distance between 2 points (click to set origin, shift-click to set end point)", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(InteractiveTool);
	UI_COMMAND(ExtractResources, "Extract Resources", "Extract resources from selection.", EUserInterfaceActionType::Button, FInputChord());
	ToolCommands.Add(ExtractResources);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FTeleportEditorModeCommands::GetCommands()
{
	return FTeleportEditorModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
