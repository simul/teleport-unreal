// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/TeleportCaptureComponent.h"

#include "ContentStreaming.h"
#if 1
#include "Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor.h"
#include "Pipelines/EncodePipelineMonoscopic.h"
#include "TeleportModule.h"
#include "TeleportMonitor.h"
#include "Components/TeleportReflectionCaptureComponent.h"
#include "TeleportSettings.h"

UTeleportCaptureComponent::UTeleportCaptureComponent()
	: bRenderOwner(false)
	, TeleportReflectionCaptureComponent(nullptr)
	, bIsStreaming(false)
	, bSendKeyframe(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = true;
	bCaptureOnMovement = false;
}

void UTeleportCaptureComponent::BeginPlay()
{
	ShowFlags.EnableAdvancedFeatures();
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAntiAliasing(true);

	if (TextureTarget && !TextureTarget->bCanCreateUAV)
	{
		TextureTarget->bCanCreateUAV = true;
	}

	ATeleportMonitor* Monitor = ATeleportMonitor::Instantiate(GetWorld());
	if (Monitor->bOverrideTextureTarget && Monitor->SceneCaptureTextureTarget)
	{
		TextureTarget = Monitor->SceneCaptureTextureTarget;
	}
	else
	{
		Monitor->SceneCaptureTextureTarget = TextureTarget;
		Monitor->UpdateServerSettings();
	}

	// Make sure that there is enough time in the render queue.
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), FString("g.TimeoutForBlockOnRenderFence 300000"));

	Super::BeginPlay();

	AActor* OwnerActor = GetTypedOuter<AActor>();
	if (bRenderOwner)
	{
		TeleportReflectionCaptureComponent = Cast<UTeleportReflectionCaptureComponent>(OwnerActor->GetComponentByClass(UTeleportReflectionCaptureComponent::StaticClass()));
	}
	else
	{
		check(OwnerActor);

		TArray<AActor*> OwnerAttachedActors;
		OwnerActor->GetAttachedActors(OwnerAttachedActors);
		HiddenActors.Add(OwnerActor);
		HiddenActors.Append(OwnerAttachedActors);

	}
}

void UTeleportCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
}

void UTeleportCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Aidan: Below allows the capture to avail of Unreal's texture streaming
	// Add the view information every tick because its only used for one tick and then
	// removed by the streaming manager.
	if(TextureTarget)
	{
		int32 W = TextureTarget->GetSurfaceWidth();
		float FOV = 90.0f;
		IStreamingManager::Get().AddViewInformation(GetComponentLocation(), W, W / FMath::Tan(FOV));

		ATeleportMonitor *Monitor = ATeleportMonitor::Instantiate(GetWorld());
		if (bIsStreaming && bCaptureEveryFrame && Monitor && Monitor->bDisableMainCamera)
		{
			CaptureScene();
		}
	}
}

const FUnrealCasterEncoderSettings& UTeleportCaptureComponent::GetEncoderSettings()
{
	if (EncodeParams.bDecomposeCube)
	{
		int32 W = TextureTarget->GetSurfaceWidth();
		// 3 across...
		EncodeParams.FrameWidth = 3 * W;
		// and 2 down... for the colour, depth, and light cubes.
		EncodeParams.FrameHeight = 3 * W; // (W + W / 2);
	}
	else
	{
		EncodeParams.FrameWidth = 2048;
		EncodeParams.FrameHeight = 1024 + 512;
	}
	return EncodeParams;
}

void UTeleportCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	// Do not render to scene capture or do encoding if not streaming
	if(!bIsStreaming)
		return;

	ATeleportMonitor* Monitor = ATeleportMonitor::Instantiate(GetWorld());
	if(Monitor && Monitor->VideoEncodeFrequency > 1)
	{
		static int u = 1;
		u--;
		u = std::min(Monitor->VideoEncodeFrequency, u);
		if(!u)
		{
			u = Monitor->VideoEncodeFrequency;
		}
		else
		{
			return;
		}
	}

	if(Monitor->bDoCubemapCulling)
	{
		CullHiddenCubeSegments();
	}

	// Aidan: The parent function belongs to SceneCaptureComponentCube and is located in SceneCaptureComponent.cpp. 
	// The parent function calls UpdateSceneCaptureContents function in SceneCaptureRendering.cpp.
	// UpdateSceneCaptureContents enqueues the rendering commands to render to the scene capture cube's render target.
	// The parent function is called from the static function UpdateDeferredCaptures located in
	// SceneCaptureComponent.cpp. UpdateDeferredCaptures is called by the BeginRenderingViewFamily function in SceneRendering.cpp.
	// Therefore the rendering commands queued after this function call below directly follow the scene capture cube's commands in the queue.
	Super::UpdateSceneCaptureContents(Scene);

	if(Monitor->bStreamVideo && TextureTarget)
	{
		FTransform Transform = GetComponentTransform();

		EncodePipeline->PrepareFrame(Scene, TextureTarget, Transform, QuadsToRender);
		if(TeleportReflectionCaptureComponent && EncodeParams.bDecomposeCube)
		{
			TeleportReflectionCaptureComponent->UpdateContents(
				Scene->GetRenderScene(),
				TextureTarget,
				Scene->GetFeatureLevel());
			int32 W = TextureTarget->GetSurfaceWidth();
			FIntPoint Offset0((W * 3) / 2, W * 2);
			TeleportReflectionCaptureComponent->PrepareFrame(
				Scene->GetRenderScene(),
				EncodePipeline->GetSurfaceTexture(),
				Scene->GetFeatureLevel(), Offset0);
		}
		EncodePipeline->EncodeFrame(Scene, TextureTarget, Transform, bSendKeyframe);
		// The client must request it again if it needs it
		bSendKeyframe = false;
	}
}

