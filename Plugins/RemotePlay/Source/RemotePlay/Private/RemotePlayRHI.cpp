// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayRHI.h"
#include "RemotePlayModule.h"

#include "RenderUtils.h"
#include "dxgiformat.h"

FRemotePlayRHI::FRemotePlayRHI(FRHICommandListImmediate& InRHICmdList)
	: RHICmdList(InRHICmdList)
{}

void* FRemotePlayRHI::GetNativeDevice(EDeviceType& OutType) const
{
	FString rhiName = GDynamicRHI->GetName();

	if (rhiName == ("D3D11"))
	{
		OutType = EDeviceType::Direct3D11;
	}
	else if (rhiName == ("D3D12"))
	{
		OutType = EDeviceType::Direct3D12;
	}
	else if (rhiName == ("Vulkan"))
	{
		OutType = EDeviceType::Vulkan;
	}
	else
	{
		OutType = EDeviceType::Invalid; 
		UE_LOG(LogRemotePlay, Error, TEXT("Unsupported RHI shader platform!"));
		return nullptr;
	}
	check(GDynamicRHI);
	return GDynamicRHI->RHIGetNativeDevice();
}
	
FTexture2DRHIRef FRemotePlayRHI::CreateSurfaceTexture(uint32 Width, uint32 Height, EPixelFormat PixelFormat) const
{
	FTexture2DRHIRef SurfaceRHI;
	FRHIResourceCreateInfo CreateInfo;

	EDeviceType DevType;
	GetNativeDevice(DevType);

	uint32 TexFlags = TexCreate_UAV | TexCreate_RenderTargetable;
	if (DevType == EDeviceType::Direct3D12)
	{
		TexFlags |= TexCreate_SharedHeap;
	}
	
	FPixelFormatInfo& FormatInfo = GPixelFormats[PixelFormat];
	const uint32 OldPlatformFormat = FormatInfo.PlatformFormat;

	// HACK: NVENC requires typed D3D11 texture, we're forcing RGBA_UNORM!
	if(PixelFormat == EPixelFormat::PF_R8G8B8A8)
	{
		FormatInfo.PlatformFormat = DXGI_FORMAT_R8G8B8A8_UNORM;	
	}
	
	SurfaceRHI = RHICmdList.CreateTexture2D(Width, Height, PixelFormat, 1, 1, TexFlags, CreateInfo);
	FormatInfo.PlatformFormat = OldPlatformFormat;
	

	return SurfaceRHI;
}
	
FUnorderedAccessViewRHIRef FRemotePlayRHI::CreateSurfaceUAV(FTexture2DRHIRef InTextureRHI) const
{
	return RHICmdList.CreateUnorderedAccessView(InTextureRHI, 0);
}
