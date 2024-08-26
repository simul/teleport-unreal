#define NOMINMAX
#include "TeleportMonitor.h"

#include "Engine/Classes/Components/MeshComponent.h"
#include "Engine/Classes/Components/SceneCaptureComponentCube.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TeleportModule.h"
#include "Teleport.h"

#include "Components/SessionComponent.h"
#include "Components/StreamableNode.h"
#include "GeometrySource.h"
#include "Teleport.h"
#include "TeleportCore/CommonNetworking.h"
#include "TeleportServer/ClientManager.h"
#ifndef FIX_DEBUG
#include "TeleportSettings.h" 

TMap<UWorld *, ATeleportMonitor *> ATeleportMonitor::Monitors;

ATeleportMonitor::ATeleportMonitor(const class FObjectInitializer &ObjectInitializer)
	: Super(ObjectInitializer), DetectionSphereRadius(1000), DetectionSphereBufferDistance(200), HandActor(nullptr),
	  GeometryTicksPerSecond(2), GeometryBufferCutoffSize(1048576) /*1MB*/, ConfirmationWaitTime(15.0f), EstimatedDecodingFrequency(10)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	RequiredLatencyMs = 30;
	// Defaults from settings class.
	const UTeleportSettings *TeleportSettings = GetDefault<UTeleportSettings>();
	if (TeleportSettings)
	{
		ClientIP = TeleportSettings->ClientIP;
		VideoEncodeFrequency = TeleportSettings->VideoEncodeFrequency;
	}
	else
	{
		VideoEncodeFrequency = 2;
	}
	bStreamVideo = true;
	bOverrideTextureTarget = false;
	SceneCaptureTextureTarget = nullptr;
	bDeferOutput = false;
	bDoCubemapCulling = false;
	BlocksPerCubeFaceAcross = 2;
	TargetFPS = 60;
	CullQuadIndex = -1;
	IDRInterval = 0; // Value of 0 means only first frame will be IDR
	VideoCodec = VideoCodec::HEVC;
	RateControlMode = EncoderRateControlMode::RC_CBR_LOWDELAY_HQ;
	AverageBitrate = 40000000; // 40mb/s
	MaxBitrate = 80000000;	   // 80mb/s
	bAutoBitRate = false;
	vbvBufferSizeInFrames = 3;
	bUseAsyncEncoding = true;
	bUse10BitEncoding = false;
	bUseYUV444Decoding = false;

	DebugStream = 0;
	DebugNetworkPackets = false;
	DebugControlPackets = false;
	Checksums = false;
	ResetCache = false;

	QualityLevel = 1;
	CompressionLevel = 1;

	ExpectedLag = 0;

	bDisableMainCamera = false;
}

ATeleportMonitor::~ATeleportMonitor()
{
	GeometrySource *geometrySource = ITeleport::Get().GetGeometrySource();
	if(geometrySource)
	{
		if(geometrySource->GetMonitor()==this)
			geometrySource->SetMonitor(nullptr);
	}
	auto *world = GetWorld();
	if (Monitors.Contains(world))
		Monitors.FindAndRemoveChecked(world);
}

ATeleportMonitor *ATeleportMonitor::Instantiate(UWorld *world)
{
	auto i = Monitors.Find(world);
	if (i)
	{
		return *i;
	}
	ATeleportMonitor *M = nullptr;
	TArray<AActor *> MActors;
	UGameplayStatics::GetAllActorsOfClass(world, ATeleportMonitor::StaticClass(), MActors);
	if (MActors.Num() > 0)
	{
		M = static_cast<ATeleportMonitor *>(MActors[0]);
	}
	else
	{
		UClass *monitorClass = ATeleportMonitor::StaticClass();
		M = world->SpawnActor<ATeleportMonitor>(monitorClass, FVector(0.0f, 0.f, 0.f), FRotator(0.0f, 0.f, 0.f), FActorSpawnParameters());
		Monitors.Add(TTuple<UWorld *, ATeleportMonitor *>(world, M));
	}
	return M;
}

