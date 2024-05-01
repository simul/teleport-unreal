// Copyright 2018-2024 Simul.co

#pragma once

#include "UnrealServerSettings.h"

namespace teleport::server
{
	struct CameraInfo;
	struct ClientNetworkContext;
}

class FSceneInterface;
class UTexture;

struct FSurfaceTexture
{
	FTexture2DRHIRef Texture;
	FUnorderedAccessViewRHIRef UAV;
};

class IEncodePipeline
{
public:
	virtual ~IEncodePipeline() = default;

	virtual void Initialise(avs::uid clientid,const FUnrealCasterEncoderSettings& InParams, teleport::server::ClientNetworkContext* context, class ATeleportMonitor* InMonitor) = 0;
	virtual void Release() = 0;
	virtual void CullHiddenCubeSegments(FSceneInterface* InScene, teleport::server::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor) = 0;
	virtual void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags) = 0;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR) = 0;
	virtual FSurfaceTexture *GetSurfaceTexture() = 0;
};
