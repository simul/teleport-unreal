// Copyright 2018-2024 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "TeleportPawnComponent.generated.h"

class APawn;
class APlayerController;
class USphereComponent;
class UStreamableRootComponent;

namespace avs
{
	typedef uint64_t uid;
	struct Pose;
}

/// A UTeleportPawnComponent should be present on any PlayerPawn used for Teleport connections. 
UCLASS(meta=(BlueprintSpawnableComponent))
class TELEPORT_API UTeleportPawnComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTeleportPawnComponent();
	virtual ~UTeleportPawnComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	USceneComponent *HeadComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	TMap<USceneComponent *, FString> PoseMappings;

	UFUNCTION(BlueprintCallable, Category = Teleport)
	void SetPoseMapping(USceneComponent *s, FString m);
};