bool UTeleportCaptureComponent::ShouldRenderFace(int32 FaceId) const
{
	if (FacesToRender.Num() <= FaceId)
	{
		return true;
	}
	return FacesToRender[FaceId];
}

void UTeleportCaptureComponent::CullHiddenCubeSegments()
{
	assert(CubeQuads.Num >= 6);

	// Aidan: Currently not going to do this on GPU because doing it on game thread allows us to  
	// share the output with the capture component to cull faces from rendering.
	ATeleportMonitor *Monitor = ATeleportMonitor::Instantiate(GetWorld());

	FQuat UnrealOrientation = FQuat(ClientCamInfo.orientation.x, ClientCamInfo.orientation.y, ClientCamInfo.orientation.z, ClientCamInfo.orientation.w);
	const FVector LookAt = UnrealOrientation.GetForwardVector() * 10;
	const FVector Up = UnrealOrientation.GetUpVector();
	const FLookAtMatrix ViewMatrix = FLookAtMatrix(FVector::ZeroVector, LookAt, Up);

	// Convert FOV from degrees to radians 
	const float FOV = FMath::DegreesToRadians(ClientCamInfo.fov);

	FMatrix ProjectionMatrix;
	if (static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FReversedZPerspectiveMatrix(FOV, ClientCamInfo.width, ClientCamInfo.height, 0, 0));
	}
	else
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FPerspectiveMatrix(FOV, ClientCamInfo.width, ClientCamInfo.height, 0, 0));
	}

	const FMatrix VP = ViewMatrix * ProjectionMatrix;

	// Use to prevent shared vectors from being tested more than once
	TMap<FVector, bool> VectorIntersectionMap;

	const uint32 BlocksPerFace = Monitor->BlocksPerCubeFaceAcross * Monitor->BlocksPerCubeFaceAcross;

	const FVector* Vertices = reinterpret_cast<FVector*>(&CubeQuads[0]);

	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		bool FaceIntersects = false;

		// Iterate through each of the face's quads
		for (uint32 j = 0; j < BlocksPerFace; ++j)
		{
			uint32 QuadIndex = i * BlocksPerFace + j;

			bool Intersects = false;

			// Iterate through each of the quad's vertices
			for (uint32 k = 0; k < 4; ++k)
			{
				uint32 VIndex = (QuadIndex * 4) + k;
				const auto& V = Vertices[VIndex];
				const bool* Value = VectorIntersectionMap.Find(V);
				if (Value && *Value)
				{
					Intersects = true;
					break;
				}
				else
				{
					if (VectorIntersectsFrustum(V, VP))
					{
						Intersects = true;
						VectorIntersectionMap.Add(TPair<FVector, bool>(V, true));
						break;
					}
					VectorIntersectionMap.Add(TPair<FVector, bool>(V, false));
				}
			}

			// For debugging only! Cull only the quad selected by the user 
			if (Monitor->CullQuadIndex >= 0 && Monitor->CullQuadIndex < CubeQuads.Num())
			{
				if (QuadIndex == Monitor->CullQuadIndex)
				{
					Intersects = false;
				}
				else
				{
					Intersects = true;
				}
			}

			QuadsToRender[QuadIndex] = Intersects;

			if (Intersects)
			{
				FaceIntersects = true;
			}
		}
		FacesToRender[i] = FaceIntersects;
	}
}

