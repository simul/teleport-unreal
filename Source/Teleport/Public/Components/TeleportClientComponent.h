// Copyright 2018-2024 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "TeleportClientComponent.generated.h"

class AClient;
class APlayerController;
class USphereComponent;
class UStreamableRootComponent;

namespace avs
{
	typedef uint64_t uid;
	struct Pose;
}

/// A UTeleportClientComponent should be present on any PlayerClient used for Teleport connections. 
UCLASS(meta=(BlueprintSpawnableComponent))
class TELEPORT_API UTeleportClientComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTeleportClientComponent();
	virtual ~UTeleportClientComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	USceneComponent *HeadComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	TMap<USceneComponent *, FString> PoseMappings;

	UFUNCTION(BlueprintCallable, Category = Teleport)
	void SetPoseMapping(USceneComponent *s, FString m);
};
