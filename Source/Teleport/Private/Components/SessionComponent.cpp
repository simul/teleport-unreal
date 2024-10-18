// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/SessionComponent.h"

#include "Engine/Classes/Components/SphereComponent.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "GeometrySource.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "TeleportServer/PluginClient.h"
//#include "libavstream/common.hpp"
//
//#include "TeleportServer/ClientMessaging.h"
//#include "TeleportCore/CommonNetworking.h"

#include "Windows/HideWindowsPlatformAtomics.h"
 
#include "Components/TeleportCaptureComponent.h"
#include "Components/StreamableNode.h"
#include "Components/StreamableRootComponent.h"
#include "Components/TeleportClientComponent.h"
#include "TeleportModule.h"
#include "TeleportMonitor.h"
#define TELEPORT_EXPORT_SERVER_DLL 1
//#include "TeleportServer/Export.h"

//#include "InteropStructures.h"
/*
#define XSTR(x) STR(x)
#define STR(x) #x

#pragma message "The value of TELEPORT_SERVER_API: " XSTR(TELEPORT_SERVER_API)*/

#if 1
using namespace teleport::server;
DECLARE_STATS_GROUP(TEXT("Teleport_Game"), STATGROUP_Teleport, STATCAT_Advanced);

template< typename TStatGroup>
static TStatId CreateStatId(const FName StatNameOrDescription, EStatDataType::Type dataType)
{ 
#if	STATS
	FString Description;
	StatNameOrDescription.ToString(Description);
	FStartupMessages::Get().AddMetadata(StatNameOrDescription, *Description,
		TStatGroup::GetGroupName(),
		TStatGroup::GetGroupCategory(), 
		TStatGroup::GetDescription(),
		false,dataType, false, false);
	TStatId StatID = IStatGroupEnableManager::Get().GetHighPerformanceEnableForStat(StatNameOrDescription,
		TStatGroup::GetGroupName(),
		TStatGroup::GetGroupCategory(),
		TStatGroup::DefaultEnable,
		false, dataType, *Description, false, false);

	return StatID;
#endif // STATS

	return TStatId();
}
template< typename TStatGroup >
static TStatId CreateStatId(const FString& StatNameOrDescription, EStatDataType::Type dataType)
{
#if	STATS
	return CreateStatId<TStatGroup>(FName(*StatNameOrDescription),dataType);
#endif // STATS

	return TStatId();
}

/**
 * This is a utility class for counting the number of cycles during the
 * lifetime of the object. It creates messages for the stats thread.
 */
class FScopeBandwidth
{
	/** Name of the stat, usually a short name **/
	FName StatId;

public:

	/**
	 * Pushes the specified stat onto the hierarchy for this thread. Starts
	 * the timing of the cycles used
	 */
	 FScopeBandwidth(TStatId InStatId, float bandwidth)
	{
		FMinimalName StatMinimalName = InStatId.GetMinimalName(EMemoryOrder::Relaxed);
		if (StatMinimalName.IsNone())
		{
			return;
		}
		if ( FThreadStats::IsCollectingData())
		{
			FName StatName = MinimalNameToName(StatMinimalName);
			StatId = StatName;
			FThreadStats::AddMessage(StatName, EStatOperation::Set, double(bandwidth));
		}
	}
	 ~FScopeBandwidth()
	{
		if (!StatId.IsNone())
		{
			//FThreadStats::AddMessage(StatId, EStatOperation::CycleScopeEnd);
		}
	}
};

const avs::uid DUD_CLIENT_ID = 0;

UTeleportSessionComponent::UTeleportSessionComponent()
	: bAutoStartSession(true)
	, AutoListenPort(10500)
	, AutoDiscoveryPort(10600)
	, DisconnectTimeout(1000)
	, InputTouchSensitivity(1.0f)
	, InputTouchAxis(0.f, 0.f)
	, InputJoystick(0.f,0.f)
	, ClientID(0)
	, BandwidthStatID(0)
	, Bandwidth(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
}
UTeleportSessionComponent::~UTeleportSessionComponent()
{
	EndSession();
}

