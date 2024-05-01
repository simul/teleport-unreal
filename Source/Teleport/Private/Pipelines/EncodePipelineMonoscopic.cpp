// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "EncodePipelineMonoscopic.h"
#include "TeleportModule.h"
#include "TeleportRHI.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#if PLATFORM_WINDOWS
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif 
#include "libavstream/surfaces/surface_dx11.hpp"
#include "libavstream/surfaces/surface_dx12.hpp"
#endif
#include <algorithm>

#include <TeleportServer/ClientNetworkContext.h>
#include <TeleportServer/VideoEncodePipeline.h>
#include <TeleportServer/CasterTypes.h>
#include <TeleportServer/CaptureDelegates.h>
#include <TeleportServer/ClientManager.h>
#include <TeleportServer/ClientData.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"
#include "TeleportMonitor.h"

#include "Engine/TextureRenderTargetCube.h"

#include "RHIDefinitions.h"
//#include "Public/GlobalShader.h"
#include "PipelineStateCache.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "HAL/UnrealMemory.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "GlobalShader.h"

DECLARE_FLOAT_COUNTER_STAT(TEXT("TeleportEncodePipelineMonoscopic"), Stat_GPU_TeleportEncodePipelineMonoscopic, STATGROUP_GPU);

#define GETSAFERHISHADER_COMPUTE(Shader) \
	Shader.GetComputeShader()

enum class EProjectCubemapVariant
{
	EncodeCameraPosition,
	DecomposeCubemaps,
	DecomposeDepth
};

