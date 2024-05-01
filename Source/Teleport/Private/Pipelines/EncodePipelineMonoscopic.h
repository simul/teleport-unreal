// Copyright 2018-2024 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "EncodePipelineInterface.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#include "TeleportServer/VideoEncodePipeline.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"


class UTextureRenderTargetCube;
class FTextureRenderTargetResource;

namespace teleport::server
{
	class VideoEncodePipeline;
}

class FEncodePipelineMonoscopic : public IEncodePipeline
{
public:

	/* Begin IEncodePipeline interface */
	void Initialise(avs::uid clientid,const FUnrealCasterEncoderSettings& InSettings, struct teleport::server::ClientNetworkContext* context, ATeleportMonitor* InMonitor) override;
	void Release() override;
	void CullHiddenCubeSegments(FSceneInterface* InScene, teleport::server::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor) override;
	void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags) override;
	void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR) override;
	FSurfaceTexture *GetSurfaceTexture() override
	{
		return &ColorSurfaceTexture;
	}
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void CullHiddenCubeSegments_RenderThread(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, teleport::server::CameraInfo CameraInfo, int32 FaceSize, uint32 Divisor);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel, FVector CameraPosition, TArray<bool> BlockIntersectionFlags);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform, bool forceIDR);

	template<typename ShaderType>
	void DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel);

	void DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel,FVector CameraPosition, const TArray<bool>& BlockIntersectionFlags);
	
	struct FShaderFlag
	{
		uint32 flag = 0;
		uint32 pad0, pad1, pad2 = 0;
	};

	teleport::server::ClientNetworkContext* ClientNetworkContext;

	FUnrealCasterEncoderSettings Settings;
	FSurfaceTexture ColorSurfaceTexture;
	FSurfaceTexture DepthSurfaceTexture;

	TUniquePtr<class teleport::server::VideoEncodePipeline> Pipeline;

	FVector2D WorldZToDeviceZTransform;

	FTextureRHIRef				SourceCubemapRHI;
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHIRef;

	ATeleportMonitor *Monitor;
	avs::uid clientId = 0;
};
