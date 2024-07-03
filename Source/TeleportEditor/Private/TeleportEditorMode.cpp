// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeleportEditorMode.h"
#include "TeleportEditorModeToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "TeleportEditorModeCommands.h"
#include "Modules/ModuleManager.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
// AddYourTool Step 1 - include the header file for your Tools here
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
#include "Tools/TeleportSimpleTool.h"
#include "Tools/TeleportInteractiveTool.h"
#include "Tools/TeleportExtractTool.h"
#include "Visualizers/TeleportStreamableRootComponentVisualizer.h"

// step 2: register a ToolBuilder in FTeleportEditorMode::Enter() below

 #if 0

#define LOCTEXT_NAMESPACE "TeleportEditorMode"

const FEditorModeID UTeleportEditorMode::EM_TeleportEditorModeId = TEXT("EM_TeleportEditorMode");

FString UTeleportEditorMode::SimpleToolName = TEXT("Teleport_ActorInfoTool");
FString UTeleportEditorMode::InteractiveToolName = TEXT("Teleport_MeasureDistanceTool");
FName UTeleportEditorMode::TeleportEditorMode_Resources = FName(TEXT("Resources"));
FName UTeleportEditorMode::TeleportEditorMode_Debug = FName(TEXT("Debug"));


UTeleportEditorMode::UTeleportEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UTeleportEditorMode::EM_TeleportEditorModeId,
		LOCTEXT("ModeName", "Teleport"),
		FSlateIcon(),
		true);
}


UTeleportEditorMode::~UTeleportEditorMode()
{
}


void UTeleportEditorMode::ActorSelectionChangeNotify()
{
}

void UTeleportEditorMode::Enter()
{
	UEdMode::Enter();

	if (GUnrealEd)
	{
		TSharedPtr<FTeleportStreamableRootComponentVisualizer> Visualizer = MakeShared<FTeleportStreamableRootComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(UStreamableRootComponent::StaticClass()->GetFName(), Visualizer);
		Visualizer->OnRegister();
	}
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// AddYourTool Step 2 - register the ToolBuilders for your Tools here.
	// The string name you pass to the ToolManager is used to select/activate your ToolBuilder later.
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 
	const FTeleportEditorModeCommands& SampleToolCommands = FTeleportEditorModeCommands::Get();

	//RegisterTool(SampleToolCommands.SimpleTool, SimpleToolName, NewObject<UTeleportSimpleToolBuilder>(this));
	RegisterTool(SampleToolCommands.InteractiveTool, InteractiveToolName, NewObject<UTeleportInteractiveToolBuilder>(this));
	//RegisterTool(SampleToolCommands.ExtractResources,TEXT("Teleport_Extract"),NewObject<UTeleportExtractToolBuilder>(this));

	// active tool type is not relevant here, we just set to default
	GetToolManager()->SelectActiveToolType(EToolSide::Left, SimpleToolName);

}

void UTeleportEditorMode::Exit()
{
	UEdMode::Exit();
	if (GUnrealEd)
	{
		GUnrealEd->UnregisterComponentVisualizer(UStreamableRootComponent::StaticClass()->GetFName());
	}
}

void UTeleportEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FTeleportEditorModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UTeleportEditorMode::GetModeCommands() const
{
	return FTeleportEditorModeCommands::Get().GetCommands();
}

#undef LOCTEXT_NAMESPACE
#endif