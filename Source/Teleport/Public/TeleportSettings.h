// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "TeleportSettings.generated.h"


UCLASS(config = Teleport, defaultconfig, meta = (DisplayName = "Teleport"))
class TELEPORT_API UTeleportSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(config, EditAnywhere, Category = Teleport)
	FDirectoryPath CachePath;

	UPROPERTY(config, EditAnywhere, Category = Teleport)
	FString ClientIP;

	UPROPERTY(config, EditAnywhere, Category = Teleport)
	int32 VideoEncodeFrequency;

	UPROPERTY(config, EditAnywhere, Category = Teleport)
	uint32 StreamGeometry : 1;

	UPROPERTY(config, EditAnywhere, Category = Teleport)
	FString SignalingPorts;

	void Apply();

	// Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	// END UDeveloperSettings Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTeleportSettingsChanged, const FString&, const UTeleportSettings*);

	/** Gets a multicast delegate which is called whenever one of the parameters in this settings object changes. */
	static FOnTeleportSettingsChanged& OnSettingsChanged();

protected:
	static FOnTeleportSettingsChanged SettingsChangedDelegate;
#endif
};