#include "Components/TeleportBaseCaptureComponent.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/ReflectionCaptureObjectVersion.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Actor.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "Misc/ScopeLock.h"
#include "Components/BillboardComponent.h"
#include "Engine/CollisionProfile.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "SceneManagement.h"
#include "Engine/ReflectionCapture.h"
#include "EngineModule.h"
#include "ShaderCompiler.h"
#include "UObject/RenderingObjectVersion.h"
#include "Engine/SphereReflectionCapture.h"
#include "Components/DrawSphereComponent.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/SkyLightComponent.h"
#include "ProfilingDebugging/CookStats.h"
#include "Engine/MapBuildDataRegistry.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Engine/TextureCube.h"
#include "Math/PackedVector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TeleportBaseCaptureComponent)

#if WITH_EDITOR
#include "Factories/TextureFactory.h"
#include "TextureCompiler.h"
#endif

static int32 SanitizeReflectionCaptureSize(int32 ReflectionCaptureSize)
{
	const int32 MaxReflectionCaptureSize = GetMaxCubeTextureDimension();
	const int32 MinReflectionCaptureSize = 1;

	return FMath::Clamp(ReflectionCaptureSize, MinReflectionCaptureSize, MaxReflectionCaptureSize);
}

class FTeleportReflectionTextureCubeResource : public FTexture
{
public:
	FTeleportReflectionTextureCubeResource() : Size(0),
									   NumMips(0),
									   Format(PF_Unknown)
	{
	}

	void SetupParameters(int32 InSize, int32 InNumMips, EPixelFormat InFormat, TArray<uint8> &&InSourceData)
	{
		Size = InSize;
		NumMips = InNumMips;
		Format = InFormat;
		SourceData = MoveTemp(InSourceData);
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::CreateCube(TEXT("ReflectionTextureCube"), Size, Format)
				.SetNumMips(NumMips)
				.SetFlags(ETextureCreateFlags::ShaderResource);

		TextureCubeRHI = RHICreateTexture(Desc);
		TextureRHI = TextureCubeRHI;

		if (SourceData.Num())
		{
			const int32 BlockBytes = GPixelFormats[Format].BlockBytes;
			int32 MipBaseIndex = 0;

			for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				const int32 MipSize = 1 << (NumMips - MipIndex - 1);
				const int32 CubeFaceBytes = MipSize * MipSize * BlockBytes;

				uint32 SrcPitch = MipSize * BlockBytes;

				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					uint32 DestPitch = 0;
					uint8 *DestBuffer = (uint8 *)RHILockTextureCubeFace(TextureCubeRHI, CubeFace, 0, MipIndex, RLM_WriteOnly, DestPitch, false);

					const int32 SourceIndex = MipBaseIndex + CubeFace * CubeFaceBytes;
					const uint8 *SourceBuffer = &SourceData[SourceIndex];

					if (SrcPitch == DestPitch)
					{
						FMemory::Memcpy(DestBuffer, SourceBuffer, CubeFaceBytes);
					}
					else
					{
						// Copy data, taking the stride into account!
						uint8 *Src = (uint8 *)SourceBuffer;
						uint8 *Dst = (uint8 *)DestBuffer;
						for (int32 Row = 0; Row < MipSize; ++Row)
						{
							FMemory::Memcpy(Dst, Src, SrcPitch);
							Src += SrcPitch;
							Dst += DestPitch;
						}
						check((PTRINT(Src) - PTRINT(SourceBuffer)) == PTRINT(CubeFaceBytes));
					}

					RHIUnlockTextureCubeFace(TextureCubeRHI, CubeFace, 0, MipIndex, false);
				}

				MipBaseIndex += CubeFaceBytes * CubeFace_MAX;
			}

