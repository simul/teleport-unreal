// Copyright 2018-2024 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

class FTeleportRHI
{
public:
	FTeleportRHI(FRHICommandListImmediate& InRHICmdList);

	enum class EDeviceType
	{
		Invalid,
		Direct3D11,
		Direct3D12,
		OpenGL,
		Vulkan,
	};
	void* GetNativeDevice(EDeviceType& OutType) const;

	FTexture2DRHIRef CreateSurfaceTexture(uint32 Width, uint32 Height, EPixelFormat PixelFormat) const;
	FUnorderedAccessViewRHIRef CreateSurfaceUAV(FTexture2DRHIRef InTextureRHI) const;

private:
	FRHICommandListImmediate& RHICmdList;
};