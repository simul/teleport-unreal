#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Variant.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "TeleportSettings.h"

#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"

#include "TeleportServer/InteropStructures.h"
#include "TeleportServer/ServerSettings.h"

#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"


#include "TeleportMonitor.generated.h"

/**
 * Rate Control Mode
 */ 
UENUM(BlueprintType)
enum class EncoderRateControlMode : uint8
{
	RC_CONSTQP = 0x0 UMETA(DisplayName = "Constant Quantization Parameter"), /**< Constant QP mode */
	RC_VBR = 0x1 UMETA(DisplayName = "Variable Bitrate"), /**< Variable bitrate mode */
	RC_CBR = 0x2 UMETA(DisplayName = "Constant Bitrate"), /**< Constant bitrate mode */
	RC_CBR_LOWDELAY_HQ = 0x3 UMETA(DisplayName = "Constant Bitrate Low Delay HQ"), /**< low-delay CBR, high quality */
	RC_CBR_HQ = 0x4 UMETA(DisplayName = "Constant Bitrate HQ (slower)"), /**< CBR, high quality (slower) */
	RC_VBR_HQ = 0x5 UMETA(DisplayName = "Variable Bitrate HQ (slower)") /**< VBR, high quality (slower) */
};

/**
 * Video Codex
 */
UENUM(BlueprintType)
enum class VideoCodec : uint8
{
	UNKNOWN=0,
	H264 = 0x1 UMETA(DisplayName = "H.264 / AVC"), 
	HEVC = 0x2 UMETA(DisplayName = "H.265 / HEVC)") 
};

// A runtime actor to enable control and monitoring of the global Teleport state.
UCLASS(Blueprintable, hidecategories = (Object,Actor,Rendering,Replication,Input,Actor,Collision,LOD,Cooking) )
class ATeleportMonitor : public AActor
{
	GENERATED_BODY()
protected:
	virtual ~ATeleportMonitor();
public:
	ATeleportMonitor(const class FObjectInitializer& ObjectInitializer);

	/// Create or get the singleton instance of TeleportMonitor for the given UWorld.
	static ATeleportMonitor* Instantiate(UWorld* world);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SRT)
	int32 RequiredLatencyMs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	FString SessionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	FString ClientIP;
	
	/** Minimum Streamable priority for geometry streaming. A UStreamableRootComponent with smaller Priority than this will not stream. See also UStreamableRootComponent::Priority.*/ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	int32 MinimumPriority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	int32 DetectionSphereRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	int32 DetectionSphereBufferDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	int32 ExpectedLag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	int64 ThrottleKpS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Teleport)
	UBlueprint* HandActor;

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category=Teleport)
	TSubclassOf<AActor> DefaultClientActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	uint8 GeometryTicksPerSecond;

	// Size we stop encoding nodes at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	int32 GeometryBufferCutoffSize;

	//Seconds to wait before resending a resource.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry, meta = (ClampMin = "0.5", ClampMax = "300.0"))
	float ConfirmationWaitTime;

	// Determines if video will be encoded and streamed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bStreamVideo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bOverrideTextureTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	class UTextureRenderTargetCube* SceneCaptureTextureTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 VideoEncodeFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bDeferOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bDoCubemapCulling;

	// The number of blocks per cube face will be this value squared
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 BlocksPerCubeFaceAcross;

	// This culls a quad at the index. For debugging only
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 CullQuadIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 TargetFPS;

	// Value of 0 means only first frame will be an IDR unless a frame is lost
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 IDRInterval;

	// H264 DOES NOT SUPPORT 10-bit Encoding!!!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	VideoCodec VideoCodec;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	EncoderRateControlMode RateControlMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 AverageBitrate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 MaxBitrate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bAutoBitRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 vbvBufferSizeInFrames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bUseAsyncEncoding;

	// NOT SUPPORTED BY H264!!!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bUse10BitEncoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	bool bUseYUV444Decoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	int32 DebugStream;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	bool DebugNetworkPackets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	bool DebugControlPackets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	bool Checksums;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	bool ResetCache;

	//An estimate of how frequently the client will decode the packets sent to it; used by throttling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint8 EstimatedDecodingFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 QualityLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 CompressionLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera)
	bool bDisableMainCamera;

	// In order:
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void UpdateServerSettings();

	inline avs::uid GetServerID()
	{
		return ServerID;
	}

	static const teleport::server::ServerSettings& GetServerSettings();
	void Tick(float DeltaTS) override;
	void CheckForNewClients();
	bool CreateSession(avs::uid clientID);

	static void StaticSetHeadPose(avs::uid client_uid, const teleport::core::Pose *);
	static void StaticSetControllerPose(avs::uid uid, int index, const teleport::core::PoseDynamic *);
	static void StaticProcessNewInputState(avs::uid client_uid, const teleport::core::InputState *, const uint8_t **, const float **);
	static void StaticProcessNewInputEvents(avs::uid client_uid, uint16_t, uint16_t, uint16_t, const teleport::core::InputEventBinary **, const teleport::core::InputEventAnalogue **, const teleport::core::InputEventMotion **);
	static void StaticDisconnect(avs::uid clientID);
	static void StaticReportHandshake(avs::uid client_uid, const teleport::core::Handshake *h);
	
	private:
	static TMap<UWorld*, ATeleportMonitor*> Monitors;

	avs::uid ServerID = 0; //UID of the server; resets between sessions.
	std::string sigport;
	void InitialiseGeometrySource();
};