class FBaseProjectCubemapCS : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters &Parameters)
	{
		return Parameters.Platform == EShaderPlatform::SP_PCD3D_SM5;
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters &Parameters, FShaderCompilerEnvironment &OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH"), bWriteDepth ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH_LINEAR"), bWriteLinearDepth ? 1 : 0);
	}

	FBaseProjectCubemapCS() = default;
	FBaseProjectCubemapCS(const ShaderMetaType::CompiledShaderInitializerType &Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		RWInputCubeAsArray.Bind(Initializer.ParameterMap, TEXT("InputCubeAsArray"));
		InputBlockCullFlagStructBuffer.Bind(Initializer.ParameterMap, TEXT("CullFlags"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputColorTexture.Bind(Initializer.ParameterMap, TEXT("OutputColorTexture"));
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		BlocksPerFaceAcross.Bind(Initializer.ParameterMap, TEXT("BlocksPerFaceAcross"));
		CubemapCameraPositionMetres.Bind(Initializer.ParameterMap, TEXT("CubemapCameraPositionMetres"));
	}
	static const uint32 kThreadGroupSize = 16;
	static const bool bWriteDepth = true;
	static const bool bWriteLinearDepth = true;

protected:
	DECLARE_INLINE_TYPE_LAYOUT(FBaseProjectCubemapCS, NonVirtual);
	LAYOUT_FIELD(FShaderResourceParameter, InputCubeMap);
	LAYOUT_FIELD(FRWShaderParameter, RWInputCubeAsArray);
	LAYOUT_FIELD(FShaderResourceParameter, InputBlockCullFlagStructBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, DefaultSampler);
	LAYOUT_FIELD(FRWShaderParameter, OutputColorTexture);
	LAYOUT_FIELD(FShaderParameter, CubemapCameraPositionMetres);
	LAYOUT_FIELD(FShaderParameter, Offset);
	LAYOUT_FIELD(FShaderParameter, BlocksPerFaceAcross);
};

template<EProjectCubemapVariant Variant>
class FProjectCubemapCS : public FBaseProjectCubemapCS
{ 
	DECLARE_SHADER_TYPE(FProjectCubemapCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return Parameters.Platform == EShaderPlatform::SP_PCD3D_SM5;
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH"), bWriteDepth ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH_LINEAR"), bWriteLinearDepth ? 1 : 0);
	}

	FProjectCubemapCS() = default;
	FProjectCubemapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseProjectCubemapCS(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		RWInputCubeAsArray.Bind(Initializer.ParameterMap, TEXT("InputCubeAsArray"));
		InputBlockCullFlagStructBuffer.Bind(Initializer.ParameterMap, TEXT("CullFlags"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputColorTexture.Bind(Initializer.ParameterMap, TEXT("OutputColorTexture"));
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		BlocksPerFaceAcross.Bind(Initializer.ParameterMap, TEXT("BlocksPerFaceAcross"));
		CubemapCameraPositionMetres.Bind(Initializer.ParameterMap, TEXT("CubemapCameraPositionMetres"));
	}
	 
	void SetInputsAndOutputs(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FUnorderedAccessViewRHIRef InputCubeMapTextureUAVRef,
		FShaderResourceViewRHIRef InputBlockCullFlagSRVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef)
	{
		auto *ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
		TShaderMapRef<FProjectCubemapCS<Variant>> smr(ShaderMap);
		FRHIComputeShader *ShaderRHI = smr.GetComputeShader();
		if (bDecomposeCubemaps)
		{
			RWInputCubeAsArray.SetTexture(RHICmdList, ShaderRHI, InputCubeMapTextureRef, InputCubeMapTextureUAVRef);
			check(RWInputCubeAsArray.IsUAVBound());
			SetSRVParameter(RHICmdList, ShaderRHI, InputBlockCullFlagStructBuffer, InputBlockCullFlagSRVRef);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		}
		OutputColorTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FIntPoint& InOffset,
		const FVector& InCubemapCameraPositionMetres,
		uint32 InBlocksPerFaceAcross)
	{
		auto *ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
		TShaderMapRef<FProjectCubemapCS<Variant>> smr(ShaderMap);
		FRHIComputeShader *ShaderRHI = smr.GetComputeShader();
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
		FVector3f campos = {(float)InCubemapCameraPositionMetres.X, (float)InCubemapCameraPositionMetres.Y, (float)InCubemapCameraPositionMetres.Z};
		SetShaderValue(RHICmdList, ShaderRHI, CubemapCameraPositionMetres, campos);
		SetShaderValue(RHICmdList, ShaderRHI, BlocksPerFaceAcross, InBlocksPerFaceAcross);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		auto *ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
		TShaderMapRef<FProjectCubemapCS<Variant>> smr(ShaderMap);
		FRHIComputeShader *ShaderRHI = smr.GetComputeShader();
		OutputColorTexture.UnsetUAV(RHICmdList, ShaderRHI);
		RWInputCubeAsArray.UnsetUAV(RHICmdList, ShaderRHI);
	}
	/*
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << RWInputCubeAsArray;
		Ar << InputBlockCullFlagStructBuffer;
		Ar << DefaultSampler;
		Ar << OutputColorTexture;
		Ar << Offset;
		Ar << BlocksPerFaceAcross;
		Ar << CubemapCameraPositionMetres;
		return bShaderHasOutdatedParameters;
	}*/
	static const bool bDecomposeCubemaps = (Variant == EProjectCubemapVariant::DecomposeCubemaps||Variant==EProjectCubemapVariant::DecomposeDepth);
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::EncodeCameraPosition>, TEXT("/Plugin/Teleport/Private/ProjectCubemap.usf"), TEXT("EncodeCameraPositionCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::DecomposeCubemaps>, TEXT("/Plugin/Teleport/Private/ProjectCubemap.usf"), TEXT("DecomposeCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::DecomposeDepth>, TEXT("/Plugin/Teleport/Private/ProjectCubemap.usf"), TEXT("DecomposeDepthCS"), SF_Compute)

static inline FVector2D CreateWorldZToDeviceZTransform(float FOV)
{
	FMatrix ProjectionMatrix;
	if(static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, FOV, 1.0f, 1.0f, GNearClippingPlane, GNearClippingPlane);
	}
	else
	{
		ProjectionMatrix = FPerspectiveMatrix(FOV, FOV, 1.0f, 1.0f, GNearClippingPlane, GNearClippingPlane);
	}

	// Based on CreateInvDeviceZToWorldZTransform() in Runtime\Engine\Private\SceneView.cpp.
	float DepthMul = ProjectionMatrix.M[2][2];
	float DepthAdd = ProjectionMatrix.M[3][2];

	if(DepthAdd == 0.0f)
	{
		DepthAdd = 0.00000001f;
	}

	float SubtractValue = DepthMul / DepthAdd;
	SubtractValue -= 0.00000001f;

	return FVector2D{1.0f / DepthAdd, SubtractValue};
}

void FEncodePipelineMonoscopic::Initialise(avs::uid id,const FUnrealCasterEncoderSettings& InSettings, teleport::server::ClientNetworkContext* context, ATeleportMonitor* InMonitor)
{
	clientId=id;
	ClientNetworkContext = context;
	Settings = InSettings;
	Monitor = InMonitor;
	WorldZToDeviceZTransform = CreateWorldZToDeviceZTransform(FMath::DegreesToRadians(90.0f));

	ENQUEUE_RENDER_COMMAND(TeleportInitializeEncodePipeline)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Initialize_RenderThread(RHICmdList);
		}
	);
}

