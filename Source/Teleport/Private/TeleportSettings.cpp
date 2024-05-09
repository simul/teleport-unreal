// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TeleportSettings.h"

UTeleportSettings::UTeleportSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
	,ClientIP("")
	,VideoEncodeFrequency(3)
	,StreamGeometry(true)
{

}

FName UTeleportSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UTeleportSettings::GetSectionText() const
{
	return NSLOCTEXT("TeleportPlugin", "TeleportSettingsSection", "Teleport");
}
#endif

#if WITH_EDITOR
void UTeleportSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.Property->GetName(), this);
	}
}

UTeleportSettings::FOnTeleportSettingsChanged& UTeleportSettings::OnSettingsChanged()
{
	return SettingsChangedDelegate;
}

UTeleportSettings::FOnTeleportSettingsChanged UTeleportSettings::SettingsChangedDelegate;
#endif


void UTeleportSettings::Apply()
{
}

 