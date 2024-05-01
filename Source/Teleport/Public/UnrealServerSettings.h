// Copyright 2018-2024 Simul.co

#pragma once

#include "CoreMinimal.h"

#include "TeleportServer/ServerSettings.h"


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

	teleport::server::CasterEncoderSettings GetAsCasterEncoderSettings()
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