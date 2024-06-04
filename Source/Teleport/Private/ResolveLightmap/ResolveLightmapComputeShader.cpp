#include "ResolveLightmapComputeShader.h"
#include "PixelShaderUtils.h"
#include "MeshPassProcessor.inl"
#include "StaticMeshResources.h"
#include "DynamicMeshBuilder.h"
#include "RenderGraphResources.h"
#include "GlobalShader.h"
#include "UnifiedBuffer.h"
#include "CanvasTypes.h"
#include "MeshDrawShaderBindings.h"
#include "RHIGPUReadback.h"
#include "TeleportModule.h"
#include "MaterialShader.h"
#pragma optimize("",off)
DECLARE_STATS_GROUP(TEXT("ResolveLightmapComputeShader"), STATGROUP_ResolveLightmapComputeShader, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("ResolveLightmapComputeShader Execute"), STAT_ResolveLightmapComputeShader_Execute, STATGROUP_ResolveLightmapComputeShader);

// This class carries our parameter declarations and acts as the bridge between cpp and HLSL.
class TELEPORT_API FResolveLightmapComputeShader: public FGlobalShader
{
public:
	
	DECLARE_GLOBAL_SHADER(FResolveLightmapComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FResolveLightmapComputeShader, FGlobalShader);
	
	
	class FResolveLightmapComputeShader_Perm_TEST : SHADER_PERMUTATION_INT("TEST", 1);
	using FPermutationDomain = TShaderPermutationDomain<
		FResolveLightmapComputeShader_Perm_TEST
	>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		/*
		* Here's where you define one or more of the input parameters for your shader.
		* Some Eamples:
		*/
		// SHADER_PARAMETER(uint32, MyUint32) // On the shader side: uint32 MyUint32;

		SHADER_PARAMETER_TEXTURE(Texture2D,SourceLightmap) // On the shader side: Texture2D<float4> MyTexture; (float4 should be whatever you expect each pixel in the texture to be, in this case float4(R,G,B,A) for 4 channels)
		SHADER_PARAMETER(FVector4f,LightMapScale) // On the shader side: float4 MyVector;
		SHADER_PARAMETER(FVector4f,LightMapAdd) // On the shader side: float4 MyVector;
		// SHADER_PARAMETER_SAMPLER(SamplerState, MyTextureSampler) // On the shader side: SamplerState MySampler; // CPP side: TStaticSamplerState<ESamplerFilter::SF_Bilinear>::GetRHI();

		// SHADER_PARAMETER_ARRAY(float, MyFloatArray, [3]) // On the shader side: float MyFloatArray[3];

		// SHADER_PARAMETER_UAV(RWTexture2D<FVector4f>, MyTextureUAV) // On the shader side: RWTexture2D<float4> MyTextureUAV;
		// SHADER_PARAMETER_UAV(RWStructuredBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: RWStructuredBuffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_UAV(RWBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: RWBuffer<FMyCustomStruct> MyCustomStructs;

		// SHADER_PARAMETER_SRV(StructuredBuffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: StructuredBuffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_SRV(Buffer<FMyCustomStruct>, MyCustomStructs) // On the shader side: Buffer<FMyCustomStruct> MyCustomStructs;
		// SHADER_PARAMETER_SRV(Texture2D<FVector4f>, MyReadOnlyTexture) // On the shader side: Texture2D<float4> MyReadOnlyTexture;

		// SHADER_PARAMETER_STRUCT_REF(FMyCustomStruct, MyCustomStruct)

		
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RenderTarget)
		

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		/*
		* Here you define constants that can be used statically in the shader code.
		* ResolveLightmap:
		*/
		// OutEnvironment.SetDefine(TEXT("MY_CUSTOM_CONST"), TEXT("1"));

		/*
		* These defines are used in the thread count section of our shader
		*/
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_ResolveLightmapComputeShader_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_ResolveLightmapComputeShader_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_ResolveLightmapComputeShader_Z);

		// This shader must support typed UAV load and we are testing if it is supported at runtime using RHIIsTypedUAVLoadSupported
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}
private:
};

// This will tell the engine to create the shader and where the shader entry point is.
//                            ShaderType                            ShaderPath                     Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FResolveLightmapComputeShader, "/Plugin/Teleport/Private/ResolveLightmap.usf", "ResolveLightmapCS", SF_Compute);

void FResolveLightmapComputeShaderInterface::DispatchRenderThread(FRHICommandListImmediate& RHICmdList, FResolveLightmapComputeShaderDispatchParams Params) {
	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_ResolveLightmapComputeShader_Execute);
		DECLARE_GPU_STAT(ResolveLightmapComputeShader)
		RDG_EVENT_SCOPE(GraphBuilder, "ResolveLightmapComputeShader");
		RDG_GPU_STAT_SCOPE(GraphBuilder, ResolveLightmapComputeShader);
		
		typename FResolveLightmapComputeShader::FPermutationDomain PermutationVector;
		
		// Add any static permutation options here
		// PermutationVector.Set<FResolveLightmapComputeShader::FMyPermutationName>(12345);

		TShaderMapRef<FResolveLightmapComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		

		bool bIsShaderValid = ComputeShader.IsValid();

		if (bIsShaderValid) {
			FResolveLightmapComputeShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FResolveLightmapComputeShader::FParameters>();

			FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder, Params.TargetTexture->TextureRHI, TEXT("ResolveLightmapComputeShader_RT"));
			auto Fmt=TargetTexture->Desc.Format;
			FRDGTextureDesc Desc=FRDGTextureDesc::Create2D({(int)Params.TargetTexture->GetSizeX(),(int)Params.TargetTexture->GetSizeY()},Fmt, FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef TmpTexture = GraphBuilder.CreateTexture(Desc, TEXT("ResolveLightmapComputeShader_TempTexture"));
			PassParameters->RenderTarget = GraphBuilder.CreateUAV(TmpTexture);
			FRDGTextureRef SourceTexture=RegisterExternalTexture(GraphBuilder,Params.SourceTexture->TextureRHI,TEXT("ResolveLightmapComputeShader_Source"));
			FRDGTextureSRVDesc SrcDesc(SourceTexture);
			PassParameters->SourceLightmap=Params.SourceTexture->TextureRHI;//GraphBuilder.CreateSRV(SrcDesc);
			PassParameters->LightMapScale=Params.LightMapScale;
			PassParameters->LightMapAdd=Params.LightMapAdd;

			auto GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Params.X, Params.Y, Params.Z), FIntVector(NUM_THREADS_ResolveLightmapComputeShader_X,NUM_THREADS_ResolveLightmapComputeShader_Y,1));
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecuteResolveLightmapComputeShader"),
				PassParameters,
				ERDGPassFlags::AsyncCompute,
				[&PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});

			
			// The copy will fail if we don't have matching formats, let's check and make sure we do.
			if (TargetTexture->Desc.Format ==Fmt) {
				AddCopyTexturePass(GraphBuilder, TmpTexture, TargetTexture, FRHICopyTextureInfo());
			} else {
				UE_LOG(LogTeleport,Error,TEXT("The provided render target has an incompatible format (Please change the RT format)."));
			}
			
		} else {
			UE_LOG(LogTeleport,Error,TEXT("The compute shader has a problem)."));
			// We exit here as we don't want to crash the game if the shader is not found or has an error.
			
		}
	}

	GraphBuilder.Execute();
}