			SourceData.Empty();
		}

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer(
			SF_Trilinear,
			AM_Clamp,
			AM_Clamp,
			AM_Clamp);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		INC_MEMORY_STAT_BY(STAT_ReflectionCaptureTextureMemory, CalcTextureSize(Size, Size, Format, NumMips) * 6);
	}

	virtual void ReleaseRHI() override
	{
		DEC_MEMORY_STAT_BY(STAT_ReflectionCaptureTextureMemory, CalcTextureSize(Size, Size, Format, NumMips) * 6);
		TextureCubeRHI.SafeRelease();
		FTexture::ReleaseRHI();
	}

	virtual uint32 GetSizeX() const override
	{
		return Size;
	}

	virtual uint32 GetSizeY() const override //-V524
	{
		return Size;
	}

	FRHITexture *GetTextureRHI()
	{
		return TextureCubeRHI;
	}

private:
	int32 Size;
	int32 NumMips;
	EPixelFormat Format;
	FTextureCubeRHIRef TextureCubeRHI;

	TArray<uint8> SourceData;
};

int32 UTeleportBaseCaptureComponent::GetReflectionCaptureSize()
{
	return 128;
}

FReflectionCaptureMapBuildData* UTeleportBaseCaptureComponent::GetMapBuildData() const
{
	AActor* Owner = GetOwner();

	if (Owner)
	{
		ULevel* OwnerLevel = Owner->GetLevel();

		if (OwnerLevel && OwnerLevel->OwningWorld)
		{
			ULevel* ActiveLightingScenario = OwnerLevel->OwningWorld->GetActiveLightingScenario();
			UMapBuildDataRegistry* MapBuildData = NULL;

			if (ActiveLightingScenario && ActiveLightingScenario->MapBuildData)
			{
				MapBuildData = ActiveLightingScenario->MapBuildData;
			}
			else if (OwnerLevel->MapBuildData)
			{
				MapBuildData = OwnerLevel->MapBuildData;
			}			
			else if (OwnerLevel->OwningWorld->IsPartitionedWorld())
			{
				// Fallback to PersistentLevel in case of missing data in OwnerLevel
				// This is quite likely for WP maps in PIE/engine until the ReflectionsCaptures
				// are updated to distribute themselves in the cells (planned for 5.1)
				MapBuildData = OwnerLevel->OwningWorld->PersistentLevel->MapBuildData;
			}
			 
			if (MapBuildData)
			{
				FReflectionCaptureMapBuildData* ReflectionBuildData = MapBuildData->GetReflectionCaptureBuildData(MapBuildDataId);

				if (ReflectionBuildData && (ReflectionBuildData->CubemapSize == UTeleportBaseCaptureComponent::GetReflectionCaptureSize() || ReflectionBuildData->HasBeenUploadedFinal()))
				{
					return ReflectionBuildData;
				}
			}
		}
	}
	
	return NULL;
}

void UTeleportBaseCaptureComponent::PropagateLightingScenarioChange()
{
	// GetMapBuildData has changed, re-upload
	MarkDirtyForRecaptureOrUpload();
}

TArray<UTeleportBaseCaptureComponent*> UTeleportBaseCaptureComponent::ReflectionCapturesToUpdate;
TArray<UTeleportBaseCaptureComponent*> UTeleportBaseCaptureComponent::ReflectionCapturesToUpdateForLoad;
FCriticalSection UTeleportBaseCaptureComponent::ReflectionCapturesToUpdateForLoadLock;

UTeleportBaseCaptureComponent::UTeleportBaseCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Brightness = 1;
	// Shouldn't be able to change reflection captures at runtime
	Mobility = EComponentMobility::Static;
	EncodedHDRCubemapTexture = nullptr;
	CachedAverageBrightness = 1.0f;
	bNeedsRecaptureOrUpload = false;
}

void UTeleportBaseCaptureComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	UpdatePreviewShape();

	if (ShouldComponentAddToScene() && ShouldRender())
	{
		//GetWorld()->Scene->AddReflectionCapture(this);
	}
}

void UTeleportBaseCaptureComponent::SendRenderTransform_Concurrent()
{	
	// Don't update the transform of a component that needs to be recaptured,
	// Otherwise the RT will get the new transform one frame before the capture
	if (!bNeedsRecaptureOrUpload)
	{
		UpdatePreviewShape();

		if (ShouldComponentAddToScene() && ShouldRender())
		{
			//GetWorld()->Scene->UpdateReflectionCaptureTransform(this);
		}
	}

	Super::SendRenderTransform_Concurrent();
}