void UTeleportSessionComponent::BeginPlay()
{
	Super::BeginPlay();
	TeleportClientComponent=nullptr;
	Monitor=ATeleportMonitor::Instantiate(GetWorld());
	Bandwidth = 0.0f;
	//INC_DWORD_STAT(STAT_BANDWIDTH); //Increments the counter by one each call.
#if STATS
	FString BandwidthName = GetName() + " Bandwidth kps";
	BandwidthStatID = CreateStatId<FStatGroup_STATGROUP_Teleport>(BandwidthName, EStatDataType::ST_double);
#endif // ENABLE_STATNAMEDEVENTS
  
	ClientActor = Cast<AActor>(GetOuter());
	if(!ClientActor.IsValid())
	{
		UE_LOG(LogTeleport, Error, TEXT("Session: Session component must be attached to an Actor!"));
		return;
	}
/*
	if(bAutoStartSession)
	{
		StartSession(AutoListenPort, AutoDiscoveryPort);
	}*/

}

void UTeleportSessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSession();

	Super::EndPlay(Reason);
}

void UTeleportSessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if(!ClientID)
		return;
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> clientData=cm.GetClient(ClientID);
	//if(!clientData)
	//	return;
	//if (!clientData->clientMessaging->isInitialised() || !ClientActor.IsValid())
	//	return;
	
	if(!DetectionSphereInner.IsValid())
	{
		AddDetectionSpheres();
	}

	DetectionSphereInner->SetSphereRadius(Monitor->DetectionSphereRadius);
	DetectionSphereOuter->SetSphereRadius(Monitor->DetectionSphereRadius + Monitor->DetectionSphereBufferDistance);
	

	if(BandwidthStatID.IsValidStat())
	{
		Bandwidth *= 0.9f;
		FScopeBandwidth Context(BandwidthStatID, Bandwidth);
	}
	if(rootNodeUid!=0)
	{
		Client_SetOrigin(ClientID,rootNodeUid);
	}

	ApplyPlayerInput(DeltaTime);
	/*
	if (GEngine)
	{
		if(ClientNetworkContext )
		{
			auto* pipeline = ClientNetworkContext->NetworkPipeline.getAvsPipeline();
			if(pipeline)
			{
				GEngine->AddOnScreenDebugMessage(135, 1.0f, FColor::White, FString::Printf(TEXT("Start Timestamp %d"), pipeline->GetStartTimestamp()));
				GEngine->AddOnScreenDebugMessage(137, 1.0f, FColor::White, FString::Printf(TEXT("Currt Timestamp %d"), pipeline->GetTimestamp()));
			}
		}
	}*/
}
static std::map<avs::uid, UTeleportSessionComponent *> teleportSessionComponents;
UTeleportSessionComponent *UTeleportSessionComponent::GetTeleportSessionComponent(avs::uid clid)
{
	auto s = teleportSessionComponents.find(clid);
	if (s != teleportSessionComponents.end())
	{
		return s->second;
	}
	return nullptr;
}
void UTeleportSessionComponent::EndSession()
{
	if(ClientID)
	{
		auto s = teleportSessionComponents.find(ClientID);
		if (s != teleportSessionComponents.end() && s->second==this)
		{
			teleportSessionComponents.erase(ClientID);
		}
	}
}


void UTeleportSessionComponent::StartSession(avs::uid clientID)
{
	ClientID = clientID;
	teleportSessionComponents[clientID]=this;
	//UTeleportCaptureComponent* CaptureComponent = Cast<UTeleportCaptureComponent>(ClientActor->GetComponentByClass(UTeleportCaptureComponent::StaticClass()));
	
	ClientActor =  Cast<AActor>(GetOuter());
	
	if(!ClientActor.IsValid())
	{
		UE_LOG(LogTeleport, Warning, TEXT("No client actor."));
	}

	if (ClientActor.IsValid())
	{
		TeleportClientComponent=ClientActor->FindComponentByClass<UTeleportClientComponent>();
		// Attach detection spheres to player pawn, but only if we're actually streaming geometry.
		AddDetectionSpheres();
		StreamNearbyNodes();
	} 
	
	// The session component must ensure that the client/player's geometry is streamed to the client.
	UStreamableRootComponent *root = ClientActor->FindComponentByClass<UStreamableRootComponent>();
	GeometrySource *geometrySource = ITeleport::Get().GetGeometrySource();
	rootNodeUid = StreamToClient(root);
	if(rootNodeUid==0)
	{
		UE_LOG(LogTeleport, Warning, TEXT("No root node for client actor \"%s\"."), *ClientActor->GetName());
	}
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> clientData = cm.GetClient(ClientID);
	Client_StreamNode(ClientID,rootNodeUid);
	Client_SetOrigin(ClientID,rootNodeUid);
	IsStreaming = true;
}

