// Copyright 2018-2024 Simul.co

#pragma once

#include <memory>

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponentCube.h"


#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#include "TeleportServer/CaptureDelegates.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"

#include "Pipelines/EncodePipelineInterface.h"
#include "UnrealServerSettings.h"
#include "TeleportCaptureComponent.generated.h"

//! This component is added to the player pawn. Derived from the SceneCaptureCube component, it
//! continuously captures the surrounding image around the Pawn. However, it has
//! other responsibilities as well.
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), meta = (BlueprintSpawnableComponent))
class TELEPORT_API UTeleportCaptureComponent : public USceneCaptureComponentCube
{
	GENERATED_BODY()
public:
	UTeleportCaptureComponent();
	virtual ~UTeleportCaptureComponent() = default;

	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	void startStreaming(avs::uid clientID,teleport::server::ClientNetworkContext* context);
	void stopStreaming();

	void requestKeyframe();

	teleport::server::CameraInfo& getClientCameraInfo();

	bool ShouldRenderFace(int32 FaceId) const ;//override;

	void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	FUnrealCasterEncoderSettings EncodeParams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Teleport)
	uint32 bRenderOwner : 1;

	const FUnrealCasterEncoderSettings& GetEncoderSettings();
private: 
	avs::uid clientId=0;
	struct FQuad
	{
		FVector BottomLeft;
		FVector TopLeft;
		FVector BottomRight;
		FVector TopRight;
	};

	void OnViewportDrawn();
	FDelegateHandle ViewportDrawnDelegateHandle;
	void CullHiddenCubeSegments();
	static void CreateCubeQuads(TArray<FQuad>& Quads, uint32 BlocksPerFaceAcross, float CubeWidth);
	static bool VectorIntersectsFrustum(const FVector& Vector, const FMatrix& ViewProjection);

	std::unique_ptr<IEncodePipeline> EncodePipeline;
	teleport::server::CameraInfo ClientCamInfo;

	TArray<FQuad> CubeQuads;
	TArray<bool> QuadsToRender;
	TArray<bool> FacesToRender;

	class UTeleportReflectionCaptureComponent *TeleportReflectionCaptureComponent;
	bool bIsStreaming;
	bool bSendKeyframe;
};