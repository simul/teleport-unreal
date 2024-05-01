// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StreamableNode.h"
#include "StreamableRootComponent.generated.h"

namespace avs
{
	typedef uint64_t uid;
}

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class TELEPORT_API UStreamableRootComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UStreamableRootComponent();

	void InitializeStreamableNodes() ;

	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category=Teleport)
	TArray<TObjectPtr<UStreamableNode>> Nodes;

	const TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> &GetStreamableNodes() const;

	void OnRegister() override;
protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> streamableNodes;
	bool nodesInitialized=false;
	bool AddSceneComponentStreamableNode(USceneComponent *sc);

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

};