void UTeleportSessionComponent::StopSession()
{
	StopStreaming();
	//ClientMessaging->stopSession();
}


void UTeleportSessionComponent::SetHeadPose(const teleport::core::Pose *newHeadPose)
{
	if(!ClientActor.IsValid())
		return;
	vec3 position = newHeadPose->position;
	vec4 orientation = newHeadPose->orientation;
	// Convert to centimetres.
	FVector NewCameraPos = FVector(position.x, position.y, position.z) * 100.0f;
	FQuat HeadPoseUE(orientation.x, orientation.y, orientation.z, orientation.w);

	if(TeleportClientComponent->HeadComponent)
	{
		TeleportClientComponent->HeadComponent->SetRelativeLocation(NewCameraPos,false,nullptr,ETeleportType::ResetPhysics);
		TeleportClientComponent->HeadComponent->SetRelativeRotation(HeadPoseUE, false, nullptr, ETeleportType::ResetPhysics);
		TeleportClientComponent->HeadComponent->MarkRenderTransformDirty();
	}
#if 0
	UTeleportCaptureComponent *CaptureComponent = Cast<UTeleportCaptureComponent>(PlayerPawn->GetComponentByClass(UTeleportCaptureComponent::StaticClass()));
	if (!CaptureComponent)
		return;


	FVector OldActorPos = PlayerPawn->GetActorLocation();
	// We want the relative location between the player and the camera to stay the same, and the player's Z component to be unchanged.
	FVector ActorToComponent = CaptureComponent->GetComponentLocation() - OldActorPos;
	FVector newActorPos = NewCameraPos - ActorToComponent;
	newActorPos.Z = OldActorPos.Z;
	PlayerPawn->SetActorLocation(newActorPos);

	// Here we set the angle of the player pawn.
	PlayerController->SetControlRotation(HeadPoseUE.Rotator());

	teleport::server::CameraInfo &ClientCamInfo = CaptureComponent->getClientCameraInfo();
	ClientCamInfo.position = {(float)NewCameraPos.X, (float)NewCameraPos.Y, (float)NewCameraPos.Z};
	ClientCamInfo.orientation = orientation;

	CaptureComponent->SetWorldLocation(NewCameraPos);

	if (Monitor->GetServerSettings()->enableDebugControlPackets)
	{
		static char c = 0;
		c--;
		if (!c)
		{
			UE_LOG(LogTeleport, Warning, TEXT("Received Head Pos: %3.2f %3.2f %3.2f"), position.x, position.y, position.z);
		}
	}
	#endif
}


void UTeleportSessionComponent::SetControllerPose(avs::uid id, const teleport::core::PoseDynamic *newPose)
{
}

void UTeleportSessionComponent::ProcessInputState(const teleport::core::InputState *, const uint8_t **, const float **)
{
}

void UTeleportSessionComponent::ProcessInputEvents( uint16_t numBinaryEvents, uint16_t numAnalogueEvents, uint16_t numMotionEvents
	, const teleport::core::InputEventBinary **binaryEventsPtr
	, const teleport::core::InputEventAnalogue **analogueEventsPtr
	, const teleport::core::InputEventMotion **motionEventsPtr)
{
}


