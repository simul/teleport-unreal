// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/**
 * This class contains info about the full set of commands used in this editor mode.
 */
class FTeleportEditorModeCommands : public TCommands<FTeleportEditorModeCommands>
{
public:
	FTeleportEditorModeCommands();

	virtual void RegisterCommands() override;
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands();
	 
	TSharedPtr<FUICommandInfo> SimpleTool;
	TSharedPtr<FUICommandInfo> InteractiveTool;
	TSharedPtr<FUICommandInfo> ExtractResources;

protected:
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};