void UTeleportBaseCaptureComponent::SafeReleaseEncodedHDRCubemapTexture()
{
	if (EncodedHDRCubemapTexture)
	{
		BeginReleaseResource(EncodedHDRCubemapTexture);
		ENQUEUE_RENDER_COMMAND(DeleteEncodedHDRCubemapTexture)(
			[ParamPointerToRelease = EncodedHDRCubemapTexture](FRHICommandListImmediate& RHICmdList)
			{
				delete ParamPointerToRelease;
			});
		EncodedHDRCubemapTexture = nullptr;
	}
}

void UTeleportBaseCaptureComponent::OnRegister()
{
	const bool bEncodedHDRCubemapTextureRequired = (GIsEditor || GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1) 
		// mobile forward renderer or translucensy in mobile deferred need encoded reflection texture when clustered reflections disabled
		&& !MobileForwardEnableClusteredReflections(GMaxRHIShaderPlatform);

	if (bEncodedHDRCubemapTextureRequired)
	{
		FReflectionCaptureMapBuildData* MapBuildData = GetMapBuildData();

		// If the MapBuildData is valid, update it. If it is not we will use the cached values, if there are any
		if (MapBuildData)
		{
			if (!EncodedHDRCubemapTexture)
			{
				EncodedHDRCubemapTexture = new FTeleportReflectionTextureCubeResource();
				TArray<uint8> EncodedHDRCapturedData;
				EPixelFormat EncodedHDRCubemapTextureFormat = PF_Unknown;
				if (IsMobileDeferredShadingEnabled(GMaxRHIShaderPlatform))
				{
					// make a copy, FullHDR data will still be needed later
					EncodedHDRCapturedData = MapBuildData->FullHDRCapturedData;
					EncodedHDRCubemapTextureFormat = PF_FloatRGBA;
				}
				else
				{
					if (GIsEditor)
					{ 
						EncodedHDRCapturedData = MapBuildData->EncodedHDRCapturedData;
					}
					else
					{
						// EncodedHDR data is no longer needed
						Swap(EncodedHDRCapturedData, MapBuildData->EncodedHDRCapturedData);
					}
					EncodedHDRCubemapTextureFormat = PF_FloatR11G11B10;
				}

				EncodedHDRCubemapTexture->SetupParameters(MapBuildData->CubemapSize, FMath::CeilLogTwo(MapBuildData->CubemapSize) + 1, EncodedHDRCubemapTextureFormat, MoveTemp(EncodedHDRCapturedData));
				BeginInitResource(EncodedHDRCubemapTexture);
			}

			CachedAverageBrightness = MapBuildData->AverageBrightness;
		}
	}
	else
	{
		// SM5 doesn't require cached values
		SafeReleaseEncodedHDRCubemapTexture();
		CachedAverageBrightness = 0;
	}

	Super::OnRegister();
}

void UTeleportBaseCaptureComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
//	GetWorld()->Scene->RemoveReflectionCapture(this);
}

void UTeleportBaseCaptureComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
{
	// Save the static mesh state for transactions, force it to be marked dirty if we are going to discard any static lighting data.
	Modify(true);

	Super::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting, bTranslationOnly);

	MapBuildDataId = FGuid::NewGuid();

	MarkRenderStateDirty();
}

void UTeleportBaseCaptureComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Gets overwritten with saved value (if being loaded from disk)
	FPlatformMisc::CreateGuid(MapBuildDataId);
#if WITH_EDITOR
	bMapBuildDataIdLoaded = false;
