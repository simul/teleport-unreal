#pragma once

#include <map>
#include <unordered_map>

#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/PreWindowsApi.h"
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Components/LightComponent.h"
#include "Platform/CrossPlatform/Shaders/CppSl.sl"
#include "libavstream/common_maths.h"
#include "libavstream/geometry/mesh_interface.hpp"

/*! The Geometry Source keeps all the geometry ready for streaming, and returns geometry
	data in glTF-style when asked for.

	It handles geometry for multiple clients, so each client will only want a subset.
*/
namespace avs
{
	typedef uint64_t uid;
}
namespace teleport
{
	namespace unreal
	{
		extern std::string ToStdString(const FString &fstr);
		extern FString ToFString(const std::string &str);
	}
}
class UTexture;
class UTextureRenderTarget2D;
class UAssetImportData;
class UStreamableNode;
class UStreamableRootComponent;
struct FStaticMeshLODResources;
class TELEPORT_API GeometrySource
{
public:
	GeometrySource();
	~GeometrySource();
	bool Tick(float DeltaTime);
	void Initialise(class ATeleportMonitor* monitor, UWorld* world);
	void ClearData();

	//! Call periodically in edit mode, never when running.
	void StoreProxies();

	avs::uid AddMesh(class UMeshComponent* MeshComponent,bool force);

	/// Extracts the resources for the node, without adding or updating the node itself. Not recursive: call this with recursive nodes to ensure all are considered.
	bool ExtractResourcesForNode(UStreamableNode *node,bool force);
	//Adds the node to the geometry source; decomposing the node to its base components. Will update the node if it has already been processed before.
	//	component : Scene component the node represents.
	//	forceUpdate : Causes node data to be extracted, even if it has been before.
	//Return UID of node.
	avs::uid AddNode(UStreamableNode *node, bool forceUpdate = false);
	avs::uid GetNodeUid(UStreamableNode *node);
	 
	USceneComponent *GetNodeSceneComponent(avs::uid u);
	//Adds the material to the geometry source, where it is processed into a streamable material.
	//Returns the UID of the processed material information, or 0 if a nullptr is passed.
	avs::uid AddMaterial(class UMaterialInterface *materialInterface);

	avs::uid AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData);

	//Compresses any textures that were found during decomposition of actors.
	//Split-off so all texture compression can happen at once with a progress bar.
	void CompressTextures();

	void EnqueueAddProxyTexture_AnyThread(UTexture *source,UTexture *target);
	//
	static FGraphEventRef RunLambdaOnGameThread(TFunction<void()> InFunction);

