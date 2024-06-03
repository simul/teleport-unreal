#pragma once

#include "CoreMinimal.h"
#include "MeshPassProcessor.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "MeshMaterialShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "RHI.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "UniformBuffer.h"
#include "RHICommandList.h"
#include "ShaderCompilerCore.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "RenderGraphResources.h"

#include "RenderGraphResources.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"

#include "ResolveLightmapComputeShader.generated.h"

#define NUM_THREADS_ResolveLightmapComputeShader_X 32
#define NUM_THREADS_ResolveLightmapComputeShader_Y 32
#define NUM_THREADS_ResolveLightmapComputeShader_Z 1


struct TELEPORT_API FResolveLightmapComputeShaderDispatchParams
{
	int X=1;
	int Y=1;
	int Z=1;

	FTexture* TargetTexture=nullptr;
	FTexture* SourceTexture=nullptr;

	FVector4f LightMapScale;
	FVector4f LightMapAdd;
};

// This is a public interface that we define so outside code can invoke our compute shader.
class TELEPORT_API FResolveLightmapComputeShaderInterface
{
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FResolveLightmapComputeShaderDispatchParams Params
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FResolveLightmapComputeShaderDispatchParams Params
	)
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[Params](FRHICommandListImmediate& RHICmdList)
			{
				DispatchRenderThread(RHICmdList,Params);
			});
	}

	// Dispatches this shader. Can be called from any thread
	static void Dispatch(
		FResolveLightmapComputeShaderDispatchParams Params
	)
	{
		if(IsInRenderingThread())
		{
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(),Params);
		}
		else
		{
			DispatchGameThread(Params);
		}
	}
};

// This is a static blueprint library that can be used to invoke our compute shader from blueprints.
UCLASS()
class TELEPORT_API UResolveLightmapComputeShaderLibrary: public UObject
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable)
		static void ExecuteRTComputeShader(UTextureRenderTarget2D* RT,FVector4f Scale,FVector4f Add)
	{
		// Create a dispatch parameters struct and fill it the input array with our args
		FResolveLightmapComputeShaderDispatchParams Params={RT->SizeX,RT->SizeY,1,RT->GameThread_GetRenderTargetResource(),nullptr,Scale,Add};

		FResolveLightmapComputeShaderInterface::Dispatch(Params);
	}
};