#endif

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdateForLoad.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UTeleportBaseCaptureComponent::SerializeLegacyData(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FReflectionCaptureObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	if (Ar.CustomVer(FReflectionCaptureObjectVersion::GUID) < FReflectionCaptureObjectVersion::MoveReflectionCaptureDataToMapBuildData)
	{
		if (Ar.UEVer() >= VER_UE4_REFLECTION_CAPTURE_COOKING)
		{
			bool bLegacy = false;
			Ar << bLegacy;
		}

		if (Ar.UEVer() >= VER_UE4_REFLECTION_DATA_IN_PACKAGES)
		{
			FGuid SavedVersion;
			Ar << SavedVersion;

			float AverageBrightness = 1.0f;

			if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::ReflectionCapturesStoreAverageBrightness)
			{
				Ar << AverageBrightness;
			}

			int32 EndOffset = 0;
			Ar << EndOffset;

			FGuid LegacyReflectionCaptureVer(0x0c669396, 0x9cb849ae, 0x9f4120ff, 0x5812f4d3);

			if (SavedVersion != LegacyReflectionCaptureVer)
			{
				// Guid version of saved source data doesn't match latest, skip the data
				// The skipping is done so we don't have to maintain legacy serialization code paths when changing the format
				Ar.Seek(EndOffset);
			}
			else
			{
				bool bValid = false;
				Ar << bValid;

				if (bValid)
				{
					FReflectionCaptureMapBuildData* LegacyMapBuildData = new FReflectionCaptureMapBuildData();

					if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::CustomReflectionCaptureResolutionSupport)
					{
						Ar << LegacyMapBuildData->CubemapSize;
					}
					else
					{
						LegacyMapBuildData->CubemapSize = 128;
					}

					{
						TArray<uint8> CompressedCapturedData;
						Ar << CompressedCapturedData;

						check(CompressedCapturedData.Num() > 0);
						FMemoryReader MemoryAr(CompressedCapturedData);

						int32 UncompressedSize;
						MemoryAr << UncompressedSize;

						int32 CompressedSize;
						MemoryAr << CompressedSize;

						LegacyMapBuildData->FullHDRCapturedData.Empty(UncompressedSize);
						LegacyMapBuildData->FullHDRCapturedData.AddUninitialized(UncompressedSize);

						const uint8* SourceData = &CompressedCapturedData[MemoryAr.Tell()];
//						verify(FCompression::UncompressMemory(NAME_Zlib, LegacyMapBuildData->FullHDRCapturedData.GetData(), UncompressedSize, SourceData, CompressedSize));
					}

					LegacyMapBuildData->AverageBrightness = AverageBrightness;

					FReflectionCaptureMapBuildLegacyData LegacyComponentData;
					LegacyComponentData.Id = MapBuildDataId;
					LegacyComponentData.MapBuildData = LegacyMapBuildData;
					GReflectionCapturesWithLegacyBuildData.AddAnnotation(this, MoveTemp(LegacyComponentData));
				}
			}
		}
	}
}

void UTeleportBaseCaptureComponent::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UTeleportBaseCaptureComponent::Serialize"), STAT_TeleportBaseCaptureComponent_Serialize, STATGROUP_LoadTime);

#if WITH_EDITOR
	FGuid OldMapBuildDataId = MapBuildDataId;
#endif

	Super::Serialize(Ar);

	SerializeLegacyData(Ar);

#if WITH_EDITOR
	// Check to see if we overwrote the MapBuildDataId with a loaded one
	if (Ar.IsLoading())
	{
		bMapBuildDataIdLoaded = OldMapBuildDataId != MapBuildDataId;
	}
#endif
}

FReflectionCaptureProxy* UTeleportBaseCaptureComponent::CreateSceneProxy()
{
	return new FReflectionCaptureProxy(nullptr);
}

