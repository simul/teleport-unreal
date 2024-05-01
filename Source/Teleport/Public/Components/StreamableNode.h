// Copyright 2018-2024 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "TeleportSignalingService.h"
#include "StreamableNode.generated.h"

class APawn;
class APlayerController;
class UTexture;
class UTexture2D;
class UMaterialInterface;
class ULightComponent;
enum EMaterialProperty;
namespace ERHIFeatureLevel
{
	enum Type;
}
namespace EMaterialQualityLevel
{
	 enum Type: uint8; 
}
namespace avs
{
	typedef uint64_t uid;
}
USTRUCT(BlueprintType)
struct FUid
{
	GENERATED_BODY()
	avs::uid Value;
	FUid(avs::uid u=0):Value(u){}
};
// This is attached to each streamable actor.
// The mesh may be shared.
UCLASS(BlueprintType)
class TELEPORT_API UStreamableNode:public UObject
{
	GENERATED_BODY()
		USceneComponent *sceneComponent=nullptr;
	public:
	UStreamableNode();
	UStreamableNode(USceneComponent *s);
	virtual ~UStreamableNode();
	virtual void BeginPlay();

	UFUNCTION(BlueprintCallable,Category=Teleport)
	USceneComponent *GetSceneComponent()
	{
		return sceneComponent;
	}
	void SetSceneComponent(USceneComponent *s);
	//Returns the amount of materials used by this actor.
	UFUNCTION(BlueprintCallable,Category=Teleport)
	int32 GetNumMaterials();

	UFUNCTION(BlueprintCallable,Category=Teleport)
	UStaticMeshComponent *GetMesh();
	//Returns the interface of the material used by the mesh.
	UFUNCTION(BlueprintCallable,Category=Teleport)
	UMaterialInterface* GetMaterial(int32 materialIndex);
	//Returns all textures used in the material.
	UFUNCTION(BlueprintCallable,Category=Teleport)
	TArray<UTexture*> GetUsedTextures();
	//Returns all textures from the property chain.
	//	materialProperty : Which property chain we are pulling the textures from.
	UFUNCTION(BlueprintCallable,Category=Teleport)
	TArray<UTexture*> GetTextureChain(EMaterialProperty materialProperty);
	//Returns the first texture from the property chain.
	//	materialProperty : Which property chain we are pulling the texture from.
	UFUNCTION(BlueprintCallable,Category=Teleport)
	UTexture* GetTexture(EMaterialProperty materialProperty);

	UFUNCTION(BlueprintCallable,Category=Teleport)
	ULightComponent* GetLightComponent();

	UFUNCTION(BlueprintCallable,Category=Teleport)
	FUid GetUid() const
	{
		return FUid(uid);
	}
	UFUNCTION(BlueprintCallable,Category=Teleport)
	FString GetUidString() const;
	/// Call only from GeometrySource.
	void SetUid(avs::uid u)
	{
		uid=u;
	}
private:
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent;
	TWeakObjectPtr<ULightComponent> LightComponent;
	//TArray<TWeakObjectPtr<UTexture2D>> LightAndShadowMaps;

	EMaterialQualityLevel::Type textureQualityLevel; //Quality level to retrieve the textures.
	ERHIFeatureLevel::Type textureFeatureLevel; //Feature level to retrieve the textures.
	avs::uid uid = 0;
};
