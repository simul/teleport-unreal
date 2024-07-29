// Copyright 2018-2024 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "TeleportSignalingService.h"
#include "Teleport/Public/GeometryStreamingService.h"

#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#include "TeleportServer/ClientMessaging.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"

#include "SessionComponent.generated.h"

class USphereComponent;
class UStreamableRootComponent;
class UTeleportPawnComponent;

namespace avs
{
	typedef uint64_t uid;
}
 
/// A UTeleportSessionComponent should be present on any PlayerController used for Teleport connections. 
UCLASS(meta=(BlueprintSpawnableComponent))
class TELEPORT_API UTeleportSessionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTeleportSessionComponent();
	virtual ~UTeleportSessionComponent();
	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	static UTeleportSessionComponent *GetTeleportSessionComponent(avs::uid clid);

	UFUNCTION(BlueprintCallable, Category=Teleport)
	void StopSession();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Teleport)
	uint32 bAutoStartSession:1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Teleport)
	int32 AutoListenPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Teleport)
	int32 AutoDiscoveryPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Teleport)
	int32 DisconnectTimeout;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Teleport)
	float InputTouchSensitivity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	UTeleportPawnComponent *TeleportPawnComponent;

	void StartSession(avs::uid clientID);
	void EndSession();
	
	void SetHeadPose(const avs::Pose *newHeadPose);
	void SetControllerPose( avs::uid id, const avs::PoseDynamic *newPose);
	void ProcessInputState( const teleport::core::InputState *, const uint8_t **, const float **);
	void ProcessInputEvents( uint16_t numBinaryEvents, uint16_t numAnalogueEvents, uint16_t numMotionEvents, const avs::InputEventBinary **binaryEventsPtr, const avs::InputEventAnalogue **analogueEventsPtr, const avs::InputEventMotion **motionEventsPtr);

	static bool clientStoppedRenderingNode(avs::uid clientID, avs::uid nodeID);
	static bool clientStartedRenderingNode(avs::uid clientID, avs::uid nodeID);
	
	void SetPlayerId(int p) ;
	int GetPlayerId()const;
private:
int playerId=0;
	void ApplyPlayerInput(float DeltaTime);
	
	static void TranslateButtons(uint32_t ButtonMask, TArray<FKey>& OutKeys);
	void StopStreaming();

	void StreamNearbyNodes();

	UFUNCTION()
	void OnInnerSphereBeginOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);

	UFUNCTION()
	void OnOuterSphereEndOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex);

	avs::uid StreamToClient(UStreamableRootComponent *);
	void UnstreamFromClient(UStreamableRootComponent *);
	void AddDetectionSpheres();

	TWeakObjectPtr<AActor> ClientActor;

	TWeakObjectPtr<USphereComponent> DetectionSphereInner; //Detects when a steamable actor has moved close enough to the client to be sent to them.
	TWeakObjectPtr<USphereComponent> DetectionSphereOuter; //Detects when a streamable actor has moved too far from the client.

	struct FInputQueue
	{
		TArray<FKey> ButtonsPressed;
		TArray<FKey> ButtonsReleased;
	};
	FInputQueue InputQueue;
	FVector2D   InputTouchAxis;
	FVector2D   InputJoystick;

	bool IsStreaming = false;
	avs::uid ClientID=0;
	avs::uid rootNodeUid=0;
#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
	/** Stat id of this object, 0 if nobody asked for it yet */
	mutable TStatId				BandwidthStatID;
	float						Bandwidth;
#endif // STATS || ENABLE_STATNAMEDEVENTS
	class ATeleportMonitor	*Monitor;
};