void FEncodePipelineMonoscopic::Release()
{
	ENQUEUE_RENDER_COMMAND(TeleportReleaseEncodePipeline)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Release_RenderThread(RHICmdList);
		}
	);
	FlushRenderingCommands();
}

void FEncodePipelineMonoscopic::CullHiddenCubeSegments(FSceneInterface* InScene, teleport::server::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor)
{
	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(TeleportCullHiddenCubeSegments)(
		[this, CameraInfo, FeatureLevel, FaceSize, Divisor](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, TeleportEncodePipelineCullHiddenCubeSegments);
			CullHiddenCubeSegments_RenderThread(RHICmdList, FeatureLevel, CameraInfo, FaceSize, Divisor);
		}
	);
}

void FEncodePipelineMonoscopic::PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags)
{
	if (!InScene || !InSourceTexture)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(TeleportPrepareFrame)(
		[this, CameraTransform, BlockIntersectionFlags, TargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, TeleportEncodePipelineMonoscopicPrepare);
			PrepareFrame_RenderThread(RHICmdList, TargetResource, FeatureLevel, CameraTransform.GetTranslation(), BlockIntersectionFlags);
		}
	);
}


void FEncodePipelineMonoscopic::EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR)
{
	if(!InScene || !InSourceTexture)
	{
		return;
	}
	// only proceed if network is ready to stream.
	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(TeleportEncodeFrame)(
		[this, CameraTransform, forceIDR](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, TeleportEncodePipelineMonoscopic);
			EncodeFrame_RenderThread(RHICmdList, CameraTransform, forceIDR);
		}
	);
}
	
