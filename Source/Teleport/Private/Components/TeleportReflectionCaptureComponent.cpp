// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/TeleportReflectionCaptureComponent.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Renderer/Private/ScenePrivate.h"
#include "SceneManagement.h"
#include "EngineModule.h"
#include "TeleportRHI.h"
#include "PixelShaderUtils.h"
#include "Engine/TextureRenderTargetCube.h"
#include <algorithm>
#include "Pipelines/EncodePipelineInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "RHIResources.h"
#include "Components/ReflectionCaptureCacheFns.h"

#define GETSAFERHISHADER_COMPUTE(Shader) \
			Shader.GetComputeShader()

enum class EUpdateReflectionsVariant
{
	NoSource,
	UpdateSpecular,
	UpdateDiffuse,
	UpdateLighting,
	Mip,
	MipRough,
	WriteToStream
};

class FUpdateReflectionsBaseCS : public FGlobalShader
{
public:
	static const uint32 kThreadGroupSize = 16;
	FUpdateReflectionsBaseCS(const ShaderMetaType::CompiledShaderInitializerType &Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
	
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		RWOutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
		DirLightCount.Bind(Initializer.ParameterMap, TEXT("DirLightCount"));
		DirLightStructBuffer.Bind(Initializer.ParameterMap, TEXT("DirLights"));
		InputCubemapAsArrayTexture.Bind(Initializer.ParameterMap, TEXT("InputCubemapAsArrayTexture"));
		RWStreamOutputTexture.Bind(Initializer.ParameterMap, TEXT("StreamOutputTexture"));
		check(RWOutputTexture.IsUAVBound()|| RWStreamOutputTexture.IsUAVBound());
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		SourceSize.Bind(Initializer.ParameterMap, TEXT("SourceSize"));
		TargetSize.Bind(Initializer.ParameterMap, TEXT("TargetSize"));
		Roughness.Bind(Initializer.ParameterMap, TEXT("Roughness"));
		RandomSeed.Bind(Initializer.ParameterMap, TEXT("RandomSeed"));
	}
	FUpdateReflectionsBaseCS() = default;
	void SetInputs(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputCubeMapTextureRef,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef = FShaderResourceViewRHIRef(nullptr, false),
		int32 InDirLightCount = 0)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();

		if (InputCubeMapTextureRef)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
			//check(InputCubeMap.IsBound());
		}
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InputCubeMapTextureRef->GetSize());

		if (DirLightsShaderResourceViewRef)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
			SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
		}
	}
	void SetInputs(
		FRHICommandList& RHICmdList,
		FShaderResourceViewRHIRef InputCubeMapSRVRef,
		int32 InSourceSize,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef=FShaderResourceViewRHIRef(nullptr,false),
		int32 InDirLightCount=0)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();

		if (InputCubeMapSRVRef)
		{
			SetSRVParameter(RHICmdList, ShaderRHI, InputCubeMap, InputCubeMapSRVRef);
			if (!InputCubeMap.IsBound())
			{
				check(InputCubeMap.IsBound());
			}
		}
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InSourceSize);

		if (DirLightsShaderResourceViewRef)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
			SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
		}
	}
	 
	void SetOutputs(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		int32 InTargetSize)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();
		//SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, TargetSize, InTargetSize);
	}


	void SetParameters(
		FRHICommandList& RHICmdList,
		FIntPoint InOffset,
		float InRoughness,
		uint32 InRandomSeed)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
		SetShaderValue(RHICmdList, ShaderRHI, Roughness, InRoughness);
		SetShaderValue(RHICmdList, ShaderRHI, RandomSeed, InRandomSeed);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();
		RWOutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
	}

	void SetStreamParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputColorTextureRef,
		FUnorderedAccessViewRHIRef InputColorTextureUAVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		const FIntPoint& InOffset)
	{
		FRHIComputeShader *ShaderRHI = RHICmdList.GetBoundComputeShader();
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, InputColorTextureRef, InputColorTextureUAVRef);
		RWStreamOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
	}
	/*
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << DefaultSampler;
		Ar << RWOutputTexture;
		Ar << InputCubemapAsArrayTexture;
		Ar << RWStreamOutputTexture;
		Ar << DirLightCount;
		Ar << DirLightStructBuffer;
		Ar << Offset;
		Ar << SourceSize;
		Ar << TargetSize;
		Ar << Roughness;
		Ar << RandomSeed;
		return bShaderHasOutdatedParameters;
	}*/
public:

struct DirectionalLight 
{
	vec4 Color;
	vec3 Direction;
};
DECLARE_INLINE_TYPE_LAYOUT(FUpdateReflectionsBaseCS, NonVirtual);
/*	SHADER_USE_PARAMETER_STRUCT(FUpdateReflectionsBaseCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCube, InputCubeMap)
	SHADER_PARAMETER_SAMPLER(SamplerState,DefaultSampler)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWOutputTexture)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCubeArray, InputCubemapAsArrayTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWStreamOutputTexture)
	SHADER_PARAMETER(uint32, DirLightCount)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<DirectionalLight>, DirLightStructBuffer)
	SHADER_PARAMETER(FVector2f, Offset)
	SHADER_PARAMETER(FVector2f, SourceSize)
	SHADER_PARAMETER(FVector2f, TargetSize)
	SHADER_PARAMETER(float, Roughness)
	SHADER_PARAMETER(int, RandomSeed)
	END_SHADER_PARAMETER_STRUCT()*/

	LAYOUT_FIELD(FShaderResourceParameter, InputCubeMap);
	LAYOUT_FIELD(FShaderResourceParameter, DefaultSampler);
	LAYOUT_FIELD(FRWShaderParameter ,RWOutputTexture);
	LAYOUT_FIELD(FShaderResourceParameter, InputCubemapAsArrayTexture);
	LAYOUT_FIELD(FRWShaderParameter, RWStreamOutputTexture);
	LAYOUT_FIELD(FShaderParameter, DirLightCount);
	LAYOUT_FIELD(FShaderResourceParameter, DirLightStructBuffer);
	LAYOUT_FIELD(FShaderParameter, Offset);
	LAYOUT_FIELD(FShaderParameter, SourceSize);
	LAYOUT_FIELD(FShaderParameter, TargetSize);
	LAYOUT_FIELD(FShaderParameter, Roughness);
	LAYOUT_FIELD(FShaderParameter, RandomSeed);
};

template<EUpdateReflectionsVariant Variant>
class FUpdateReflectionsCS : public FUpdateReflectionsBaseCS
{
	DECLARE_SHADER_TYPE(FUpdateReflectionsCS, Global);
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
		OutEnvironment.SetDefine(TEXT("MIP_ROUGH"), (Variant ==EUpdateReflectionsVariant::MipRough));
	}

	FUpdateReflectionsCS() = default;
	FUpdateReflectionsCS(const FUpdateReflectionsBaseCS::CompiledShaderInitializerType &Initializer)
		: FUpdateReflectionsBaseCS(Initializer)
	{
	}

};


IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::NoSource>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("NoSourceCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("UpdateSpecularCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("UpdateDiffuseCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("UpdateLightingCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::Mip>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("FromMipCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::MipRough>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("FromMipCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("WriteToStreamCS"), SF_Compute)

//IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsPS, TEXT("/Plugin/Teleport/Private/UpdateReflections.usf"), TEXT("UpdateReflectionsPS"), SF_Pixel);

#if WITH_EDITOR
int32 FindOrAllocateCubemapIndex(FScene* Scene, const UReflectionCaptureComponent* Component)
{
	int32 CubemapIndex = -1;

	// Try to find an existing capture index for this component
	const FCaptureComponentSceneState *CaptureSceneStatePtr = ((fReflectionCaptureCache *)&Scene->ReflectionSceneData.AllocatedReflectionCaptureState)->Find( Component);

	if (CaptureSceneStatePtr)
	{
		CubemapIndex = CaptureSceneStatePtr->CubemapIndex;
	}
	else
	{
		// Reuse a freed index if possible
		CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.FindAndSetFirstZeroBit();
		if (CubemapIndex == INDEX_NONE)
		{
			// If we didn't find a free index, allocate a new one from the CubemapArraySlotsUsed bitfield
			CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.Num();
			if (CubemapIndex >= Scene->ReflectionSceneData.CubemapArray.GetMaxCubemaps())
				return -1;
			Scene->ReflectionSceneData.CubemapArraySlotsUsed.Add(true);
		}
		fReflectionCaptureCache *rcc = (fReflectionCaptureCache *)&Scene->ReflectionSceneData.AllocatedReflectionCaptureState;
		rcc->Add(Component, FCaptureComponentSceneState(CubemapIndex));
		Scene->ReflectionSceneData.AllocatedReflectionCaptureStateHasChanged = true;

		check(CubemapIndex < GMaxNumReflectionCaptures);
	}

	check(CubemapIndex >= 0);
	return CubemapIndex;
}
#endif
UTeleportReflectionCaptureComponent::UTeleportReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAttached = false;
	BoxTransitionDistance = 100;
	Mobility = EComponentMobility::Movable;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	
	specularOffset	=FIntPoint(0, 0);
	diffuseOffset	= specularOffset + FIntPoint(specularSize*3/2, specularSize*2);
	roughOffset	 = FIntPoint(3 * specularSize, 0);
	lightOffset = diffuseOffset + FIntPoint(specularSize * 3 / 2, specularSize * 2);
}

void UTeleportReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewCaptureBox)
	{
		PreviewCaptureBox->InitBoxExtent(((GetComponentTransform().GetScale3D() - FVector(BoxTransitionDistance)) / GetComponentTransform().GetScale3D()).ComponentMax(FVector::ZeroVector));
	}

	// Instead of Super::UpdatePreviewShape(), which is not exported:

	if (CaptureOffsetComponent)
	{
		CaptureOffsetComponent->SetRelativeLocation_Direct(CaptureOffset / GetComponentTransform().GetScale3D());
		if (CaptureOffset.IsNearlyZero())
		{
			CaptureOffsetComponent->SetVisibility(false);
		}
		else
		{
			CaptureOffsetComponent->SetVisibility(true);
		}
	}
}

float UTeleportReflectionCaptureComponent::GetInfluenceBoundingRadius() const
{
	return (GetComponentTransform().GetScale3D() + FVector(BoxTransitionDistance)).Size();
}

void UTeleportReflectionCaptureComponent::Init(FRHICommandListImmediate& RHICmdList,FCubeTexture &t, int32 size, int32 NumMips)
{
	FRHITextureCreateDesc CreateCubeDesc = FRHITextureCreateDesc::CreateCube(TEXT("ReflectionCapture"), size, PF_FloatRGBA);
	t.TextureCubeRHIRef = GDynamicRHI->RHICreateTexture_RenderThread(RHICmdList, CreateCubeDesc);
	
	for (int i = 0; i < NumMips; i++)
	{
		t.UnorderedAccessViewRHIRefs[i] = RHICreateUnorderedAccessView(t.TextureCubeRHIRef, i);
	}
	for (int i = 0; i < NumMips; i++)
	{
		t.TextureCubeMipRHIRefs[i] = RHICreateShaderResourceView(t.TextureCubeRHIRef, i);
	}
}

void UTeleportReflectionCaptureComponent::Release(FCubeTexture &t)
{
	const uint32_t NumMips = t.TextureCubeRHIRef->GetNumMips();
	for (uint32_t i = 0; i < NumMips; i++)
	{
		if (t.UnorderedAccessViewRHIRefs[i])
			t.UnorderedAccessViewRHIRefs[i]->Release();
		if (t.TextureCubeMipRHIRefs[i])
			t.TextureCubeMipRHIRefs[i]->Release();
	}
	if (t.TextureCubeRHIRef)
		t.TextureCubeRHIRef->Release();
}

void UTeleportReflectionCaptureComponent::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Init(RHICmdList, SpecularCubeTexture, specularSize, 3);
	Init(RHICmdList, RoughSpecularCubeTexture, specularSize, 3);
	Init(RHICmdList,DiffuseCubeTexture,diffuseSize, 1);
	Init(RHICmdList,LightingCubeTexture,lightSize, 1);
}

void UTeleportReflectionCaptureComponent::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Release(SpecularCubeTexture);
	Release(RoughSpecularCubeTexture);
	Release(DiffuseCubeTexture);
	Release(LightingCubeTexture);
}

static float RoughnessFromMip(float mip, float numMips)
{
	static float  roughness_mip_scale = 1.2f;
	return exp2((3.0f + mip - numMips) / roughness_mip_scale);
}


void UTeleportReflectionCaptureComponent::UpdateReflections_RenderThread(
	FRHICommandListImmediate& RHICmdList, FScene *Scene,
	UTextureRenderTargetCube *InSourceTexture,
	ERHIFeatureLevel::Type FeatureLevel)
{
	if (!SpecularCubeTexture.TextureCubeRHIRef || !DiffuseCubeTexture.TextureCubeRHIRef || !LightingCubeTexture.TextureCubeRHIRef)
		Initialize_RenderThread(RHICmdList);
	FTextureRenderTargetCubeResource* SourceCubeResource = nullptr;
	if(InSourceTexture)
		SourceCubeResource =static_cast<FTextureRenderTargetCubeResource*>(InSourceTexture->GetRenderTargetResource());

	const int32 SourceSize = SourceCubeResource->GetSizeX();


	int32 CaptureIndex = 0;
	FTextureRHIRef TargetResource;
	FIntPoint Offset0(0, 0);
	if (OverrideTexture)
	{
		TargetResource = OverrideTexture->GetRenderTargetResource()->TextureRHI;
	}
	else
	{
//		CaptureIndex = FindOrAllocateCubemapIndex(Scene, this);
		if (CaptureIndex>=0&&Scene&&Scene->ReflectionSceneData.CubemapArray.GetCubemapSize())
		{
			TRefCountPtr<IPooledRenderTarget> rt = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();
			
			if (rt.IsValid())
				TargetResource=rt->GetRHI();
		}
	}
	randomSeed++;
/*	if (Scene->ReflectionSceneData.CubemapArray.IsValid() &&
		Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().IsValid())
	{
		CubemapArray = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}
	else
	{
		return;
	}*/
	//DispatchUpdateReflectionsShader<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>>(
	//	RHICmdList, InSourceTexture->TextureReference.TextureReferenceRHI.GetReference()->GetTextureReference(), Target_UAV,FeatureLevel);

	//FSceneRenderTargetItem& DestCube = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();


	SCOPED_DRAW_EVENT(RHICmdList, UpdateReflections);

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	// Specular Reflections
	{
		typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular> ShaderType;

		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular>> CopyCubemapShader(ShaderMap);
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::Mip>> MipShader(ShaderMap);
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::MipRough>> MipRoughShader(ShaderMap);
		FUpdateReflectionsBaseCS *s = MipShader.GetShader();
		FUpdateReflectionsBaseCS *r=MipRoughShader.GetShader();
		FRHIComputeShader *rhiShaderS = MipShader.GetComputeShader();
		FRHIComputeShader *rhiShaderR = MipRoughShader.GetComputeShader();
		// The 0 mip is copied directly from the source cubemap,
		{
			const int32 MipSize = specularSize;
			CopyCubemapShader->SetInputs(RHICmdList,SourceCubeResource->GetTextureRHI());
			CopyCubemapShader->SetOutputs(RHICmdList,
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[0],MipSize
				);
			CopyCubemapShader->SetParameters(RHICmdList, Offset0, 0.f, randomSeed);
			
			SetComputePipelineState(RHICmdList, CopyCubemapShader.GetComputeShader());
			uint32 NumThreadGroupsXY = (specularSize +1) / ShaderType::kThreadGroupSize;
			DispatchComputeShader(RHICmdList, CopyCubemapShader.GetShader(), NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			CopyCubemapShader->UnsetParameters(RHICmdList);
		}
		// The other mips are generated
		int32 MipSize = specularSize;
		int32 PrevMipSize = specularSize;
		uint32_t NumMips = SpecularCubeTexture.TextureCubeRHIRef->GetNumMips();
		for (uint32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			float roughness = RoughnessFromMip((float)MipIndex, (float)(2* NumMips));
			FUpdateReflectionsBaseCS *Shader=static_cast<FUpdateReflectionsBaseCS *>((roughness < 0.99f) ? s:r);
			FRHIComputeShader *rhiShader=((roughness<0.99f)?rhiShaderS:rhiShaderR);
			PrevMipSize = MipSize;
			MipSize = (MipSize + 1) / 2;
			// Apparently Unreal can't cope with copying from one mip to another in the same texture. So we use the original cube:
			Shader->SetInputs(RHICmdList,
				SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList,
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex],MipSize
			);
			Shader->SetParameters(RHICmdList, Offset0, roughness, randomSeed);
			SetComputePipelineState(RHICmdList, rhiShader);
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			Shader->UnsetParameters(RHICmdList);
		}
		MipSize = specularSize;
		PrevMipSize = specularSize;
		for (uint32 MipIndex = 0; MipIndex < RoughSpecularCubeTexture.TextureCubeRHIRef->GetNumMips(); MipIndex++)
		{
			float roughness = RoughnessFromMip(float(NumMips+MipIndex), (float)(2 * NumMips));
			FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>((roughness < 0.99f) ? s : r);
			FRHIComputeShader *rhiShader = ((roughness < 0.99f) ? rhiShaderS : rhiShaderR);
			Shader->SetInputs(RHICmdList,SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList,RoughSpecularCubeTexture.TextureCubeRHIRef, RoughSpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
			Shader->SetParameters(RHICmdList, Offset0, roughness, randomSeed);
			SetComputePipelineState(RHICmdList, rhiShader);
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			Shader->UnsetParameters(RHICmdList);
			PrevMipSize = MipSize;
			MipSize = (MipSize + 1) / 2;
		}
	}

	// Diffuse Reflections
	{
		typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse> ShaderType;
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse>> ComputeShader(ShaderMap);
		FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>(ComputeShader.GetShader());
		uint32_t NumMips = DiffuseCubeTexture.TextureCubeRHIRef->GetNumMips();
		int32 MipSize = DiffuseCubeTexture.TextureCubeRHIRef->GetSize();
		for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			Shader->SetInputs(RHICmdList, SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList, DiffuseCubeTexture.TextureCubeRHIRef, DiffuseCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
			Shader->SetParameters(RHICmdList, Offset0, 1.0, randomSeed);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(ComputeShader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			ComputeShader->UnsetParameters(RHICmdList);
			MipSize = (MipSize + 1) / 2;
		}
		if (TargetResource)
		{
			for (uint32 MipIndex = 0; MipIndex < std::min(NumMips, (uint32)TargetResource->GetNumMips()); MipIndex++)
			{
				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					FRHICopyTextureInfo CopyTextureInfo;
					CopyTextureInfo.SourceSliceIndex = CubeFace;
					CopyTextureInfo.DestSliceIndex =CubeFace+( CaptureIndex >= 0 ? CaptureIndex : 0);
					CopyTextureInfo.SourceMipIndex =CopyTextureInfo.DestMipIndex= MipIndex;
					RHICmdList.CopyTexture(SpecularCubeTexture.TextureCubeRHIRef
						, TargetResource,CopyTextureInfo);
				}
			}
		}
	}

	// Lighting
	{
		TResourceArray<FShaderDirectionalLight> ShaderDirLights;

		for (auto& LightInfo : Scene->Lights)
		{
			if (LightInfo.LightType == LightType_Directional && LightInfo.LightSceneInfo->bVisible)
			{
				// We could update this later to only send dynamic lights if we want
				FShaderDirectionalLight ShaderDirLight;
				// The color includes the intensity. Divide by max intensity of 20
				ShaderDirLight.Color = LightInfo.Color * 0.05f; 
				ShaderDirLight.Direction = LightInfo.LightSceneInfo->Proxy->GetDirection();
				ShaderDirLights.Emplace(MoveTemp(ShaderDirLight));
			}
		}
		if (ShaderDirLights.Num())
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("ShaderDirLights"));
			CreateInfo.ResourceArray = &ShaderDirLights;

			FBufferRHIRef DirLightSB = RHICreateStructuredBuffer(
				sizeof(FShaderDirectionalLight),
				ShaderDirLights.Num() * sizeof(FShaderDirectionalLight),
				BUF_ShaderResource,
				CreateInfo
			);

			FShaderResourceViewRHIRef DirLightSRV = RHICreateShaderResourceView(DirLightSB);

			typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting> ShaderType;
			TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting>> ComputeShader(ShaderMap);
			FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>(ComputeShader.GetShader());
			int32 MipSize = lightSize;
			uint32_t NumMips = LightingCubeTexture.TextureCubeRHIRef->GetNumMips();
			for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				int32 PrevMipSize = MipSize;
				MipSize = (MipSize + 1) / 2;
				float roughness = RoughnessFromMip((float)MipIndex, (float)NumMips);
				Shader->SetInputs(RHICmdList, SourceCubeResource->GetTextureRHI(), DirLightSRV, ShaderDirLights.Num());
				Shader->SetOutputs(RHICmdList, LightingCubeTexture.TextureCubeRHIRef, LightingCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
				Shader->SetParameters(RHICmdList, Offset0, 1.0, randomSeed);
				SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(ComputeShader));
				uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
				DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
				ComputeShader->UnsetParameters(RHICmdList);
			}

			//Release the resources
			DirLightSRV->Release();
			DirLightSB->Release();
		}
	}
}

// write the reflections to the UAV of the output video stream.
void Decompose_RenderThread(FRHICommandListImmediate& RHICmdList
	, FCubeTexture &CubeTexture, FSurfaceTexture *TargetSurfaceTexture, TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>> &Shader, FIntPoint TargetOffset)
{
	auto *ComputeShader = (FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream> *)Shader.GetComputeShader(); // Shader;
	const int32 EffectiveTopMipSize = CubeTexture.TextureCubeRHIRef->GetSizeXYZ().X;
	const int32 NumMips = CubeTexture.TextureCubeRHIRef->GetNumMips();
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	int32 MipSize = EffectiveTopMipSize;
	// 2 * W for the colour cube two face height 
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		ComputeShader->SetStreamParameters(RHICmdList,
			CubeTexture.TextureCubeRHIRef,
			CubeTexture.UnorderedAccessViewRHIRefs[MipIndex],
			TargetSurfaceTexture->Texture,
			TargetSurfaceTexture->UAV,
			TargetOffset);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
		uint32 NumThreadGroupsXY = MipSize > ComputeShader->kThreadGroupSize ? MipSize / ComputeShader->kThreadGroupSize : 1;
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
		ComputeShader->UnsetParameters(RHICmdList);
		TargetOffset.Y += (MipSize * 2);
		MipSize = (MipSize + 1) / 2;
	}
}
// write the reflections to the UAV of the output video stream.
void UTeleportReflectionCaptureComponent::WriteReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel
	,FIntPoint StartOffset)
{
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream> ShaderType;

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>> ComputeShader(ShaderMap);
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

//	FShader *Shader = ComputeShader.operator*();
	Decompose_RenderThread(RHICmdList, DiffuseCubeTexture, TargetSurfaceTexture, ComputeShader, StartOffset + diffuseOffset);
	Decompose_RenderThread(RHICmdList, SpecularCubeTexture, TargetSurfaceTexture, ComputeShader, StartOffset + specularOffset);
	Decompose_RenderThread(RHICmdList, RoughSpecularCubeTexture, TargetSurfaceTexture, ComputeShader, StartOffset + roughOffset);
	Decompose_RenderThread(RHICmdList, LightingCubeTexture, TargetSurfaceTexture, ComputeShader, StartOffset + lightOffset);
}

void UTeleportReflectionCaptureComponent::Initialise()
{
	bAttached = false;
	ENQUEUE_RENDER_COMMAND(UTeleportReflectionCaptureComponentInitialize)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Initialize_RenderThread(RHICmdList);
		}
	);
}

void UTeleportReflectionCaptureComponent::UpdateContents(FScene *Scene,UTextureRenderTargetCube *InSourceTexture, ERHIFeatureLevel::Type FeatureLevel)
{
	ENQUEUE_RENDER_COMMAND(TeleportCopyReflections)(
		[this, Scene, InSourceTexture, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, TeleportReflectionCaptureComponent);
			UpdateReflections_RenderThread(RHICmdList, Scene, InSourceTexture, FeatureLevel);
		}
	);
}

void UTeleportReflectionCaptureComponent::PrepareFrame(FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel, FIntPoint StartOffset)
{
	ENQUEUE_RENDER_COMMAND(TeleportWriteReflectionsToSurface)(
		[this, Scene, TargetSurfaceTexture, FeatureLevel, StartOffset](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, TeleportReflectionCaptureComponent);
			WriteReflections_RenderThread(RHICmdList, Scene, TargetSurfaceTexture, FeatureLevel, StartOffset);
		}
	);
}

void UTeleportReflectionCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
}

#if WITH_EDITOR
// Redefine UE's internal, unexported functions:

void UReflectionCaptureComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Gets overwritten with saved value (if being loaded from disk)
	FPlatformMisc::CreateGuid(MapBuildDataId);
#if WITH_EDITOR
	bMapBuildDataIdLoaded=false;
#endif

	if(!HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdateForLoad.AddUnique(this);
		bNeedsRecaptureOrUpload=true;
	}
}

void UReflectionCaptureComponent::PropagateLightingScenarioChange()
{
	const FSceneInterface* Scene=GetWorld()->Scene;
	const EShaderPlatform  ShaderPlatform=Scene?Scene->GetShaderPlatform():GMaxRHIShaderPlatform;
	const bool bEncodedDataRequired=false;//IsEncodedHDRCubemapTextureRequired(ShaderPlatform);

	if(bEncodedDataRequired&&EncodedHDRCubemapTexture==nullptr)
	{
		ReregisterComponent();
	}
	else
	{
		// GetMapBuildData has changed, re-upload
		MarkDirtyForRecaptureOrUpload();
	}
}

void UReflectionCaptureComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	UpdatePreviewShape();

	if(ShouldComponentAddToScene()&&ShouldRender())
	{
		GetWorld()->Scene->AddReflectionCapture(this);
	}
}
void UReflectionCaptureComponent::OnRegister()
{
	const FSceneInterface* Scene=GetWorld()->Scene;
	const EShaderPlatform  ShaderPlatform=Scene?Scene->GetShaderPlatform():GMaxRHIShaderPlatform;
	const bool bEncodedHDRCubemapTextureRequired=false;//IsEncodedHDRCubemapTextureRequired(ShaderPlatform);

	{
		// SM5 doesn't require cached values
		SafeReleaseEncodedHDRCubemapTexture();
		CachedAverageBrightness=0;
	}

	Super::OnRegister();
}


void UReflectionCaptureComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	GetWorld()->Scene->RemoveReflectionCapture(this);
}

void UReflectionCaptureComponent::SendRenderTransform_Concurrent()
{
	// Don't update the transform of a component that needs to be recaptured,
	// Otherwise the RT will get the new transform one frame before the capture
	if(!bNeedsRecaptureOrUpload)
	{
		UpdatePreviewShape();

		if(ShouldComponentAddToScene()&&ShouldRender())
		{
			GetWorld()->Scene->UpdateReflectionCaptureTransform(this);
		}
	}

	Super::SendRenderTransform_Concurrent();
}

void UReflectionCaptureComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting,bool bTranslationOnly)
{
	// Save the static mesh state for transactions, force it to be marked dirty if we are going to discard any static lighting data.
	Modify(true);

	Super::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting,bTranslationOnly);

	MapBuildDataId=FGuid::NewGuid();

	MarkRenderStateDirty();
}

#if WITH_EDITOR
bool UReflectionCaptureComponent::CanEditChange(const FProperty* Property) const
{
	bool bCanEditChange=Super::CanEditChange(Property);

	if(Property->GetFName()==GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent,Cubemap)||
		Property->GetFName()==GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent,SourceCubemapAngle))
	{
		bCanEditChange&=ReflectionSourceType==EReflectionSourceType::SpecifiedCubemap;
	}

	return bCanEditChange;
}

void UReflectionCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if(PropertyChangedEvent.GetPropertyName()==GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent,Cubemap)||
		PropertyChangedEvent.GetPropertyName()==GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent,SourceCubemapAngle)||
		PropertyChangedEvent.GetPropertyName()==GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent,ReflectionSourceType))
	{
		MarkDirtyForRecapture();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR


void UReflectionCaptureComponent::BeginDestroy()
{
	// Deregister the component from the update queue
	if(bNeedsRecaptureOrUpload)
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdate.Remove(this);
		ReflectionCapturesToUpdateForLoad.Remove(this);
	}

	// Have to do this because we can't use GetWorld in BeginDestroy
	for(TSet<FSceneInterface*>::TConstIterator SceneIt(GetRendererModule().GetAllocatedScenes()); SceneIt; ++SceneIt)
	{
		FSceneInterface* Scene=*SceneIt;
		Scene->ReleaseReflectionCubemap(this);
	}

	if(EncodedHDRCubemapTexture)
	{
	//	BeginReleaseResource(EncodedHDRCubemapTexture);
	}

	// Begin a fence to track the progress of the above BeginReleaseResource being completed on the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool UReflectionCaptureComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy()&&ReleaseResourcesFence.IsFenceComplete();
}

void UReflectionCaptureComponent::FinishDestroy()
{
	if(EncodedHDRCubemapTexture)
	{
//		delete EncodedHDRCubemapTexture;
		EncodedHDRCubemapTexture=nullptr;
	}

	Super::FinishDestroy();
}
#include "UObject/ReflectionCaptureObjectVersion.h"
#include "Engine/MapBuildDataRegistry.h"
void UReflectionCaptureComponent::SerializeLegacyData(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FReflectionCaptureObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	if(Ar.CustomVer(FReflectionCaptureObjectVersion::GUID)<FReflectionCaptureObjectVersion::MoveReflectionCaptureDataToMapBuildData)
	{
		if(Ar.UEVer()>=VER_UE4_REFLECTION_CAPTURE_COOKING)
		{
			bool bLegacy=false;
			Ar<<bLegacy;
		}

		if(Ar.UEVer()>=VER_UE4_REFLECTION_DATA_IN_PACKAGES)
		{
			FGuid SavedVersion;
			Ar<<SavedVersion;

			float AverageBrightness=1.0f;

			if(Ar.CustomVer(FRenderingObjectVersion::GUID)>=FRenderingObjectVersion::ReflectionCapturesStoreAverageBrightness)
			{
				Ar<<AverageBrightness;
			}

			int32 EndOffset=0;
			Ar<<EndOffset;

			FGuid LegacyReflectionCaptureVer(0x0c669396,0x9cb849ae,0x9f4120ff,0x5812f4d3);

			if(SavedVersion!=LegacyReflectionCaptureVer)
			{
				// Guid version of saved source data doesn't match latest, skip the data
				// The skipping is done so we don't have to maintain legacy serialization code paths when changing the format
				Ar.Seek(EndOffset);
			}
			else
			{
				bool bValid=false;
				Ar<<bValid;

				if(bValid)
				{
					FReflectionCaptureMapBuildData* LegacyMapBuildData=new FReflectionCaptureMapBuildData();

					if(Ar.CustomVer(FRenderingObjectVersion::GUID)>=FRenderingObjectVersion::CustomReflectionCaptureResolutionSupport)
					{
						Ar<<LegacyMapBuildData->CubemapSize;
					}
					else
					{
						LegacyMapBuildData->CubemapSize=128;
					}

					{
						TArray<uint8> CompressedCapturedData;
						Ar<<CompressedCapturedData;

						check(CompressedCapturedData.Num()>0);
						FMemoryReader MemoryAr(CompressedCapturedData);

						int32 UncompressedSize;
						MemoryAr<<UncompressedSize;

						int32 CompressedSize;
						MemoryAr<<CompressedSize;

						LegacyMapBuildData->FullHDRCapturedData.Empty(UncompressedSize);
						LegacyMapBuildData->FullHDRCapturedData.AddUninitialized(UncompressedSize);

						const uint8* SourceData=&CompressedCapturedData[MemoryAr.Tell()];
						verify(FCompression::UncompressMemory(NAME_Zlib,LegacyMapBuildData->FullHDRCapturedData.GetData(),UncompressedSize,SourceData,CompressedSize));
					}

					LegacyMapBuildData->AverageBrightness=AverageBrightness;

					FReflectionCaptureMapBuildLegacyData LegacyComponentData;
					LegacyComponentData.Id=MapBuildDataId;
					LegacyComponentData.MapBuildData=LegacyMapBuildData;
					GReflectionCapturesWithLegacyBuildData.AddAnnotation(this,MoveTemp(LegacyComponentData));
				}
			}
		}
	}
}

void UReflectionCaptureComponent::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UReflectionCaptureComponent::Serialize"),STAT_ReflectionCaptureComponent_Serialize,STATGROUP_LoadTime);

#if WITH_EDITOR
	FGuid OldMapBuildDataId=MapBuildDataId;
#endif

	Super::Serialize(Ar);

	SerializeLegacyData(Ar);

#if WITH_EDITOR
	// Check to see if we overwrote the MapBuildDataId with a loaded one
	if(Ar.IsLoading())
	{
		bMapBuildDataIdLoaded=OldMapBuildDataId!=MapBuildDataId;
	}
	else
	{
		// If we're cooking, display a deterministic cook warning if we didn't overwrite the generated GUID at load time
//		UE_CLOG(Ar.IsCooking()&&!GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn)&&!bMapBuildDataIdLoaded,LogReflectionCaptureComponent,Warning,TEXT("%s contains a legacy UReflectionCaptureComponent and is being non-deterministically cooked - please resave the asset and recook."),*GetOutermost()->GetName());
	}
#endif
}

#if WITH_EDITOR
void UReflectionCaptureComponent::PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel)
{
	if(SupportsTextureCubeArray(PendingFeatureLevel))
	{
		SafeReleaseEncodedHDRCubemapTexture();

		MarkDirtyForRecaptureOrUpload();
	}
}
#endif // WITH_EDITOR

void UReflectionCaptureComponent::SafeReleaseEncodedHDRCubemapTexture()
{
	//if(EncodedHDRCubemapTexture)
	//{
	//	BeginReleaseResource(EncodedHDRCubemapTexture);
	//	ENQUEUE_RENDER_COMMAND(DeleteEncodedHDRCubemapTexture)(
	//		[ParamPointerToRelease=EncodedHDRCubemapTexture](FRHICommandListImmediate& RHICmdList)
	//		{
//	//			delete ParamPointerToRelease;
	//		});
	//	EncodedHDRCubemapTexture=nullptr;
	//
}


TArray<UReflectionCaptureComponent*> UReflectionCaptureComponent::ReflectionCapturesToUpdate;
TArray<UReflectionCaptureComponent*> UReflectionCaptureComponent::ReflectionCapturesToUpdateForLoad;
FCriticalSection UReflectionCaptureComponent::ReflectionCapturesToUpdateForLoadLock;

#endif