void UTeleportSessionComponent::StreamNearbyNodes()
{
	if (teleport::server::GetServerSettings().enableGeometryStreaming)
	{
		// Fill the list of streamed actors, so a reconnecting client will not have to download geometry it already has.
		TSet<AActor *> actorsOverlappingOnStart;
		DetectionSphereInner->GetOverlappingActors(actorsOverlappingOnStart);
		for (AActor *a : actorsOverlappingOnStart)
		{
			UStreamableRootComponent *streamableRootComponent = a->FindComponentByClass<UStreamableRootComponent>();
			if (streamableRootComponent)
			{
				StreamToClient(streamableRootComponent);
			}
		}
	}
}

void UTeleportSessionComponent::StopStreaming()
{
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> clientData = cm.GetClient(ClientID);
	//Stop geometry stream.
	//GeometryStreamingService->stopStreaming();

	//Stop video stream.
	if(ClientActor.IsValid() )
	{
		UTeleportCaptureComponent* CaptureComponent = Cast<UTeleportCaptureComponent>(ClientActor->GetComponentByClass(UTeleportCaptureComponent::StaticClass()));
		if(CaptureComponent)
		{
			CaptureComponent->stopStreaming();
		}

		ClientActor.Reset();
	}
	Client_StopSession(ClientID);
	IsStreaming = false;
}


bool UTeleportSessionComponent::clientStoppedRenderingNode(avs::uid clientID, avs::uid nodeID)
{
	return true;
}

bool UTeleportSessionComponent::clientStartedRenderingNode(avs::uid clientID, avs::uid nodeID)
{
	return true;
}
void UTeleportSessionComponent::SetPlayerId(int p) 
{
	playerId=p;
}

int UTeleportSessionComponent::GetPlayerId() const
{
	return playerId;
}

