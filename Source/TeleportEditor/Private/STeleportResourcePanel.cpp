// Fill out your copyright notice in the Description page of Project Settings.


#include "STeleportResourcePanel.h"
#include "SlateOptMacros.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "TeleportEditorModule.h"
#include "LevelEditor.h"
#include "Selection.h"
BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STeleportResourcePanel::Construct(const FArguments& InArgs)
{
	
	ChildSlot
	[
		// Populate the widget
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding( 16.0f )
		.HAlign(HAlign_Left)
		[
			SNew( SButton )
			.Text( NSLOCTEXT("", "ExtractSelected", "Extract Selected") )
			.OnClicked( this, &STeleportResourcePanel::ExtractSelected )
			.IsEnabled(this, &STeleportResourcePanel::IsAnythingSelected)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding( 16.0f )
		.HAlign(HAlign_Left)
		[
			SNew( SButton )
			.Text( NSLOCTEXT("", "MakeSelectedStreamable", "Make Selected Streamable") )
			.OnClicked( this, &STeleportResourcePanel::MakeSelectedStreamable )
			.IsEnabled(this, &STeleportResourcePanel::IsAnythingSelected)
		]
	];
	
}
bool STeleportResourcePanel::IsAnythingSelected() const
{
	return GEditor->GetSelectedActors()->Num() != 0;
}

FReply STeleportResourcePanel::ExtractSelected()
{
#if !UE_BUILD_SHIPPING
	FTeleportEditorModule::Get().ExtractResources();
#endif 
	return FReply::Handled();
}


FReply STeleportResourcePanel::MakeSelectedStreamable()
{
#if !UE_BUILD_SHIPPING
	FTeleportEditorModule::Get().MakeSelectedStreamable();
#endif 
	return FReply::Handled();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION
