// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "TeleportRHI.h"
#include "TeleportModule.h"
#include "RenderUtils.h"
#include "Windows/PreWindowsApi.h"
#include "dxgiformat.h"
#include "Windows/PostWindowsApi.h"

FTeleportRHI::FTeleportRHI(FRHICommandListImmediate& InRHICmdList)
	: RHICmdList(InRHICmdList)
{}

void* FTeleportRHI::GetNativeDevice(EDeviceType& OutType) const
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
		UE_LOG(LogTeleport, Error, TEXT("Unsupported RHI shader platform!"));
		return nullptr;
	}
	check(GDynamicRHI);
	return GDynamicRHI->RHIGetNativeDevice();
}
	
FTexture2DRHIRef FTeleportRHI::CreateSurfaceTexture(uint32 Width, uint32 Height, EPixelFormat PixelFormat) const
{
	FTexture2DRHIRef SurfaceRHI;
	FRHIResourceCreateInfo CreateInfo(TEXT("SurfaceRHI"));

	EDeviceType DevType;
	GetNativeDevice(DevType);

	uint32 TexFlags = (uint32)(TexCreate_UAV | TexCreate_RenderTargetable);
	if (DevType == EDeviceType::Direct3D12)
	{
		TexFlags |= (uint32)TexCreate_Shared;
	}
	
	FPixelFormatInfo& FormatInfo = GPixelFormats[PixelFormat];
	const uint32 OldPlatformFormat = FormatInfo.PlatformFormat;

	// HACK: NVENC requires typed D3D11 texture, we're forcing RGBA_UNORM!
	if(PixelFormat == EPixelFormat::PF_R8G8B8A8)
	{
		FormatInfo.PlatformFormat = DXGI_FORMAT_R8G8B8A8_UNORM;	
	}
	
	SurfaceRHI = RHICreateTexture2D(Width, Height, PixelFormat, 1, 1, (ETextureCreateFlags)TexFlags, CreateInfo);
	FormatInfo.PlatformFormat = OldPlatformFormat;
	

	return SurfaceRHI;
}
	
FUnorderedAccessViewRHIRef FTeleportRHI::CreateSurfaceUAV(FTexture2DRHIRef InTextureRHI) const
{
	return RHICmdList.CreateUnorderedAccessView(InTextureRHI, 0,0,1);
}