void UTeleportSessionComponent::OnInnerSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if(IsStreaming)
	{
		TArray<UStreamableRootComponent*> OutComponents;
		OtherActor->GetComponents(OutComponents,true); 
		for (auto r : OutComponents)
		{
			StreamToClient(r);
		}

		UE_LOG(LogTeleport, Verbose, TEXT("\"%s\" overlapped with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
	}
}

avs::uid UTeleportSessionComponent::StreamToClient(UStreamableRootComponent *streamableRootComponent)
{
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> clientData = cm.GetClient(ClientID);
	//if(!clientData)
	//	return 0;
	const TMap<USceneComponent*, TWeakObjectPtr<UStreamableNode>> &streamableNodes = streamableRootComponent->GetStreamableNodes();
	for(auto n:streamableNodes)
	{
		avs::uid nodeID = n.Value->GetUid().Value;
		Client_StreamNode(ClientID,nodeID);
	}
	if(streamableNodes.Num())
		return streamableNodes.begin()->Value->GetUid().Value;
	return 0;
	//UE_LOG(LogTeleport, Verbose, TEXT("\"%s\" ended overlap with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
}

void UTeleportSessionComponent::UnstreamFromClient(UStreamableRootComponent *streamableRootComponent)
{
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> clientData = cm.GetClient(ClientID);
	const TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> &streamableNodes = streamableRootComponent->GetStreamableNodes();
	for (auto n : streamableNodes)
	{
		avs::uid nodeID = n.Value->GetUid().Value;
		//clientData->unstreamNode(nodeID);
		Client_UnstreamNode(ClientID,nodeID);
	}
}

void UTeleportSessionComponent::OnOuterSphereEndOverlap(UPrimitiveComponent * OverlappedComponent, AActor * OtherActor, UPrimitiveComponent * OtherComp, int32 OtherBodyIndex)
{
	if(OtherActor==ClientActor)
		return;
	if(IsStreaming)
	{
		GeometrySource *geometrySource = ITeleport::Get().GetGeometrySource();
		UStreamableRootComponent *streamableRootComponent=OtherActor->FindComponentByClass<UStreamableRootComponent>();
		if (streamableRootComponent)
		{ 
			UnstreamFromClient(streamableRootComponent);
		}
	}
}

void UTeleportSessionComponent::AddDetectionSpheres()
{
	FAttachmentTransformRules transformRules = FAttachmentTransformRules(EAttachmentRule::KeepRelative, true);

	//Attach streamable geometry detection spheres to player pawn.
	{
		DetectionSphereInner = NewObject<USphereComponent>(ClientActor.Get(), "InnerSphere");
		DetectionSphereInner->OnComponentBeginOverlap.AddDynamic(this, &UTeleportSessionComponent::OnInnerSphereBeginOverlap);
		DetectionSphereInner->SetCollisionProfileName("TeleportSensor");
		DetectionSphereInner->SetGenerateOverlapEvents(true);
		DetectionSphereInner->SetSphereRadius(Monitor->DetectionSphereRadius);

		DetectionSphereInner->RegisterComponent();
		DetectionSphereInner->AttachToComponent(ClientActor->GetRootComponent(), transformRules);
	}

	{
		DetectionSphereOuter = NewObject<USphereComponent>(ClientActor.Get(), "OuterSphere");
		DetectionSphereOuter->OnComponentEndOverlap.AddDynamic(this, &UTeleportSessionComponent::OnOuterSphereEndOverlap);
		DetectionSphereOuter->SetCollisionProfileName("TeleportSensor");
		DetectionSphereOuter->SetGenerateOverlapEvents(true);
		DetectionSphereOuter->SetSphereRadius(Monitor->DetectionSphereRadius + Monitor->DetectionSphereBufferDistance);

		DetectionSphereOuter->RegisterComponent();
		DetectionSphereOuter->AttachToComponent(ClientActor->GetRootComponent(), transformRules);
	}
}

void UTeleportSessionComponent::ApplyPlayerInput(float DeltaTime)
{
/*	check(PlayerController.IsValid());
	static bool move_from_inputs = false;
	if (move_from_inputs)
	{
		PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_X, InputTouchAxis.X + InputJoystick.X, DeltaTime, 1, true);
		PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_Y, InputTouchAxis.Y + InputJoystick.Y, DeltaTime, 1, true);
	}
	while (InputQueue.ButtonsPressed.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsPressed.Pop(), EInputEvent::IE_Pressed, 1.0f, true);
	} 
	while (InputQueue.ButtonsReleased.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsReleased.Pop(), EInputEvent::IE_Released, 1.0f, true);
	}  */
}
 
void UTeleportSessionComponent::TranslateButtons(uint32_t ButtonMask, TArray<FKey>& OutKeys)
{
	// TODO: Add support for other buttons as well.

	enum ETeleportButtons
	{
		BUTTON_A = 0x00000001,
		BUTTON_ENTER = 0x00100000,
		BUTTON_BACK = 0x00200000,
	};

	if (ButtonMask & BUTTON_A)
	{
//		OutKeys.Add(EKeys::MotionController_Right_Trigger);
	}
	if (ButtonMask & BUTTON_ENTER)
	{
		// Not sure about this.
		OutKeys.Add(EKeys::Virtual_Accept);
	}
	if (ButtonMask & BUTTON_BACK)
	{
		OutKeys.Add(EKeys::Virtual_Back);
	}
}

// Send to this client all the movement updates it needs.
void UTeleportSessionComponent::SendMovementUpdates()
{
	//auto &cm = ClientManager::instance();
	//std::shared_ptr<ClientData> client = cm.GetClient(ClientID);
	//client->SendMovementUpdates();
	//std::vector<teleport::core::MovementUpdate> updateList;
	//(numUpdates);
	/*auto axesStandard = client->clientMessaging->getClientNetworkContext()->axesStandard;
	if(axesStandard==avs::AxesStandard::NotInitialized)
		return;
	for(int i = 0; i < numUpdates; i++)
	{
		updateList[i] = updates[i];

		avs::ConvertPosition(avs::AxesStandard::UnityStyle, axesStandard, updateList[i].position);
		avs::ConvertRotation(avs::AxesStandard::UnityStyle, axesStandard, updateList[i].rotation);
		avs::ConvertScale	(avs::AxesStandard::UnityStyle, axesStandard, updateList[i].scale);
		avs::ConvertPosition(avs::AxesStandard::UnityStyle, axesStandard, updateList[i].velocity);
		avs::ConvertPosition(avs::AxesStandard::UnityStyle, axesStandard, updateList[i].angularVelocityAxis);
	}

	client->clientMessaging->updateNodeMovement(updateList);*/
}
#endif