protected:
	void AddTexture_Internal(avs::uid u,UTexture* texture);
	void RenderLightmap_RenderThread(FRHICommandListImmediate &RHICmdList,UTexture* source,UTexture* target,FVector4f Scale,FVector4f Add);
	void UpdateCachePath();
	struct Mesh
	{
		avs::uid id;
		UStaticMesh *staticMesh;
		FString bulkDataIDString; // ID string of the bulk data the last time it was processed; changes whenever the mesh data is reimported, so can be used to detect changes.
	};

	struct MaterialChangedInfo
	{
		avs::uid id = 0;
		bool wasProcessedThisSession = false;
	};
	//! Sometimes we create a modified Texture etc. The ProxyAsset struct tells us which asset to use and if it's been modified.
	struct ProxyAsset
	{
		UObject *proxyAsset=nullptr;
		FDateTime modified;
	};

	TMap<UObject*,ProxyAsset> proxyAssetMap;
	TSet<UObject*> proxiesToStore;
	TArray<UObject*> assetsToStore;

	//! Process any proxy assets that are ready.
	void StoreProxyAssets();

	class ATeleportMonitor* Monitor=nullptr;

	std::map<avs::uid, std::vector<vec3>> scaledPositionBuffers;
	std::map<avs::uid, std::vector<tvector4<signed char>>> tangentNormalBuffers; //Stores data to the corrected tangent and normal buffers.

	std::map<avs::uid,std::vector<vec3>> normalBuffers;
	std::map<avs::uid,std::vector<vec4>> tangentBuffers;
	std::map<avs::uid, std::vector<FVector2f>> processedUVs;

	std::map<avs::uid, USceneComponent *> sceneComponentFromNode;
	std::map<FName, avs::uid,FNameFastLess> processedNodes; //Nodes we have already stored in the GeometrySource; <Level Unique Node Name, Node Identifier>.
	TMap<UStaticMesh*, Mesh> processedMeshes; //Meshes we have already stored in the GeometrySource; the pointer points to the uid of the stored mesh information.
	TMap<UMaterialInterface*, MaterialChangedInfo> processedMaterials; //Materials we have already stored in the GeometrySource; the pointer points to the uid of the stored material information.
	TMap<FName, avs::uid> processedTextures; //Textures we have already stored in the GeometrySource; the pointer points to the uid of the stored texture information.
	TMap<const FStaticShadowDepthMapData*, avs::uid> processedShadowMaps;

	void PrepareMesh(Mesh* mesh);
	bool ExtractMesh(Mesh* mesh, uint8 lodIndex);
	bool ExtractMeshData(Mesh* mesh, FStaticMeshLODResources& lod, avs::AxesStandard extractToBasis);

	// Add a pure node with no mesh etc.
	avs::uid AddEmptyNode(UStreamableNode *node,avs::uid oldID);
	//Add a node that represents a mesh.
	//	meshComponent : Mesh the node will represent.
	//	oldID : ID being used by this node, if zero it will create a new ID.
	//Returns the ID of the node added.
	avs::uid AddMeshNode(UStreamableNode *node, avs::uid oldID);
	//Add a node that represents a light.
	//	lightComponent : Light the node will represent.
	//	oldID : ID being used by this node, if zero it will create a new ID.
	//Returns the ID of the node added.
	avs::uid AddShadowMapNode(ULightComponent* lightComponent, avs::uid oldID);

	//Returns component transform.
	//	component : Component we want the transform of.
	avs::Transform GetComponentTransform(USceneComponent* component);

	//! Equivalent to AddTexture, called only for lightmaps.
	avs::uid AddLightmapTexture(UTexture* texture,FVector4f Scale,FVector4f Add);
	//Determines if the texture has already been stored, and pulls apart the texture data and stores it in a avs::Texture.
	//	texture : UTexture to pull the texture data from.
	//Returns the uid for this texture.
	avs::uid AddTexture(UTexture *texture);

	//Returns the first texture in the material chain.
	//	materialInterface : The interface of the material we are decomposing.
	//	propertyChain : The material property we are decomposing.
	//	outTexture : Texture related to the chain to output into.
	void GetDefaultTexture(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture);

	//Decomposes the material into the texture, and outFactor supplied.
	//	materialInterface : The interface of the material we are decomposing.
	//	propertyChain : The material property we are decomposing.
	//	outTexture : Texture related to the chain to output into.
	//	outFactor : Factor related to the chain to output into.
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, float &outFactor);
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, vec3 &outFactor);
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, vec4 &outFactor);

	//Decomposes UMaterialExpressionTextureSample; extracting the texture, and tiling data.
	//	materialInterface : The base class of the material we are decomposing.
	//	textureSample : The expression we want to extract/decompose the data from.
	//	outTexture : Texture related to the chain to output into.
	//Returns the amount of expressions that were handled in the chain.
	size_t DecomposeTextureSampleExpression(UMaterialInterface* materialInterface, class UMaterialExpressionTextureSample* textureSample, avs::TextureAccessor& outTexture);

	//This will return 0 if there is no source data, or a nullptr is passed.
	int64 GetAssetImportTimestamp(UAssetImportData* importData);
};

struct ShadowMapData
{
	const FStaticShadowDepthMap& depthTexture;
	const FVector4& position;
	const FQuat& orientation;

	ShadowMapData(const FStaticShadowDepthMap& _depthTexture, const FVector4& _position, const FQuat& _orientation)
		:depthTexture(_depthTexture), position(_position), orientation(_orientation) {}

	ShadowMapData(const ULightComponent* light)
		:depthTexture(light->StaticShadowDepthMap),
		position(light->GetLightPosition()),
		orientation(light->GetDirection().Rotation().Quaternion())
		{}
};