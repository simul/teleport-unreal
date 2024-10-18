// Copyright 2018-2024 Teleport XR Ltd

#pragma once

#include "CoreMinimal.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#include "TeleportServer/InteropStructures.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"


#include "UnrealServerSettings.generated.h"


USTRUCT(BlueprintType)
struct FUnrealCasterEncoderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	int32 FrameWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	int32 FrameHeight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	int32 DepthWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	int32 DepthHeight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	bool bWriteDepthTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	bool bStackDepth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	bool bDecomposeCube;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	float MaxDepth;

	CasterEncoderSettings GetAsCasterEncoderSettings()
	{
		return
		{
			FrameWidth,
			FrameHeight,
			DepthWidth,
			DepthHeight,
			bWriteDepthTexture,
			bStackDepth,
			bDecomposeCube,
			MaxDepth
		};
	}
};