void ATeleportMonitor::PostInitProperties()
{
	Super::PostInitProperties();
	bNetLoadOnClient = false;
	SetCanBeDamaged(false);
	bRelevantForLevelBounds = false;
}

void ATeleportMonitor::PostLoad()
{
	Super::PostLoad();
}

void ATeleportMonitor::PostRegisterAllComponents()
{
	UpdateServerSettings();
}

void ATeleportMonitor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}
avs::uid GenerateServerUid() 
{
	static avs::uid server_session=1;
	return server_session++;
}
void ATeleportMonitor::BeginPlay()
{
	Super::BeginPlay();

	ServerID = GenerateServerUid();

	// Decompose the geometry in the level, if we are streaming the geometry.
	if (teleport::server::GetServerSettings().enableGeometryStreaming)
	{
		InitialiseGeometrySource();
	}
	const UTeleportSettings *TeleportSettings=GetDefault<UTeleportSettings>();
	teleport::server::InitializationSettings initializationSettings;
	initializationSettings.clientIP = "";
	std::string cachePath=teleport::unreal::ToStdString(TeleportSettings->CachePath.Path);
	initializationSettings.httpMountDirectory =cachePath.c_str();
	initializationSettings.certDirectory = "";
	initializationSettings.privateKeyDirectory = "";
	sigport=teleport::unreal::ToStdString(TeleportSettings->SignalingPorts);
	initializationSettings.signalingPorts =sigport.c_str();

	initializationSettings.clientStoppedRenderingNode = &UTeleportSessionComponent::clientStoppedRenderingNode;
	initializationSettings.clientStartedRenderingNode = &UTeleportSessionComponent::clientStartedRenderingNode;
	initializationSettings.headPoseSetter = &ATeleportMonitor::StaticSetHeadPose;
	initializationSettings.controllerPoseSetter = &ATeleportMonitor::StaticSetControllerPose;
	initializationSettings.newInputStateProcessing = &ATeleportMonitor::StaticProcessNewInputState;
	initializationSettings.newInputEventsProcessing = &ATeleportMonitor::StaticProcessNewInputEvents;
	initializationSettings.disconnect = &ATeleportMonitor::StaticDisconnect;
	initializationSettings.messageHandler;
	initializationSettings.reportHandshake = &ATeleportMonitor::StaticReportHandshake;
	// initializationSettings.processAudioInput = &ATeleportMonitor::StaticSetHeadPose;
	initializationSettings.getUnixTimestampNs = nullptr; // use default.
	initializationSettings.start_unix_time_us = 0;

	teleport::server::ApplyInitializationSettings(&initializationSettings);
}

void ATeleportMonitor::EndPlay(const EEndPlayReason::Type reason)
{
	auto &cm = teleport::server::ClientManager::instance();
	cm.shutdown();
	Super::EndPlay(reason);
}

void ATeleportMonitor::StaticSetHeadPose(avs::uid client_uid, const avs::Pose *pose)
{
	UTeleportSessionComponent *session = UTeleportSessionComponent::GetTeleportSessionComponent(client_uid);
	if(session)
		session->SetHeadPose(pose);
}

void ATeleportMonitor::StaticSetControllerPose(avs::uid uid, int index, const avs::PoseDynamic *)
{
}

void ATeleportMonitor::StaticProcessNewInputState(avs::uid client_uid, const teleport::core::InputState *, const uint8_t **, const float **)
{
}

void ATeleportMonitor::StaticProcessNewInputEvents(avs::uid client_uid, uint16_t, uint16_t, uint16_t, const avs::InputEventBinary **, const avs::InputEventAnalogue **, const avs::InputEventMotion **)
{
}

void ATeleportMonitor::StaticDisconnect(avs::uid clientId)
{
}

void ATeleportMonitor::StaticReportHandshake(avs::uid client_uid, const teleport::core::Handshake *h)
{
}

#if WITH_EDITOR
void ATeleportMonitor::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// We want to update when a value is set, not when they are dragging to their desired value.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (GetServerSettings().enableGeometryStreaming ==  true)
		{
			InitialiseGeometrySource();
		}
		UpdateServerSettings();
	}
}
#endif