void FEncodePipelineMonoscopic::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	FTeleportRHI RHI(RHICmdList);
	FTeleportRHI::EDeviceType DeviceType;
	void* DeviceHandle = RHI.GetNativeDevice(DeviceType);

	teleport::server::GraphicsDeviceType CasterDeviceType;
	avs::SurfaceBackendInterface* avsSurfaceBackends[2] = { nullptr };

	EPixelFormat PixelFormat;
	if (Monitor->bUse10BitEncoding)
	{
		PixelFormat = EPixelFormat::PF_R16G16B16A16_UNORM;
	}
	else
	{
		PixelFormat = EPixelFormat::PF_R8G8B8A8;
	}

	switch(DeviceType)
	{
	case FTeleportRHI::EDeviceType::Direct3D11:
		CasterDeviceType = teleport::server::GraphicsDeviceType::Direct3D11;
		break;
	case FTeleportRHI::EDeviceType::Direct3D12:
		CasterDeviceType = teleport::server::GraphicsDeviceType::Direct3D12;
		break;
	case FTeleportRHI::EDeviceType::OpenGL:
		CasterDeviceType = teleport::server::GraphicsDeviceType::OpenGL;
		break;
	default:
		UE_LOG(LogTeleport, Error, TEXT("Failed to obtain native device handle"));
		return; 
	} 
	// Roderick: we create a DOUBLE-HEIGHT texture, and encode colour in the top half, depth in the bottom.
	int32 streamWidth;
	int32  streamHeight;
	if (Settings.bDecomposeCube)
	{
		streamWidth = Settings.FrameWidth;
		streamHeight = Settings.FrameHeight;
	}
	else
	{
		streamWidth = std::max<int32>(Settings.FrameWidth, Settings.DepthWidth);
		streamHeight = Settings.FrameHeight + Settings.DepthHeight;
	}
	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(streamWidth, streamHeight, PixelFormat);
	D3D12_RESOURCE_DESC desc = ((ID3D12Resource*)ColorSurfaceTexture.Texture->GetNativeResource())->GetDesc();

	if(ColorSurfaceTexture.Texture.IsValid())
	{
		ColorSurfaceTexture.UAV = RHI.CreateSurfaceUAV(ColorSurfaceTexture.Texture);
	}
	else
	{
		UE_LOG(LogTeleport, Error, TEXT("Failed to create encoder color input surface texture"));
		return;
	}

	Pipeline.Reset(new teleport::server::VideoEncodePipeline);
	
	auto ServerSettings = Settings.GetAsCasterEncoderSettings();
	teleport::server::VideoEncodeParams params;
	params.encodeWidth = ServerSettings.frameWidth;
	params.encodeHeight = ServerSettings.frameHeight;
	params.deviceHandle = DeviceHandle;
	params.deviceType = CasterDeviceType;
	params.inputSurfaceResource = ColorSurfaceTexture.Texture->GetNativeResource();

	
	auto &cm = teleport::server::ClientManager::instance();
	std::shared_ptr<teleport::server::ClientData> clientData = cm.GetClient(clientId);
	auto &np = clientData->clientMessaging->getClientNetworkContext()->NetworkPipeline;
	Pipeline->initialize(*Monitor->GetServerSettings(), params, &(np.ColorQueue), &np.TagDataQueue);
}
	
void FEncodePipelineMonoscopic::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Pipeline.Reset();

	ColorSurfaceTexture.Texture.SafeRelease();
	ColorSurfaceTexture.UAV.SafeRelease();

	DepthSurfaceTexture.Texture.SafeRelease();
	DepthSurfaceTexture.UAV.SafeRelease();
} 

void FEncodePipelineMonoscopic::CullHiddenCubeSegments_RenderThread(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, teleport::server::CameraInfo CameraInfo, int32 FaceSize, uint32 Divisor)
{
	// Aidan: Currently not going to do this on GPU so this function is unused
	// We will do this on cpu on game thread instead because we can share the output with the capture component to cull faces from rendering.
}

void FEncodePipelineMonoscopic::PrepareFrame_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* TargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FVector CameraPosition,
	TArray<bool> BlockIntersectionFlags)
{
	if (!UnorderedAccessViewRHIRef || !UnorderedAccessViewRHIRef->IsValid() || TargetResource->TextureRHI != SourceCubemapRHI)
	{
		UnorderedAccessViewRHIRef = RHICreateUnorderedAccessView(TargetResource->TextureRHI, 0);
		SourceCubemapRHI = TargetResource->TextureRHI;
	}
	{
		if (Settings.bDecomposeCube)
		{
			DispatchDecomposeCubemapShader(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel, CameraPosition, BlockIntersectionFlags);
		}
	}
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform, bool forceIDR)
{
	check(Pipeline.IsValid());

	// The transform of the capture component needs to be sent with the image
	FVector t = CameraTransform.GetTranslation()*0.01f;
	FQuat r = CameraTransform.GetRotation();
	const FVector s = CameraTransform.GetScale3D();
	avs::Transform CamTransform; 
	CamTransform.position = {(float)t.X, (float)t.Y, (float)t.Z};
	CamTransform.rotation = {(float)r.X, (float)r.Y, (float)r.Z, (float)r.W};
	CamTransform.scale = {(float)s.X, (float)s.Y, (float)s.Z};

	avs::ConvertTransform(avs::AxesStandard::UnrealStyle, ClientNetworkContext->axesStandard, CamTransform);
	// TODO: extra data...
	teleport::server::Result result = Pipeline->process(nullptr,0, forceIDR);
	if (!result)
	{
		UE_LOG(LogTeleport, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}

template<typename ShaderType>
void FEncodePipelineMonoscopic::DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	const uint32 NumThreadGroupsX = Settings.FrameWidth / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Settings.FrameHeight / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsZ = Settings.bDecomposeCube ? 6 : 1;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, TextureRHI, TextureUAVRHI,
		ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
		 FIntPoint(0, 0));
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);

	ComputeShader->UnsetParameters(RHICmdList);
}

