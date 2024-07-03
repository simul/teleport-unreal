// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * 
 */
class TELEPORTEDITOR_API STeleportResourcePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STeleportResourcePanel)
	{}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);
	FReply ExtractSelected();
	FReply MakeSelectedStreamable();

	bool IsAnythingSelected() const;
};