const teleport::server::ServerSettings &ATeleportMonitor::GetServerSettings()
{
	auto &s=teleport::server::GetServerSettings();
	return s;
}

#include "Engine/TextureRenderTargetCube.h"
void ATeleportMonitor::UpdateServerSettings()
{
	float ClientDrawDistanceOffset = 25.f;

	int32_t captureCubeSize = 0;
	int32_t webcamWidth = 0;
	int32_t webcamHeight = 0;
	const UTeleportSettings *TeleportSettings=GetDefault<UTeleportSettings>();
	teleport::server::GetServerSettings()= {
		RequiredLatencyMs,

		DetectionSphereRadius,
		DetectionSphereBufferDistance,
		ThrottleKpS,

		TeleportSettings->StreamGeometry!=0,
		GeometryTicksPerSecond,
		GeometryBufferCutoffSize,
		ConfirmationWaitTime,
		ClientDrawDistanceOffset,

		bStreamVideo,
		false, // bStreamWebcam

		captureCubeSize,
		webcamWidth,
		webcamHeight,
		TeleportSettings->VideoEncodeFrequency,

		bDeferOutput,
		bDoCubemapCulling,
		BlocksPerCubeFaceAcross,
		CullQuadIndex,
		TargetFPS,
		IDRInterval,
		(avs::VideoCodec)VideoCodec,
		(teleport::server::VideoEncoderRateControlMode)RateControlMode,
		AverageBitrate,
		MaxBitrate,
		bAutoBitRate,
		vbvBufferSizeInFrames,
		bUseAsyncEncoding,
		bUse10BitEncoding,
		bUseYUV444Decoding,
		false, // useAlphaLayerEncoding,
		false, // usePerspectiveRendering
		0,	   // perspectiveWidth;
		0,	   // perspectiveHeight;
		0.f,   // perspectiveFOV
		true,
		1,
		// Audio
		false,
		false,

		DebugStream,
		DebugNetworkPackets,
		DebugControlPackets,
		ResetCache,
		false,
		EstimatedDecodingFrequency,

		1024,
		true,
		QualityLevel,
		CompressionLevel,

		bDisableMainCamera,

		avs::AxesStandard::UnrealStyle,

		64,
		64,
		64,
		64,
		64};
}