void FEncodePipelineMonoscopic::DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI
	, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel
	,FVector CameraPosition, const TArray<bool>& BlockIntersectionFlags)
{
	FVector  t = CameraPosition *0.01f;
	vec3 pos_m ={(float)t.X,(float)t.Y,(float)t.Z};
	avs::ConvertPosition(avs::AxesStandard::UnrealStyle, ClientNetworkContext->axesStandard, pos_m);
	const FVector &CameraPositionMetres =*((const FVector*)&pos_m);
	FGlobalShaderMap *GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeCubemaps> ShaderType;
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeDepth> DepthShaderType;
	typedef FProjectCubemapCS<EProjectCubemapVariant::EncodeCameraPosition> EncodeCameraPositionShaderType;
	
	TResourceArray<FShaderFlag> BlockCullFlags;

	for (auto& Flag : BlockIntersectionFlags)
	{
		BlockCullFlags.Add({ Flag, 0, 0, 0 });
	}

	FRHIResourceCreateInfo CreateInfo(TEXT("BlockCullFlagSB"));
	CreateInfo.ResourceArray = &BlockCullFlags;

	FBufferRHIRef BlockCullFlagSB = RHICreateStructuredBuffer(
		sizeof(FShaderFlag),
		BlockCullFlags.Num() * sizeof(FShaderFlag),
		BUF_ShaderResource,
		CreateInfo
	);

	FShaderResourceViewRHIRef BlockCullFlagSRV = RHICreateShaderResourceView(BlockCullFlagSB);

	// This is the number of segments each cube face is split into for culling
	uint32 BlocksPerFaceAcross = (uint32)FMath::Sqrt(float(BlockCullFlags.Num())/6.0f);

	int W = SourceCubemapRHI->GetSizeXYZ().X;
	{
		const uint32 NumThreadGroupsX = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;

		TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
		ComputeShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		ComputeShader->SetParameters(RHICmdList, FIntPoint(0, 0), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(ComputeShader));
		DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		ComputeShader->UnsetParameters(RHICmdList);
	}

	{
		const uint32 NumThreadGroupsX = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;
		TShaderMapRef<DepthShaderType> DepthShader(GlobalShaderMap);
		DepthShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		DepthShader->SetParameters(RHICmdList, FIntPoint(0, W * 2), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(DepthShader));
		DispatchComputeShader(RHICmdList, DepthShader.GetShader(), NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		DepthShader->UnsetParameters(RHICmdList);
	}

	{
		const uint32 NumThreadGroupsX = 4;
		const uint32 NumThreadGroupsY = 1;
		const uint32 NumThreadGroupsZ = 1;
		TShaderMapRef<EncodeCameraPositionShaderType> EncodePosShader(GlobalShaderMap);
		EncodePosShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		EncodePosShader->SetParameters(RHICmdList, FIntPoint(W*3-(32*4), W * 3-(3*8)), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(EncodePosShader));
		DispatchComputeShader(RHICmdList, EncodePosShader.GetShader(), NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		EncodePosShader->UnsetParameters(RHICmdList);
	}
	
}