void UTeleportBaseCaptureComponent::UpdatePreviewShape() 
{
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

#if WITH_EDITOR
bool UTeleportBaseCaptureComponent::CanEditChange(const FProperty* Property) const
{
	bool bCanEditChange = Super::CanEditChange(Property);


	return bCanEditChange;
}

void UTeleportBaseCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

void UTeleportBaseCaptureComponent::BeginDestroy()
{
	// Deregister the component from the update queue
	if (bNeedsRecaptureOrUpload)
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdate.Remove(this);
		ReflectionCapturesToUpdateForLoad.Remove(this);
	}

	// Have to do this because we can't use GetWorld in BeginDestroy
	for (TSet<FSceneInterface*>::TConstIterator SceneIt(GetRendererModule().GetAllocatedScenes()); SceneIt; ++SceneIt)
	{
		FSceneInterface* Scene = *SceneIt;
//		Scene->ReleaseReflectionCubemap(this);
	}

	if (EncodedHDRCubemapTexture)
	{
		BeginReleaseResource(EncodedHDRCubemapTexture);
	}

	// Begin a fence to track the progress of the above BeginReleaseResource being completed on the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool UTeleportBaseCaptureComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

void UTeleportBaseCaptureComponent::FinishDestroy()
{
	if (EncodedHDRCubemapTexture)
	{
		delete EncodedHDRCubemapTexture;
		EncodedHDRCubemapTexture = nullptr;
	}

	Super::FinishDestroy();
}

void UTeleportBaseCaptureComponent::MarkDirtyForRecaptureOrUpload() 
{ 
	if (GetVisibleFlag())
	{
		ReflectionCapturesToUpdate.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UTeleportBaseCaptureComponent::MarkDirtyForRecapture() 
{ 
	if (GetVisibleFlag())
	{
		MarkPackageDirty();
		MapBuildDataId = FGuid::NewGuid();
		ReflectionCapturesToUpdate.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UTeleportBaseCaptureComponent::UpdateReflectionCaptureContents(UWorld* WorldToUpdate, const TCHAR* CaptureReason, bool bVerifyOnlyCapturing, bool bCapturingForMobile, bool bInsideTick)
{
	if (WorldToUpdate && WorldToUpdate->Scene
		// Don't capture and read back capture contents if we are currently doing async shader compiling
		// This will keep the update requests in the queue until compiling finishes
		// Note: this will also prevent uploads of cubemaps from DDC, which is unintentional
		&& (GShaderCompilingManager == NULL || !GShaderCompilingManager->IsCompiling())
#if WITH_EDITOR
		// Prevent any reflection capture if textures are still compiling
		&& FTextureCompilingManager::Get().GetNumRemainingTextures() == 0
#endif
		)
	{
		//guarantee that all render proxies are up to date before kicking off this render
		WorldToUpdate->SendAllEndOfFrameUpdates();

		{
			for (FActorIterator It(WorldToUpdate); It; ++It)
			{
				TInlineComponentArray<UTeleportBaseCaptureComponent*> Components;
				(*It)->GetComponents(Components);
				for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
				{
					Components[ComponentIndex]->MarkDirtyForRecapture(); // Continuously refresh reflection captures
				}
			}
		}

		TArray<UTeleportBaseCaptureComponent*> WorldCombinedCaptures;

		for (int32 CaptureIndex = ReflectionCapturesToUpdate.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
		{
			UTeleportBaseCaptureComponent* CaptureComponent = ReflectionCapturesToUpdate[CaptureIndex];

			if (CaptureComponent->GetWorld() == WorldToUpdate)
			{
				WorldCombinedCaptures.Add(CaptureComponent);
				ReflectionCapturesToUpdate.RemoveAt(CaptureIndex);
			}
		}

		if (ReflectionCapturesToUpdateForLoad.Num() > 0)
		{
			FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
			for (int32 CaptureIndex = ReflectionCapturesToUpdateForLoad.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
			{
				UTeleportBaseCaptureComponent* CaptureComponent = ReflectionCapturesToUpdateForLoad[CaptureIndex];

				if (CaptureComponent->GetWorld() == WorldToUpdate)
				{
					WorldCombinedCaptures.Add(CaptureComponent);
					ReflectionCapturesToUpdateForLoad.RemoveAt(CaptureIndex);
				}
			}
		}

//		WorldToUpdate->Scene->AllocateReflectionCaptures(WorldCombinedCaptures, CaptureReason, bVerifyOnlyCapturing, bCapturingForMobile, bInsideTick);
	}
}

#if WITH_EDITOR
void UTeleportBaseCaptureComponent::PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel)
{
	if (SupportsTextureCubeArray(PendingFeatureLevel))
	{
		SafeReleaseEncodedHDRCubemapTexture();

		MarkDirtyForRecaptureOrUpload();
	}
}
#endif // WITH_EDITOR