bool ATeleportMonitor::CreateSession(avs::uid clientID)
{
	auto &cm = teleport::server::ClientManager::instance();
	auto c = cm.signalingService.getSignalingClient(clientID);
	if (!c)
		return false;
	AGameModeBase *GameMode = GetWorld()->GetAuthGameMode();
	if (!GameMode)
	{
		UE_LOG(LogTeleport, Error, TEXT("GameMode is not set!"));
		return false;
	}
	const UTeleportSettings *TeleportSettings = GetDefault<UTeleportSettings>();
	if (!TeleportSettings)
		return false;
	if(!DefaultClientActor)
	{
		UE_LOG(LogTeleport, Error, TEXT("DefaultClientActor is not set!"));
		return false;
	}
	UTeleportSessionComponent *teleportSessionComponent = UTeleportSessionComponent::GetTeleportSessionComponent(clientID);
	AActor *NewClientActor=nullptr;
	if (!teleportSessionComponent)
	{
		static int pid=0;
		pid++;
		FActorSpawnParameters SpawnParams;
 
		//Actual Spawn. The following function returns a reference to the spawned actor
		NewClientActor = GetWorld()->SpawnActor<AActor>(DefaultClientActor, GetTransform(), SpawnParams);
		if (!NewClientActor)
		{
			UE_LOG(LogTeleport, Error, TEXT("Failed to spawn NewClientActor!"));
			return false;
		}
		// pawn should have a session component.
		teleportSessionComponent = NewClientActor->FindComponentByClass<UTeleportSessionComponent>();
		if (!teleportSessionComponent)
		{
			UE_LOG(LogTeleport, Error, TEXT("UTeleportSessionComponent not found in NewClientActor!"));
			teleportSessionComponent = NewObject<UTeleportSessionComponent>(NewClientActor);
			teleportSessionComponent->RegisterComponent();
			//root->AttachToActor(Actor);
			NewClientActor->AddInstanceComponent(teleportSessionComponent);
		}
		teleportSessionComponent->SetPlayerId(pid);
	}
	teleportSessionComponent->StartSession(clientID);

	const UTeleportSettings &teleportSettings = *TeleportSettings;
	teleport::server::ClientSettings clientSettings;
	teleport::server::ServerSettings &serverSettings=teleport::server::GetServerSettings();
	clientSettings.shadowmapSize =serverSettings.defaultShadowmapSize;
	teleport::core::ClientDynamicLighting clientDynamicLighting;
	clientSettings.captureCubeTextureSize =serverSettings.captureCubeSize;
	clientSettings.backgroundMode = teleport::core::BackgroundMode::COLOUR;
	clientSettings.backgroundColour = {0, 0, 1.f, 0};
	clientSettings.drawDistance =serverSettings.detectionSphereRadius;
	clientSettings.minimumNodePriority = MinimumPriority;
	int faceSize = clientSettings.captureCubeTextureSize;
	int doubleFaceSize = faceSize * 2;
	int halfFaceSize = (int)(faceSize * 0.5);

	int perspectiveWidth =serverSettings.perspectiveWidth;
	int perspectiveHeight =serverSettings.perspectiveHeight;

	uint2 cubeMapsOffset = {0, 0};
	// Offsets to lighting cubemaps in video texture
	if (clientSettings.backgroundMode == teleport::core::BackgroundMode::VIDEO)
	{
		if (serverSettings.usePerspectiveRendering)
		{
			cubeMapsOffset.x = perspectiveWidth / 2;
			cubeMapsOffset.y = perspectiveHeight;
		}
		else
		{
			cubeMapsOffset.x = halfFaceSize * 3;
			cubeMapsOffset.y = doubleFaceSize;
		}
	}
	// UpdateClientDynamicLighting(cubeMapsOffset);
	//  Depth is stored in color's alpha channel if alpha layer encoding is enabled.
	if (serverSettings.useAlphaLayerEncoding)
	{
		// We don't currently encode shadows or use light cubemap
		// clientSettings.lightPos = clientSettings.diffusePos + new Vector2Int(clientSettings.diffuseCubemapSize * 3, 0);
		// clientSettings.shadowmapPos = clientSettings.lightPos + new Vector2Int(MIPS_WIDTH, 0);
		clientSettings.webcamPos = {clientDynamicLighting.diffuseCubemapSize * 3, 0};
	}
	else
	{
		int2 shadowmapPos = {0, 2 * clientDynamicLighting.diffuseCubemapSize};
		uint2 cubeMpasOffset;
		uint2 webcamPos = cubeMapsOffset + uint2(clientDynamicLighting.specularCubemapSize * 3, clientDynamicLighting.specularCubemapSize * 2);
	}
	uint2 webcamSize = {(unsigned)serverSettings.webcamWidth, (unsigned)serverSettings.webcamHeight};
	// find the size of the video texture.
	if (clientSettings.backgroundMode == teleport::core::BackgroundMode::VIDEO)
	{
		clientSettings.videoTextureSize.x = clientSettings.videoTextureSize.y = 0;
		if (serverSettings.usePerspectiveRendering)
		{
			clientSettings.videoTextureSize.x = std::max(clientSettings.videoTextureSize.x, serverSettings.perspectiveWidth);
			clientSettings.videoTextureSize.y = std::max(clientSettings.videoTextureSize.y, serverSettings.perspectiveHeight);
		}
		else
		{
			clientSettings.videoTextureSize.x = std::max(clientSettings.videoTextureSize.x, faceSize * 3);
			clientSettings.videoTextureSize.y = std::max(clientSettings.videoTextureSize.y, faceSize * 2);
		}
		// Is depth separate?
		if (!serverSettings.useAlphaLayerEncoding)
		{
			clientSettings.videoTextureSize.y = std::max(clientSettings.videoTextureSize.y, clientSettings.videoTextureSize.y + clientSettings.videoTextureSize.y / 2);
		}
	}
	clientSettings.videoTextureSize.x = std::max(clientSettings.videoTextureSize.x, clientDynamicLighting.diffusePos.x + clientDynamicLighting.diffuseCubemapSize * 3);
	clientSettings.videoTextureSize.y = std::max(clientSettings.videoTextureSize.y, clientDynamicLighting.diffusePos.y + clientDynamicLighting.diffuseCubemapSize * 2);
	if (serverSettings.enableWebcamStreaming)
	{
		clientSettings.videoTextureSize.x = std::max(clientSettings.videoTextureSize.x, clientSettings.webcamPos.x + clientSettings.webcamSize.x);
		clientSettings.videoTextureSize.y = std::max(clientSettings.videoTextureSize.y, clientSettings.webcamPos.y + clientSettings.webcamSize.y);
	}

	/*	avs::VideoEncodeCapabilities videoEncodeCapabilities = VideoEncoder.GetEncodeCapabilities();
		if (clientSettings.videoTextureSize.x < videoEncodeCapabilities.minWidth || clientSettings.videoTextureSize.x > videoEncodeCapabilities.maxWidth || clientSettings.videoTextureSize.y < videoEncodeCapabilities.minHeight || clientSettings.videoTextureSize.y > videoEncodeCapabilities.maxHeight)
		{
			Debug.LogError("The video encoder does not support the video texture dimensions " + clientSettings.videoTextureSize.x + " x " + clientSettings.videoTextureSize.y + ".");
		}*/
	teleport::server::ClientManager::instance().SetClientSettings(clientID, clientSettings);
	return true;
}
void ATeleportMonitor::CheckForNewClients()
{
	auto &cm = teleport::server::ClientManager::instance();
	avs::uid id = cm.firstUnlinkedClientUid();
	if (id == 0)
	{
		return;
	}
	if(CreateSession(id))
		cm.popFirstUnlinkedClientUid(id);
}