void UTeleportCaptureComponent::CreateCubeQuads(TArray<FQuad>& Quads, uint32 BlocksPerFaceAcross, float CubeWidth)
{
	const float HalfWidth = CubeWidth / 2;
	const float QuadSize = CubeWidth / (float)BlocksPerFaceAcross;

	// Unreal Engine coordinates: X is forward, Y is right, Z is up, 
	const FVector StartPos = FVector(HalfWidth, -HalfWidth, -HalfWidth); // Bottom left of front face
	// Aidan: First qauternion is rotated to match Unreal's cubemap face rotations
	// Second quaternion is to get position, forward and side vectors relative to front face
	// In quaternion multiplication, the rhs or second qauternion is applied first
	static const float Rad90 = FMath::DegreesToRadians(90);
	static const float Rad180 = FMath::DegreesToRadians(180);
	static const FQuat FrontQuat = FQuat(FVector::ForwardVector, -Rad90); // No need to multiply as second qauternion would be identity
	static const FQuat BackQuat = (FQuat(FVector::ForwardVector, Rad90) * FQuat(FVector::UpVector, Rad180)).GetNormalized(SMALL_NUMBER);
	static const FQuat RightQuat = FQuat(FVector::RightVector, Rad180) * FQuat(FVector::UpVector, Rad90).GetNormalized(SMALL_NUMBER);
	static const FQuat LeftQuat = FQuat(FVector::UpVector, -Rad90); // No need to multiply as first quaternion would be identity
	static const FQuat TopQuat = FQuat(FVector::UpVector, -Rad90) * FQuat(FVector::RightVector, -Rad90).GetNormalized(SMALL_NUMBER);
	static const FQuat BottomQuat = FQuat(FVector::UpVector, Rad90) * FQuat(FVector::RightVector, Rad90).GetNormalized(SMALL_NUMBER);

	static const FQuat FaceQuats[6] = { FrontQuat, BackQuat, RightQuat, LeftQuat, TopQuat, BottomQuat };

	const uint32 NumQuads = BlocksPerFaceAcross * BlocksPerFaceAcross * 6;

	Quads.Empty();
	Quads.Reserve(NumQuads);

	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		const FQuat& q = FaceQuats[i];
		const FVector RightVec = q.RotateVector(FVector::RightVector).GetSafeNormal() * QuadSize;
		const FVector UpVec = q.RotateVector(FVector::UpVector).GetSafeNormal() * QuadSize;
		FVector Pos = q.RotateVector(StartPos);

		// Go right
		for (uint32 j = 0; j < BlocksPerFaceAcross; ++j)
		{
			FVector QuadPos = Pos;
			// Go up
			for (uint32 k = 0; k < BlocksPerFaceAcross; ++k)
			{
				FQuad Quad;
				Quad.BottomLeft = QuadPos;
				Quad.TopLeft = QuadPos + UpVec;
				Quad.BottomRight = QuadPos + RightVec;
				Quad.TopRight = Quad.TopLeft + RightVec;

				QuadPos = Quad.TopLeft;

				Quads.Emplace(MoveTemp(Quad));
			}
			Pos += RightVec;
		}
	}
}

bool UTeleportCaptureComponent::VectorIntersectsFrustum(const FVector& Vector, const FMatrix& ViewProjection)
{
	FPlane Result = ViewProjection.TransformFVector4(FVector4(Vector, 1.0f));
	if (Result.W <= 0.0f)
	{
		return false;
	}
	// the result of this will be x and y coords in -1..1 projection space
	const float RHW = 1.0f / Result.W;
	Result.X *= RHW;
	Result.Y *= RHW;

	// Move from projection space to normalized 0..1 UI space
	/*const float NormX = (Result.X / 2.f) + 0.5f;
	const float NormY = 1.f - (Result.Y / 2.f) - 0.5f;

	if (NormX < 0.0f || NormX > 1.0f || NormY < 0.0f || NormY > 1.0f)
	{
		return false;
	}*/

	if (Result.X < -1.0f || Result.X > 1.0f || Result.Y < -1.0f || Result.Y > 1.0f)
	{
		return false;
	}
	return true;
}

void UTeleportCaptureComponent::startStreaming(avs::uid id,teleport::server::ClientNetworkContext* context)
{
	clientId = id;
	ATeleportMonitor *Monitor = ATeleportMonitor::Instantiate(GetWorld());
	if (!ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &UTeleportCaptureComponent::OnViewportDrawn);
			GameViewport->bDisableWorldRendering = Monitor->bDisableMainCamera;
		}
	}

	EncodePipeline.reset(new FEncodePipelineMonoscopic);
	EncodePipeline->Initialise(clientId,EncodeParams, context, Monitor);

	if(TeleportReflectionCaptureComponent)
	{
		TeleportReflectionCaptureComponent->Initialise();
		TeleportReflectionCaptureComponent->bAttached = true;
	}

	bIsStreaming = true;
	bCaptureEveryFrame = true;
	bSendKeyframe = false;

	FQuat UnrealOrientation = GetComponentTransform().GetRotation();
	ClientCamInfo.orientation = {(float)UnrealOrientation.X, (float)UnrealOrientation.Y, (float)UnrealOrientation.Z, (float)UnrealOrientation.W};

	CreateCubeQuads(CubeQuads, Monitor->BlocksPerCubeFaceAcross, TextureTarget->GetSurfaceWidth());

	QuadsToRender.Init(true, CubeQuads.Num());
	FacesToRender.Init(true, 6);
}

void UTeleportCaptureComponent::stopStreaming()
{
	clientId=0;
	bIsStreaming = false;
	bCaptureEveryFrame = false;
	CubeQuads.Empty();
	QuadsToRender.Empty();
	FacesToRender.Empty();

	if(EncodePipeline)
	{
		EncodePipeline->Release();
		EncodePipeline.reset();
	}

	if (ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}

	UE_LOG(LogTeleport, Log, TEXT("Capture: Stopped streaming"));
}

void UTeleportCaptureComponent::requestKeyframe()
{
	bSendKeyframe = true;
}

void UTeleportCaptureComponent::OnViewportDrawn()
{
}

teleport::server::CameraInfo& UTeleportCaptureComponent::getClientCameraInfo()
{
	return ClientCamInfo;
}


#endif