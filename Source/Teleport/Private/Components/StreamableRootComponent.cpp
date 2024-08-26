// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StreamableRootComponent.h"
#include "Components/StreamableNode.h"
#include "GameFramework/Actor.h"
#include "GeometrySource.h"
#include "Teleport.h"
#include "TeleportModule.h"
#include "Engine.h"

// Sets default values for this component's properties
UStreamableRootComponent::UStreamableRootComponent()
{
	PrimaryComponentTick.bStartWithTickEnabled = false; 
	PrimaryComponentTick.bCanEverTick = true;
	Priority=0;
}

// Called when the game starts or when the StreamableNode object is needed in editing.
bool UStreamableRootComponent::AddSceneComponentStreamableNode(USceneComponent *sc)
{
	if (!sc)
		return false;
	TWeakObjectPtr<UStreamableNode> streamableNode=streamableNodes.FindOrAdd(sc);
	if(!streamableNode.Get())
	{
		TObjectPtr<UStreamableNode> n=NewObject<UStreamableNode>();
		n->SetSceneComponent(sc);
		n->Priority=Priority;
		Nodes.Add(n);
		streamableNode=n;
		streamableNodes[sc]=Nodes[Nodes.Num()-1];
	}
	GeometrySource *geometrySource=ITeleport::Get().GetGeometrySource();
	geometrySource->AddNode(streamableNode.Get(),true);
	bool result=(streamableNode->GetUid().Value!=0);
	return result;
}

void UStreamableRootComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor *actor = GetOwner();

	// Detect any motion:
	GEngine->OnActorMoved().AddUObject(this, &UStreamableRootComponent::OnMoved);
}


void UStreamableRootComponent::OnMoved(AActor* Actor)
{
    UE_LOG(LogTemp, Warning, TEXT("Level actor moved"));
}

void UStreamableRootComponent::InitializeStreamableNodes() 
{
	if(nodesInitialized)
		return;
	streamableNodes.Empty();
	Nodes.Empty();
	AActor *actor=GetOwner();
	if(!actor)
	{
		UE_LOG(LogTeleport, Error, TEXT("UStreamableRootComponent::InitializeStreamableNodes: Null actor owner!"));
		actor=Cast<AActor>(GetOuter());
		if(!actor)
			return;
	}
	USceneComponent *root=actor->GetRootComponent();
	if(!root)
	{
		UE_LOG(LogTeleport, Error, TEXT("UStreamableRootComponent::InitializeStreamableNodes: Null root USceneComponent!"));
		return;
	}
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	bool stationary=root->Mobility!=EComponentMobility::Type::Movable;
	PrimaryComponentTick.bCanEverTick = !stationary;
	SetComponentTickEnabled(!stationary);
	bool result=AddSceneComponentStreamableNode(root);
	TArray<USceneComponent*> children;
	root->GetChildrenComponents(true,children);
	for(int i=0;i<children.Num();i++)
	{
		result&=AddSceneComponentStreamableNode(children[i]);
	}
	nodesInitialized=result;
}

const TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> &UStreamableRootComponent::GetStreamableNodes() const
{
	if(!nodesInitialized)
		const_cast<UStreamableRootComponent*>(this)->InitializeStreamableNodes();
	return streamableNodes;
}

void UStreamableRootComponent::OnRegister() 
{
	// Call InitializeStreamableNodes later. We may not have all the components on the Actor yet.
	nodesInitialized=false;
	Super::OnRegister();
}

// Called every frame
void UStreamableRootComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	GeometrySource *geometrySource=ITeleport::Get().GetGeometrySource();
	// Call GeometrySource::UpdateNode() on all our streamable nodes.
	for(auto n:streamableNodes)
	{
		geometrySource->UpdateNode(n.Value.Get());
	}
	#if WITH_EDITOR
	#endif
}