void ATeleportMonitor::Tick(float DeltaTS)
{
	auto &cm = teleport::server::ClientManager::instance();
	cm.tick(DeltaTS);
	CheckForNewClients();
}

void ATeleportMonitor::InitialiseGeometrySource()
{
	UWorld *world = GetWorld();

	GeometrySource *geometrySource = ITeleport::Get().GetGeometrySource();
	geometrySource->Initialise(this, world);

	ECollisionChannel remotePlayChannel;
	FCollisionResponseParams profileResponses;
	// Returns the collision channel used by Teleport; uses the object type of the profile to determine the channel.
	UCollisionProfile::GetChannelAndResponseParams("TeleportSensor", remotePlayChannel, profileResponses);
#if 0
	TArray<AActor*> staticMeshActors;
	UGameplayStatics::GetAllActorsOfClass(world, AStaticMeshActor::StaticClass(), staticMeshActors);
	//Decompose all relevant actors into streamable geometry.
	for(auto actor : staticMeshActors)
	{
		UMeshComponent* rootMesh = Cast<UMeshComponent>(actor->GetComponentByClass(UMeshComponent::StaticClass()));

		//Decompose the meshes that would cause an overlap event to occur with the "TeleportSensor" profile.
		if(rootMesh->GetGenerateOverlapEvents() && rootMesh->GetCollisionResponseToChannel(remotePlayChannel) != ECollisionResponse::ECR_Ignore)
		{
			geometrySource->AddNode(actor, true);
		}
	}

	TArray<AActor*> lightActors;
	UGameplayStatics::GetAllActorsOfClass(world, ALight::StaticClass(), lightActors);

	//Decompose all relevant light actors into streamable geometry.
	for(auto actor : lightActors)
	{
		auto sgc = actor->GetComponentByClass(UStreamableNode::StaticClass());
		if(sgc)
		{
			//TArray<UTexture2D*> shadowAndLightMaps = static_cast<UStreamableNode*>(sgc)->GetLightAndShadowMaps();
			ULightComponent* lightComponent = static_cast<UStreamableNode*>(sgc)->GetLightComponent();
			if(lightComponent)
			{
				//ShadowMapData smd(lc);
				geometrySource->AddNode(actor, true);
			}
		}
	}
#endif
	geometrySource->CompressTextures();
}
#endif