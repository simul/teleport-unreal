// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/StreamableNode.h"
#include "TeleportModule.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "GeometrySource.h"
#include "Engine/StaticMesh.h"

#include "Engine/Classes/Materials/Material.h"


UStreamableNode::UStreamableNode()
	:textureQualityLevel(EMaterialQualityLevel::High),
	textureFeatureLevel(ERHIFeatureLevel::SM5)
{
	BeginPlay();
}

UStreamableNode::UStreamableNode(USceneComponent *s)
	:textureQualityLevel(EMaterialQualityLevel::High),
	textureFeatureLevel(ERHIFeatureLevel::SM5)
{
	SetSceneComponent(s);
	BeginPlay();
}

UStreamableNode::~UStreamableNode()
{
}

void UStreamableNode::BeginPlay()
{
}

void UStreamableNode::SetSceneComponent(USceneComponent *s)
{
	sceneComponent=s;
	if(sceneComponent)
	{
		UObject::Rename(*MakeUniqueObjectName(GetOuter(),GetClass(),*(s->GetFName().ToString()+"_node")).ToString());
		GeometrySource *geometrySource=ITeleport::Get().GetGeometrySource();
		Actor=Cast<AActor>(sceneComponent->GetOuter());
		StaticMeshComponent=Cast<UStaticMeshComponent>(sceneComponent);
		LightComponent=Cast<ULightComponent>(sceneComponent);
	}
	else
	{
		Actor=nullptr;
		StaticMeshComponent=nullptr;
		LightComponent=nullptr;
	}
}
	

int32 UStreamableNode::GetNumMaterials()
{
	if(!StaticMeshComponent.Get())
		return 0;
	return StaticMeshComponent->GetNumMaterials();
}

UStaticMeshComponent *UStreamableNode::GetMesh()
{
	if(!StaticMeshComponent.Get())
		return 0;
	return StaticMeshComponent.Get();
}

UMaterialInterface * UStreamableNode::GetMaterial(int32 materialIndex)
{
	if(!StaticMeshComponent.Get())
		return 0;
	return StaticMeshComponent->GetMaterial(materialIndex);
}

TArray<UTexture*> UStreamableNode::GetUsedTextures()
{
	TArray<UTexture*> usedTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		UMaterial* material = matInterface->GetMaterial();

		if(material)
		{
			material->GetUsedTextures(usedTextures, textureQualityLevel, false, textureFeatureLevel, false);
		}
	}

	return usedTextures;
}

FString UStreamableNode::GetUidString() const
{
	FString ret=FString::Format(TEXT("{0}"),{uid});
	return ret;
}

TArray<UTexture*> UStreamableNode::GetTextureChain(EMaterialProperty materialProperty)
{
	TArray<UTexture*> outTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		matInterface->GetTexturesInPropertyChain(materialProperty, outTextures, nullptr, nullptr);
	}
	
	TArray<UTexture*> uniqueTextures;

	//Remove duplicates by moving unique instances to an array.
	while(outTextures.Num() != 0)
	{
		UTexture *uniqueTexture = outTextures[0];

		uniqueTextures.Add(uniqueTexture);
		//Removes all matching instances.
		outTextures.Remove(uniqueTexture);
	}

	return uniqueTextures;
}

UTexture * UStreamableNode::GetTexture(EMaterialProperty materialProperty)
{
	TArray<UTexture*> outTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		matInterface->GetTexturesInPropertyChain(materialProperty, outTextures, nullptr, nullptr);
	}

	UTexture * texture = nullptr;

	//Discarding duplicates by assuming we are using only one texture for the property chain.
	if(outTextures.Num() != 0)
	{
		texture = outTextures[0];
	}

	return texture;
}

ULightComponent* UStreamableNode::GetLightComponent()
{
	return LightComponent.Get();
}
/*
TArray<UTexture2D*> UStreamableNode::GetLightAndShadowMaps()
{
	UWorld* world = Actor->GetWorld();
	ULevel* level = world->GetCurrentLevel();
	world->GetLightMapsAndShadowMaps(level, LightAndShadowMaps);

	return LightAndShadowMaps;
}*/