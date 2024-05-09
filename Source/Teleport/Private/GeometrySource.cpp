#include "GeometrySource.h"

//General Unreal Engine
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Classes/EditorFramework/AssetImportData.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "RawMesh.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"
#include "Components/StreamableRootComponent.h"
#include "Components/StreamableNode.h"
#include "TeleportSettings.h"

//Unreal File Manager
#include "Core/Public/HAL/FileManagerGeneric.h"

//Textures
#include "Engine/Classes/EditorFramework/AssetImportData.h"

//Materials & Material Expressions
#include "Engine/Classes/Materials/Material.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant3Vector.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant4Vector.h"
#include "Engine/Classes/Materials/MaterialExpressionMultiply.h"
#include "Engine/Classes/Materials/MaterialExpressionScalarParameter.h"
#include "Engine/Classes/Materials/MaterialExpressionTextureCoordinate.h"
#include "Engine/Classes/Materials/MaterialExpressionVectorParameter.h"

//For progress bar while compressing textures.
#include "Misc/ScopedSlowTask.h"
 
#include "TeleportMonitor.h"
#include "TeleportModule.h"
#include "TeleportServer/GeometryStore.h"
#include "TeleportServer/UnityPlugin/InteropStructures.h"
 
#include <functional> //std::function
  
#if 0 
#include <random> 
std::default_random_engine generator; 
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif
#pragma optimize("",off)
using namespace teleport;
using namespace unreal;

std::string teleport::unreal::ToStdString(const FString &fstr)
{
	return std::string(TCHAR_TO_UTF8(*fstr));
}
FString teleport::unreal::ToFString(const std::string &str)
{
	return FString(str.c_str());
}

 FString StandardizePath(FString file_name, FString path_root)
{
	if (file_name == "")
	{
		return "";
	}
	FString p = file_name;
	p = p.Replace(TEXT("."), TEXT("_-_"), ESearchCase::IgnoreCase);
	p = p.Replace(TEXT(","), TEXT("_--_"), ESearchCase::IgnoreCase);
	p = p.Replace(TEXT(" "), TEXT("___"), ESearchCase::IgnoreCase);
	p = p.Replace(TEXT("\\"),TEXT( "/"), ESearchCase::IgnoreCase);
	if (path_root.Len() > 0)
		p = p.Replace(*path_root, TEXT(""), ESearchCase::IgnoreCase);
	return p;
}
template<class T>
FString GetResourcePath(T *obj, bool force=false)
{
#ifdef UNITY_EDITOR
	// If in Editor and not playing, let's force a regeneration of the path, in case there are code changes that modify the paths.
	if (!Application.isPlaying)
		force = true;
#endif
	FString path;//= obj->GetPathName();
	//FAssetData AssetData;
	if(obj->AssetImportData)
		path=obj->AssetImportData->GetFirstFilename();
	if(path.IsEmpty())
		path = obj->GetPathName();
	FString EngineContentPath = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir());
	FString ProjectContentPath=FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	if(FPaths::IsUnderDirectory(path,EngineContentPath))
	{
		FPaths::MakePathRelativeTo(path,*EngineContentPath);
		path=FString("Engine/")+path;
	}
	else if (FPaths::IsUnderDirectory(path, ProjectContentPath))
	{
		FPaths::MakePathRelativeTo(path, *ProjectContentPath);
	}
	else
	{
		int colon_pos=path.Find(":");
		if(colon_pos>=0)
			path=path.RightChop(colon_pos+2);
	}
	if(path.Len()>0&&path[0]=='/')
		path=path.RightChop(1);
	if(path.Len()>0&&path[0]=='\\')
		path=path.RightChop(1);
	//obj->GetAssetRegistryTags(AssetData);
	// dots and commas are undesirable in URL's. Therefore we replace them.
	path = StandardizePath(path, "Content/");
//	resourcePathManager.SetResourcePath(obj, path);
	return path;
}

#define LOG_MATERIAL_INTERFACE(materialInterface) UE_LOG(LogTeleport, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Error"));
#define LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name) UE_LOG(LogTeleport, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported expression with type name <" + name + ">"));
#define LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, length) UE_LOG(LogTeleport, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported property chain length of <" + length + ">"));
 

namespace
{
	const unsigned long long DUMMY_TEX_COORD = 0;
}

FName GetUniqueComponentName(USceneComponent* component)
{
	return *FPaths::Combine(component->GetOutermost()->GetName(), component->GetOuter()->GetName(), component->GetName());
}
FName GetUniqueActorName(AActor *actor)
{
	return *FPaths::Combine(actor->GetOutermost()->GetName(), actor->GetOuter()->GetName(), actor->GetName());
}

GeometrySource::GeometrySource()
	:Monitor(nullptr)
{}

GeometrySource::~GeometrySource()
{}

void GeometrySource::Initialise(ATeleportMonitor* monitor, UWorld* world)
{
	check(monitor);  

	Monitor = monitor;
	if(Monitor->ResetCache)
	{
		//Clear all stored data, if the reset cache flag is set.
		ClearData(); 
	}
	else
	{
		//Otherwise, flag all processed materials as not having been changed this session; so we will update them incase they changed, while also reusing IDs.
		for(auto& material : processedMaterials)
		{
			material.second.wasProcessedThisSession = false;
		}
	}
	const UTeleportSettings *TeleportSettings=GetDefault<UTeleportSettings>();
	if(TeleportSettings)
	{
		FString cachePath=TeleportSettings->CachePath.Path;
		teleport::server::GeometryStore::GetInstance().SetCachePath(ToStdString(cachePath).c_str());
	}
	UStaticMeshComponent* handMeshComponent = nullptr;
	//Use the hand actor blueprint set in the monitor.
	if(Monitor->HandActor)
	{
		AActor* handActor = world->SpawnActor(Monitor->HandActor->GeneratedClass);
		handMeshComponent = Cast<UStaticMeshComponent>(handActor->GetComponentByClass(UStaticMeshComponent::StaticClass()));

		if(!handMeshComponent)
		{
			UE_LOG(LogTeleport, Warning, TEXT("Hand actor set in TeleportMonitor has no static mesh component."));
		}
	}
	else
	{
		UE_LOG(LogTeleport, Log, TEXT("No hand actor set in TeleportMonitor."));
	}

	//If we can not use a set blueprint, then we use the default one.
	if(!handMeshComponent)
	{
		FString defaultHandLocation("Blueprint'/Game/Teleport/TeleportHand.TeleportHand'");
		UBlueprint* defaultHandBlueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *defaultHandLocation));

		if(defaultHandBlueprint)
		{
			AActor* handActor = world->SpawnActor(defaultHandBlueprint->GeneratedClass);
			handMeshComponent = Cast<UStaticMeshComponent>(handActor->GetComponentByClass(UStaticMeshComponent::StaticClass()));

			if(!handMeshComponent)
			{
				UE_LOG(LogTeleport, Warning, TEXT("Default hand actor in <%s> has no static mesh component."), *defaultHandLocation);
			}
		}
		else
		{
			UE_LOG(LogTeleport, Warning, TEXT("Could not find default hand actor in <%s>."), *defaultHandLocation);
		}
	}

	//Add the hand actors, and their resources, to the geometry source.
	/*if(handMeshComponent)
	{
		std::vector<std::pair<void*, avs::uid>> oldHands = GeometryStore::GetInstance().getHands();

		avs::uid firstHandID = AddNode(handActor, true);

		//Retrieve the newly added/updated hand node, and set to the correct data type.
		avs::Node* firstHand = GeometryStore::GetInstance().getNode(firstHandID);
		firstHand->data_type = avs::NodeDataType::Hand;

		//Whether we created a new node for the hand model, or it already existed; i.e whether the hand actor has changed.
		bool isSameHandNode = oldHands.size() != 0 && firstHandID == oldHands[0].second;

		//Set the ID of the second hand, and create second hand's node by copying the first hand.
		avs::uid secondHandID = isSameHandNode ? oldHands[1].second : avs::GenerateUid();
		avs::Node secondHand{*firstHand};
		GeometryStore::GetInstance().storeNode(secondHandID, secondHand);

		///Use data nodes rather than actors for pointers; there's little else they can point to.
		GeometryStore::GetInstance().setHands({firstHand, firstHandID}, {&secondHand, secondHandID});
	}*/
}

avs::AttributeSemantic IndexToSemantic(int index)
{
	switch (index)
	{
	case 0:
		return avs::AttributeSemantic::POSITION;
	case 1:
		return avs::AttributeSemantic::NORMAL;
	case 2:
		return avs::AttributeSemantic::TANGENT;
	case 3:
		return avs::AttributeSemantic::TEXCOORD_0;
	case 4:
		return avs::AttributeSemantic::TEXCOORD_1;
	case 5:
		return avs::AttributeSemantic::COLOR_0;
	case 6:
		return avs::AttributeSemantic::JOINTS_0;
	case 7:
		return avs::AttributeSemantic::WEIGHTS_0;
	};
	return avs::AttributeSemantic::TEXCOORD_0;
}

bool GeometrySource::ExtractMesh(Mesh* mesh, uint8 lodIndex)
{
	if (mesh->staticMesh->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
	{
		return false;
	}

	UStaticMesh *StaticMesh = Cast<UStaticMesh>(mesh->staticMesh);
	auto &lods = StaticMesh->GetRenderData()->LODResources;
	if(lods.Num() == 0) return false;

	if(!ExtractMeshData(mesh, lods[lodIndex], avs::AxesStandard::EngineeringStyle))
		return false;
	if(!ExtractMeshData(mesh, lods[lodIndex], avs::AxesStandard::GlStyle))
		return false;
	
	return true;
}

bool GeometrySource::ExtractMeshData(Mesh *mesh, FStaticMeshLODResources &lod, avs::AxesStandard axesStandard)
{
	InteropMesh newMesh;

	std::vector<avs::PrimitiveArray> primitiveArrays;
	std::vector<avs::uid> accessorIDs;
	std::vector<avs::Accessor> accessors;
	std::vector<avs::uid> bufferViewIDs;
	std::vector<avs::BufferView> bufferViews;
	std::vector<avs::uid> bufferIDs;
	std::vector<avs::GeometryBuffer> buffers;

	auto AddBuffer = [this, &buffers,&bufferIDs](avs::uid b_uid, size_t num, size_t stride, void* data)
	{
		avs::GeometryBuffer b ;
		b.byteLength = num * stride;
		b.data = static_cast<uint8_t*>(data); //Remember, just a pointer: we don't own this data.
		buffers.push_back(b);
		bufferIDs.push_back(b_uid);
	};

	auto AddBufferView = [this,&bufferViews,&bufferViewIDs](avs::uid b_uid, avs::uid v_uid, size_t start_index, size_t num, size_t stride)
	{
		avs::BufferView bv;
		bv.byteOffset = start_index * stride;
		bv.byteLength = num * stride;
		bv.byteStride = stride;
		bv.buffer = b_uid;
		bufferViews.push_back(bv);
		bufferViewIDs.push_back(v_uid);
	};

	FPositionVertexBuffer& pb = lod.VertexBuffers.PositionVertexBuffer;
	FStaticMeshVertexBuffer& vb = lod.VertexBuffers.StaticMeshVertexBuffer;

	avs::uid positions_uid = avs::GenerateUid();
	avs::uid normals_uid = avs::GenerateUid();
#ifndef TELEPORT_PACKED_NORMAL_TANGENTS
	avs::uid tangents_uid = avs::GenerateUid();
#endif
	avs::uid texcoords_uid = avs::GenerateUid();
	avs::uid indices_uid = avs::GenerateUid();
	avs::uid positions_view_uid = avs::GenerateUid();
	avs::uid normals_view_uid = avs::GenerateUid();
#ifndef TELEPORT_PACKED_NORMAL_TANGENTS
	avs::uid tangents_view_uid = avs::GenerateUid();
#endif
	avs::uid texcoords_view_uid[8];

	avs::uid indices_view_uid = avs::GenerateUid();
	avs::Accessor::ComponentType componentType;
	size_t istride;

	//Switch components based on client.
	int x, y, z, w;
	switch(axesStandard)
	{
		case avs::AxesStandard::GlStyle:
			z = 0; x = 1; y = 2; w = 3;
			break;
		case avs::AxesStandard::EngineeringStyle:
			y = 0; x = 1; z = 2; w = 3;
			break;
		default:
			UE_LOG(LogTeleport, Error, TEXT("Attempting to extract mesh data from mesh \"%s\" using unsupported axes standard of %d!"), *mesh->staticMesh->GetName(), axesStandard);
			x = 0; y = 1; z = 2; w = 3;
			break;
	}

	// First create the Buffers:
	// Position:
	{
		std::vector<vec3>& p = scaledPositionBuffers[positions_uid];
		p.resize(pb.GetNumVertices());
		const float* orig = static_cast<const float*>(pb.GetVertexData());
		for(size_t j = 0; j < pb.GetNumVertices(); j++)
		{
			p[j].x = orig[j * 3 + x] * 0.01f;
			p[j].y = orig[j * 3 + y] * 0.01f;
			p[j].z = orig[j * 3 + z] * 0.01f;
		}
		size_t stride = pb.GetStride();
		AddBuffer(positions_uid, pb.GetNumVertices(), stride, p.data());
		size_t position_stride = pb.GetStride();
		// Offset is zero, because the sections are just lists of indices. 
		AddBufferView(positions_uid, positions_view_uid, 0, pb.GetNumVertices(), position_stride);
	}
	// It's not clear that draco compression likes packed tangent-normals, so we'll separate them here:
#ifndef TELEPORT_PACKED_NORMAL_TANGENTS
	std::vector<vec3>& normals=normalBuffers[normals_uid];
	std::vector<vec4>& tangents=tangentBuffers[tangents_uid];
	normals.resize(pb.GetNumVertices());
	tangents.resize(pb.GetNumVertices());
	if(vb.GetUseHighPrecisionTangentBasis())
	{
		UE_LOG(LogTeleport,Warning,TEXT("High Precision Tangent Basis is untested, but in-use on mesh \"%s\"."),*mesh->staticMesh->GetName());

		const signed short* original=static_cast<signed short*>(vb.GetTangentData());
		//Create corrected version of the original tangent buffer for the PC client.
		for(size_t i=0; i<pb.GetNumVertices(); i++)
		{
			//Tangents
			vec4 &t=tangents[i];
			t.x=float(original[i*8+x])/16383.f;
			t.y=float(original[i*8+y])/16383.f;
			t.z=float(original[i*8+z])/16383.f;
			t.w=float(original[i*8+w])/16383.f;

			//Normals
			vec3 &n=normals[i];
			n.x=float(original[i*8+4+x])/16383.f;
			n.y=float(original[i*8+4+y])/16383.f;
			n.z=float(original[i*8+4+z])/16383.f;
		}
	}
	else
	{
		//It won't actually swizzle/component-shift if the C-array isn't the correct data type.
		const signed char* original=static_cast<signed char*>(vb.GetTangentData());
		for(size_t i=0; i<pb.GetNumVertices(); i++)
		{
			//Tangents
			vec4 &t=tangents[i];
			t.x=float(original[i*8+x])/127.f;
			t.y=float(original[i*8+y])/127.f;
			t.z=float(original[i*8+z])/127.f;
			t.w=float(original[i*8+w])/127.f;

			//Normals
			vec3 &n=normals[i];
			n.x=float(original[i*8+4+x])/127.f;
			n.y=float(original[i*8+4+y])/127.f;
			n.z=float(original[i*8+4+z])/127.f;
		}
	}

	size_t normal_stride=sizeof(vec3);
	size_t tangent_stride=sizeof(vec4);
	// Normal:
	{
		AddBuffer(normals_uid,vb.GetNumVertices(),normal_stride,normals.data());
		AddBufferView(normals_uid,normals_view_uid,0,pb.GetNumVertices(),tangent_stride);
	}

	// Tangent:
	{
		AddBuffer(tangents_uid, vb.GetNumVertices(),tangent_stride, tangents.data());
		AddBufferView(tangents_uid, tangents_view_uid, 0, pb.GetNumVertices(), tangent_stride);
	}
#else
	{
		std::vector<tvector4<signed char>>& tangents = tangentNormalBuffers[normals_uid];
		tangents.resize(pb.GetNumVertices() * 2);

		if(vb.GetUseHighPrecisionTangentBasis())
		{
			UE_LOG(LogTeleport, Warning, TEXT("High Precision Tangent Basis is untested, but in-use on mesh \"%s\"."), *mesh->staticMesh->GetName());

			const signed short* original = static_cast<signed short*>(vb.GetTangentData());
			//Create corrected version of the original tangent buffer for the PC client.
			for(size_t i = 0; i < pb.GetNumVertices(); i++)
			{
				//Tangents
				tangents[i * 2].x = original[i * 8 + x];
				tangents[i * 2].y = original[i * 8 + y];
				tangents[i * 2].z = original[i * 8 + z];
				tangents[i * 2].w = original[i * 8 + w];

				//Normals
				tangents[i * 2 + 1].x = original[i * 8 + 4 + x];
				tangents[i * 2 + 1].y = original[i * 8 + 4 + y];
				tangents[i * 2 + 1].z = original[i * 8 + 4 + z];
				tangents[i * 2 + 1].w = original[i * 8 + 4 + w];
			}
		}
		else
		{
			//It won't actually swizzle/component-shift if the C-array isn't the correct data type.
			const signed char* original = static_cast<signed char*>(vb.GetTangentData());
			//Create corrected version of the original tangent buffer for the PC client.
			for(size_t i = 0; i < pb.GetNumVertices(); i++)
			{
				//Tangents
				tangents[i * 2].x = original[i * 8 + x];
				tangents[i * 2].y = original[i * 8 + y];
				tangents[i * 2].z = original[i * 8 + z];
				tangents[i * 2].w = original[i * 8 + w];

				//Normals
				tangents[i * 2 + 1].x = original[i * 8 + 4 + x];
				tangents[i * 2 + 1].y = original[i * 8 + 4 + y];
				tangents[i * 2 + 1].z = original[i * 8 + 4 + z];
				tangents[i * 2 + 1].w = original[i * 8 + 4 + w];
			}
		}

		size_t tangent_stride = sizeof(signed char) * 8;
		// Normal:
		{
			AddBuffer(normals_uid, vb.GetNumVertices(), tangent_stride, tangents.data());
			AddBufferView(normals_uid, normals_view_uid, 0, pb.GetNumVertices(), tangent_stride);
		}

		// Tangent:
		/*if(vb.GetTangentData())
		{
			size_t stride = vb.GetTangentSize() / vb.GetNumVertices();
			AddBuffer(tangents_uid, vb.GetNumVertices(), stride, tangents.data());
			AddBufferView(tangents_uid, tangents_view_uid, 0, pb.GetNumVertices(), tangent_stride);
		}*/
	}
	#endif
	// TexCoords:
	{
		std::vector<FVector2f>& uvData = processedUVs[texcoords_uid];
		uvData.reserve(vb.GetNumVertices() * vb.GetNumTexCoords());

		size_t texcoords_stride = sizeof(FVector2f);
		for(size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 

			for(uint32_t k = 0; k < vb.GetNumVertices(); k++)
			{
				FVector2f v2 = vb.GetVertexUV(k, j);
				uvData.push_back(v2);
			}
		}
		AddBuffer(texcoords_uid, vb.GetNumVertices() * vb.GetNumTexCoords(), texcoords_stride, uvData.data());
		for(size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 
			texcoords_view_uid[j] = avs::GenerateUid();
			AddBufferView(texcoords_uid, texcoords_view_uid[j], j * pb.GetNumVertices(), pb.GetNumVertices(), texcoords_stride);
		}
	}

	//Indices:
	{
		FRawStaticIndexBuffer& ib = lod.IndexBuffer;
		FIndexArrayView arr = ib.GetArrayView();

		componentType = ib.Is32Bit() ? avs::Accessor::ComponentType::UINT : avs::Accessor::ComponentType::USHORT;
		istride = avs::GetComponentSize(componentType);

		AddBuffer(indices_uid, ib.GetNumIndices(), istride, reinterpret_cast<void*>(((uint64*)&arr)[0]));
		AddBufferView(indices_uid, indices_view_uid, 0, ib.GetNumIndices(), istride);
	}

	// Now create the views:
	size_t  num_elements = lod.Sections.Num();
	primitiveArrays.resize(num_elements);
	for(size_t i = 0; i < num_elements; i++)
	{
		auto& section = lod.Sections[i];
		auto& pa =primitiveArrays[i];
		//pa.attributeCount = 2 + (vb.GetTangentData() ? 1 : 0) + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);
		pa.attributeCount = 2 + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);
		pa.attributes = new avs::Attribute[pa.attributeCount];
		size_t idx = 0;
		// Position:
		{
			avs::Attribute& attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::POSITION;
			avs::Accessor a ;
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = pb.GetNumVertices();
			a.bufferView = positions_view_uid;
			accessorIDs.push_back(attr.accessor);
			accessors.push_back(a);
		}
#ifndef TELEPORT_PACKED_NORMAL_TANGENTS
		// Normal:
		{
			avs::Attribute& attr=pa.attributes[idx++];
			attr.accessor=avs::GenerateUid();
			attr.semantic=avs::AttributeSemantic::NORMAL;
			avs::Accessor& a=accessors[attr.accessor];
			a.byteOffset=0;
			a.type=avs::Accessor::DataType::VEC4;
			//GetUseHighPrecisionTangentBasis() ? PF_R16G16B16A16_SNORM : PF_R8G8B8A8_SNORM
			a.componentType=avs::Accessor::ComponentType::FLOAT;
			a.count=vb.GetNumVertices();
			a.bufferView=normals_view_uid;
			accessorIDs.push_back(attr.accessor);
			accessors.push_back(a);
		}
		// Tangent:
		{
			avs::Attribute& attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENT;
			avs::Accessor& a =accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count=vb.GetNumVertices();
			a.bufferView = tangents_view_uid;
			accessorIDs.push_back(attr.accessor);
			accessors.push_back(a);
		}
#else
		// Normal:
		{
			avs::Attribute& attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENTNORMALXZ;
			avs::Accessor& a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			//GetUseHighPrecisionTangentBasis() ? PF_R16G16B16A16_SNORM : PF_R8G8B8A8_SNORM
			a.componentType = vb.GetUseHighPrecisionTangentBasis() ? avs::Accessor::ComponentType::UINT : avs::Accessor::ComponentType::USHORT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = normals_view_uid;
		}
		#endif
		// TexCoords:
		for(size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			avs::Attribute& attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = j == 0 ? avs::AttributeSemantic::TEXCOORD_0 : avs::AttributeSemantic::TEXCOORD_1;
			avs::Accessor& a =accessors[attr.accessor];
			// Offset into the global texcoord views
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC2;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = texcoords_view_uid[j];
			accessorIDs.push_back(attr.accessor);
			accessors.push_back(a);
		}

		//Indices:
		{
			pa.indices_accessor = avs::GenerateUid();

			avs::Accessor& i_a =accessors[pa.indices_accessor];
			i_a.byteOffset = section.FirstIndex * istride;
			i_a.type = avs::Accessor::DataType::SCALAR;
			i_a.componentType = componentType;
			i_a.count = section.NumTriangles * 3;// same as pb???
			i_a.bufferView = indices_view_uid;
			accessorIDs.push_back(pa.indices_accessor);
			accessors.push_back(i_a);
		}

		// probably no default material in UE4?
		pa.material = 0;
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}
	std::string name=ToStdString(mesh->staticMesh->GetName());
	newMesh.name=name.c_str();

	newMesh.primitiveArrayCount=primitiveArrays.size();
	newMesh.primitiveArrays=primitiveArrays.data();

	newMesh.accessorIDs		=accessorIDs.data();
	newMesh.accessors		=accessors.data();
	newMesh.accessorCount	=accessors.size();

	newMesh.bufferViewIDs	=bufferViewIDs.data();
	newMesh.bufferViews		=bufferViews.data();
	newMesh.bufferViewCount	=bufferViews.size();
	
	newMesh.bufferIDs	=bufferIDs.data();
	newMesh.buffers		=buffers.data();
	newMesh.bufferCount	=buffers.size();

	uint64_t inverseBindMatricesAccessorID=0;
	newMesh.inverseBindMatricesAccessorID=inverseBindMatricesAccessorID;
	std::string path = ToStdString(GetResourcePath(mesh->staticMesh));
	UpdateCachePath();
	return false;//teleport::server::GeometryStore::GetInstance().storeMesh(mesh->id, path,GetAssetImportTimestamp(mesh->staticMesh->AssetImportData),newMesh, axesStandard);
}

void GeometrySource::UpdateCachePath()
{
	const UTeleportSettings *TeleportSettings=GetDefault<UTeleportSettings>();
	if(TeleportSettings)
	{
		FString cachePath=TeleportSettings->CachePath.Path;
		teleport::server::GeometryStore::GetInstance().SetCachePath(ToStdString(cachePath).c_str());
	}
}
avs::uid GeometrySource::AddEmptyNode(UStreamableNode *streamableNode,avs::uid oldID)
{
	UpdateCachePath();
	USceneComponent *sceneComponent=streamableNode->GetSceneComponent();
	USceneComponent* parent=sceneComponent->GetAttachParent();
	avs::NodeDataType nodeDataType=avs::NodeDataType::None;

	avs::uid parentID=0;
	if(parent)
		parentID=processedNodes[GetUniqueComponentName(parent)];

	avs::uid nodeID=oldID==0?avs::GenerateUid():oldID;
	streamableNode->SetUid(nodeID);
	bool stationary=false;//node->IsStationary;
	int priority=0;
	avs::NodeRenderState nodeRenderState;

	avs::Node newNode={ToStdString(sceneComponent->GetName()),GetComponentTransform(sceneComponent),stationary,0,priority,parentID,avs::NodeDataType::None,0,{},0,{},{},nodeRenderState,{0,0,0,0},0.f,{0,0,0},0,0.f,""};

	processedNodes[GetUniqueComponentName(sceneComponent)]=nodeID;
	sceneComponentFromNode[nodeID]=sceneComponent;
	UpdateCachePath();
	teleport::server::GeometryStore::GetInstance().storeNode(nodeID,newNode);

	return nodeID;
}
avs::uid GeometrySource::AddMeshNode(UStreamableNode *streamableNode, avs::uid oldID)
{
	avs::uid nodeID=AddEmptyNode(streamableNode,oldID);
	if(!nodeID)
		return 0;
	ExtractResourcesForNode(streamableNode,false);
	UMeshComponent *meshComponent=streamableNode->GetMesh();
	avs::uid dataID=AddMesh(meshComponent,false);
	std::vector<avs::uid> materialIDs;
	TArray<UMaterialInterface*> mats=meshComponent->GetMaterials();
	//Add material, and textures, for streaming to clients.
	for(int32 i=0; i<mats.Num(); i++)
	{
		avs::uid materialID=AddMaterial(mats[i]);
		if(materialID!=0)
			materialIDs.push_back(materialID);
		else UE_LOG(LogTeleport,Warning,TEXT("Actor \"%s\" has no material applied to material slot %d."),*meshComponent->GetOuter()->GetName(),i);
	}
	avs::NodeDataType nodeDataType = avs::NodeDataType::Mesh;
	avs::NodeRenderState nodeRenderState;

	avs::Node* node=teleport::server::GeometryStore::GetInstance().getNode(nodeID);
	node->data_type=nodeDataType;
	node->data_uid=dataID;
	node->materials=materialIDs;
	node->renderState=nodeRenderState;

	return nodeID;
}

avs::uid GeometrySource::AddShadowMapNode(ULightComponent* lightComponent, avs::uid oldID)
{
	avs::uid dataID = AddShadowMap(lightComponent->StaticShadowDepthMap.Data);
	if(dataID == 0)
	{
		UE_LOG(LogTeleport, Warning, TEXT("Failed to add shadow map for actor with name: %s"), *lightComponent->GetOuter()->GetName());
		return 0;
	}
	avs::uid nodeID = oldID == 0 ? avs::GenerateUid() : oldID;
	/*
	avs::Node newNode{GetComponentTransform(lightComponent), dataID, avs::NodeDataType::ShadowMap, {}, {}};

	processedNodes[GetUniqueComponentName(lightComponent)] = nodeID;
	teleport::server::GeometryStore::GetInstance().storeNode(nodeID, newNode);
	*/
	return nodeID;
}

avs::Transform GeometrySource::GetComponentTransform(USceneComponent* component)
{
	check(component)

	FTransform transform = component->GetComponentTransform();

	// convert offset from cm to metres.
	FVector t = transform.GetTranslation() * 0.01f;
	// We retain Unreal axes until sending to individual clients, which might have varying standards.
	FQuat r = transform.GetRotation();
	const FVector s = transform.GetScale3D();

	return avs::Transform{{(float)t.X, (float)t.Y, (float)t.Z}, {(float)r.X, (float)r.Y, (float)r.Z, (float)r.W}, {(float)s.X, (float)s.Y, (float)s.Z}};
}

void GeometrySource::ClearData()
{
	scaledPositionBuffers.clear();
	processedUVs.clear();

	processedNodes.clear();
	sceneComponentFromNode.clear();
	processedMeshes.clear();
	processedMaterials.clear();
	processedTextures.clear();
	processedShadowMaps.clear();

	//We just use the pointer. I.e. we don't copy the mesh buffer data.
	teleport::server::GeometryStore::GetInstance().clear(false);
}

avs::uid GeometrySource::AddMesh(UMeshComponent *MeshComponent,bool force)
{
	if(!MeshComponent)
		return 0;
	if (MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		UE_LOG(LogTeleport, Warning, TEXT("Skeletal meshes not supported yet. Found on actor: %s"), *MeshComponent->GetOuter()->GetName());
		return 0;
	}

	UStaticMeshComponent* staticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	UStaticMesh *staticMesh = staticMeshComponent->GetStaticMesh();

	if(!staticMesh)
	{
		UE_LOG(LogTeleport, Warning, TEXT("Actor \"%s\" has been set as streamable, but they have no mesh assigned to their mesh component!"), *MeshComponent->GetOuter()->GetName());
		return 0;
	}

	//The mesh data was reimported, if the ID string has changed.
	FString idString=staticMesh->GetFName().ToString();

	Mesh* mesh;

	auto meshIt = processedMeshes.find(staticMesh);
	if(meshIt != processedMeshes.end())
	{
		//Reuse the ID if this mesh has been processed before.
		mesh = &meshIt->second;

		//Return if we have already processed the mesh in this play session, or the processed data wasn't cleared at the start of the play session, and the mesh data has not changed.
		if(!force&&idString == mesh->bulkDataIDString)
		{
			return mesh->id;
		}
	}
	else
	{
		//Create a new ID if this mesh has never been processed.
		mesh = &processedMeshes[staticMesh];
		mesh->id = avs::GenerateUid();
	}

	mesh->staticMesh = staticMesh;
	mesh->bulkDataIDString = idString;
	PrepareMesh(mesh);
	if(!ExtractMesh(mesh, 0))
	{
		UE_LOG(LogTeleport, Error, TEXT("Mesh \"%s\" could not be extracted!"), *staticMesh->GetFullName());
		auto k = processedMeshes.find(staticMesh);
		if(k!=processedMeshes.end())
			processedMeshes.erase(k);
		return 0;
	}
	
	return mesh->id;
}

bool GeometrySource::ExtractResourcesForNode(UStreamableNode *streamableNode,bool force)
{
	if(!streamableNode)
		return false;
	check(streamableNode);
	USceneComponent *sceneComponent=streamableNode->GetSceneComponent();
	if(!sceneComponent)
		return false;
	UMeshComponent *meshComponent=streamableNode->GetMesh();
	if(meshComponent==nullptr)
	{
		UE_LOG(LogTeleport,Error,TEXT("Node \"%s\" has no meshComponent."),*(sceneComponent->GetOwner()->GetName()));
		return false;
	}
	avs::uid dataID=AddMesh(meshComponent,force);
	if(dataID==0)
	{
		UE_LOG(LogTeleport,Error,TEXT("Node \"%s\" failed to add mesh."),*(meshComponent->GetOwner()->GetName()));
		return false;
	}
	std::vector<avs::uid> materialIDs;
	//Materials that this mesh has applied to its material slots.
	TArray<UMaterialInterface*> mats=meshComponent->GetMaterials();

	//Add material, and textures, for streaming to clients.
	for(int32 i=0; i<mats.Num(); i++)
	{
		avs::uid materialID=AddMaterial(mats[i]);
		if(materialID!=0)
			materialIDs.push_back(materialID);
		else UE_LOG(LogTeleport,Warning,TEXT("Actor \"%s\" has no material applied to material slot %d."),*meshComponent->GetOuter()->GetName(),i);
	}
	return true;
}

avs::uid GeometrySource::AddNode(UStreamableNode *node, bool forceUpdate)
{
	if(!node)
		return 0;
	check(node);
	USceneComponent *sceneComponent = node->GetSceneComponent();
	if(!sceneComponent)
		return 0;
	std::map<FName, avs::uid>::iterator nodeIterator = processedNodes.find(GetUniqueComponentName(sceneComponent));

	avs::uid nodeID =node->GetUid().Value;
	if(nodeID==0)
		nodeID=nodeIterator != processedNodes.end() ? nodeIterator->second : 0;

	if(forceUpdate || nodeID == 0)
	{
		UMeshComponent *meshComponent = node->GetMesh();
		ULightComponent *lightComponent = node->GetLightComponent();

		if(meshComponent)
			nodeID = AddMeshNode(node, nodeID);
		else if(lightComponent)
			nodeID = AddShadowMapNode(lightComponent, nodeID);
		else
		{
			nodeID =AddEmptyNode(node, nodeID);
		}
	}
	node->SetUid(nodeID);
	return nodeID;
}

avs::uid GeometrySource::GetNodeUid(UStreamableNode *node)
{
	USceneComponent *sceneComponent = node->GetSceneComponent();
	std::map<FName, avs::uid>::iterator nodeIterator = processedNodes.find(GetUniqueComponentName(sceneComponent));
	avs::uid nodeID = nodeIterator != processedNodes.end() ? nodeIterator->second : 0;
	return nodeID;
}

USceneComponent *GeometrySource::GetNodeSceneComponent(avs::uid u)
{
	return sceneComponentFromNode[u];
}

avs::uid GeometrySource::AddMaterial(UMaterialInterface* materialInterface)
{
	//Return 0 if we were passed a nullptr.
	if(!materialInterface)
		return 0;
	//Try and locate the pointer in the list of processed materials.
	std::unordered_map<UMaterialInterface*, MaterialChangedInfo>::iterator materialIt = processedMaterials.find(materialInterface);

	avs::uid materialID;
	if(materialIt != processedMaterials.end())
	{
		//Reuse the ID if this material has been processed before.
		materialID = materialIt->second.id;
		//Return if we have already processed the material in this play session.
		if(materialIt->second.wasProcessedThisSession)
			return materialID;
	}
	else
	{
		materialID = avs::GenerateUid();
	}
	processedMaterials[materialInterface] = {materialID, true};

	avs::Material newMaterial;

	newMaterial.name = TCHAR_TO_ANSI(*materialInterface->GetName());

	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_BaseColor, newMaterial.pbrMetallicRoughness.baseColorTexture, newMaterial.pbrMetallicRoughness.baseColorFactor);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Metallic, newMaterial.pbrMetallicRoughness.metallicRoughnessTexture, newMaterial.pbrMetallicRoughness.metallicFactor);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Roughness, newMaterial.pbrMetallicRoughness.metallicRoughnessTexture, newMaterial.pbrMetallicRoughness.roughnessMultiplier);
	newMaterial.pbrMetallicRoughness.roughnessOffset=0.f;
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_AmbientOcclusion, newMaterial.occlusionTexture, newMaterial.occlusionTexture.strength);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Normal, newMaterial.normalTexture, newMaterial.normalTexture.scale);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_EmissiveColor, newMaterial.emissiveTexture, newMaterial.emissiveFactor);

	//MP_WorldPositionOffset Property Chain for SimpleGrassWind
	{
		TArray<UMaterialExpression*> outExpressions;
		materialInterface->GetMaterial()->GetExpressionsInPropertyChain(MP_WorldPositionOffset, outExpressions, nullptr);

		if(outExpressions.Num() != 0)
		{
			if(outExpressions[0]->GetName().Contains("MaterialFunctionCall"))
			{
				UMaterialExpressionMaterialFunctionCall* functionExp = Cast<UMaterialExpressionMaterialFunctionCall>(outExpressions[0]);
				if(functionExp->MaterialFunction->GetName() == "SimpleGrassWind")
				{
					avs::SimpleGrassWindExtension simpleGrassWind;

					if(functionExp->FunctionInputs[0].Input.Expression && functionExp->FunctionInputs[0].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windIntensity = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[0].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[1].Input.Expression && functionExp->FunctionInputs[1].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windWeight = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[1].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[2].Input.Expression && functionExp->FunctionInputs[2].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windSpeed = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[2].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[3].Input.Expression && functionExp->FunctionInputs[3].Input.Expression->GetName().Contains("TextureSample"))
					{
						simpleGrassWind.texUID = AddTexture(Cast<UMaterialExpressionTextureBase>(functionExp->FunctionInputs[3].Input.Expression)->Texture);
					}

					newMaterial.extensions[avs::MaterialExtensionIdentifier::SIMPLE_GRASS_WIND] = std::make_unique<avs::SimpleGrassWindExtension>(simpleGrassWind);
				}
			}
		}
	}

	std::string path = ToStdString(GetResourcePath(materialInterface));
	int64 timestamp=0;
	if (materialInterface->AssetImportData)
		timestamp = GetAssetImportTimestamp(materialInterface->AssetImportData);
	UpdateCachePath();
	teleport::server::GeometryStore::GetInstance().storeMaterial(materialID, "", path, timestamp, newMaterial);

	UE_CLOG(newMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != newMaterial.occlusionTexture.index, LogTeleport, Warning, TEXT("Occlusion texture on material <%s> is not combined with metallic-roughness texture."), *materialInterface->GetName());

	return materialID;
}

avs::uid GeometrySource::AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData)
{
	//Check for nullptr
	if(!shadowDepthMapData) return 0;

	//Return pre-stored shadow_uid
	auto it = processedShadowMaps.find(shadowDepthMapData);
	if (it != processedShadowMaps.end())
	{
		return (it->second);
	}

	//Generate new shadow map
	avs::uid shadow_uid = avs::GenerateUid();
	/*avs::Texture shadowTexture;

	shadowTexture.name = std::string("Shadow Map UID: ") + std::to_string(shadow_uid);
	shadowTexture.width = shadowDepthMapData->ShadowMapSizeX;
	shadowTexture.height = shadowDepthMapData->ShadowMapSizeY;
	shadowTexture.depth = 1;
	shadowTexture.bytesPerPixel = shadowDepthMapData->DepthSamples.GetTypeSize();;
	shadowTexture.arrayCount = 1;
	shadowTexture.mipCount = 1;

	shadowTexture.format = shadowTexture.bytesPerPixel == 4 ? avs::TextureFormat::D32F : 
							shadowTexture.bytesPerPixel == 3 ? avs::TextureFormat::D24F : 
							shadowTexture.bytesPerPixel == 2 ? avs::TextureFormat::D16F :
							avs::TextureFormat::INVALID;
	shadowTexture.compression = avs::TextureCompression::UNCOMPRESSED;

	shadowTexture.dataSize = shadowDepthMapData->DepthSamples.GetAllocatedSize();
	shadowTexture.data = new unsigned char[shadowTexture.dataSize];
	memcpy(shadowTexture.data, (uint8_t*)shadowDepthMapData->DepthSamples.GetData(), shadowTexture.dataSize);
	shadowTexture.sampler_uid = 0;

	processedShadowMaps[shadowDepthMapData] = shadow_uid;
	teleport::server::GeometryStore::GetInstance().storeShadowMap(shadow_uid, "DUD GUID", 0, shadowTexture);
	*/
	return shadow_uid;
}

void GeometrySource::CompressTextures()
{
	UpdateCachePath();
	size_t texturesToCompressAmount = teleport::server::GeometryStore::GetInstance().getNumberOfTexturesWaitingForCompression();

#define LOCTEXT_NAMESPACE "GeometrySource"
	//Create FScopedSlowTask to show user progress of compressing the texture.
	FScopedSlowTask compressTextureTask(texturesToCompressAmount, FText::Format(LOCTEXT("Compressing Texture", "Starting compression of {0} textures"), texturesToCompressAmount));
	compressTextureTask.MakeDialog(false, true);

	for(int i = 0; i < texturesToCompressAmount; i++)
	{
		const avs::Texture *textureInfo = teleport::server::GeometryStore::GetInstance().getNextTextureToCompress();
		compressTextureTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Compressing Texture", "Compressing texture {0}/{1} ({2} [{3} x {4}])"), i + 1, texturesToCompressAmount, FText::FromString(ANSI_TO_TCHAR(textureInfo->name.data())), textureInfo->width, textureInfo->height));
		
		teleport::server::GeometryStore::GetInstance().compressNextTexture();
	}
#undef LOCTEXT_NAMESPACE
}

void GeometrySource::PrepareMesh(Mesh* mesh)
{
	// We will pre-encode the mesh to prepare it for streaming.
	if (mesh->staticMesh->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
	{
		UStaticMesh* StaticMesh = mesh->staticMesh;
		int verts = StaticMesh->GetNumVertices(0);
		FStaticMeshRenderData *StaticMeshRenderData = StaticMesh->GetRenderData();
		if (!StaticMeshRenderData->IsInitialized())
		{
			UE_LOG(LogTeleport, Warning, TEXT("StaticMeshRenderData Not ready"));
			return;
		}
		FStaticMeshLODResources &LODResources = StaticMeshRenderData->LODResources[0];

		FPositionVertexBuffer &PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
		FStaticMeshVertexBuffer &StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;

		uint32 pos_stride = PositionVertexBuffer.GetStride();
		const float *pos_data = (const float*)PositionVertexBuffer.GetVertexData();

		int numVertices = PositionVertexBuffer.GetNumVertices();
		for (int i = 0; i < numVertices; i++)
		{
			pos_data += pos_stride / sizeof(float);
		}
	}
}

avs::uid GeometrySource::AddTexture(UTexture* texture)
{
	avs::uid textureID;
	auto it = processedTextures.find(texture);

	if(it != processedTextures.end())
	{
		//Reuse the ID if this texture has been processed before, and return value
		textureID = it->second;
		return textureID;
	}
	else
	{
		//Create a new ID if this texture has never been processed.
		textureID = avs::GenerateUid();
		processedTextures[texture] = textureID;
	}

	avs::Texture newTexture;

	//Assuming the first running platform is the desired running platform.
	auto *rpd = texture->GetRunningPlatformData()[0];
	FTextureSource& textureSource = texture->Source;
	newTexture.compression=avs::TextureCompression::BASIS_COMPRESSED;
	newTexture.name = TCHAR_TO_ANSI(*texture->GetName());
	newTexture.width = textureSource.GetSizeX();
	newTexture.height = textureSource.GetSizeY();
	newTexture.depth = textureSource.GetVolumeSizeZ(); ///!!! Is this actually where Unreal stores its depth information for a texture? !!!
	newTexture.bytesPerPixel = textureSource.GetBytesPerPixel();
	newTexture.arrayCount = textureSource.GetNumSlices(); ///!!! Is this actually the array count? !!!
	newTexture.mipCount = textureSource.GetNumMips();
	newTexture.sampler_uid = 0;

	UE_CLOG(newTexture.bytesPerPixel != 4, LogTeleport, Warning, TEXT("Texture \"%s\" has bytes per pixel of %d!"), *texture->GetName(), newTexture.bytesPerPixel);

	switch(textureSource.GetFormat())
	{
		case ETextureSourceFormat::TSF_G8:
			newTexture.format = avs::TextureFormat::G8;
			break;
		case ETextureSourceFormat::TSF_BGRA8:
			newTexture.format = avs::TextureFormat::BGRA8;
			break;
		case ETextureSourceFormat::TSF_BGRE8:
			newTexture.format = avs::TextureFormat::BGRE8;
			break;
		case ETextureSourceFormat::TSF_RGBA16:
			newTexture.format = avs::TextureFormat::RGBA16;
			break;
		case ETextureSourceFormat::TSF_RGBA16F:
			newTexture.format = avs::TextureFormat::RGBA16F;
			break;
		case ETextureSourceFormat::TSF_RGBA8:
			newTexture.format = avs::TextureFormat::RGBA8;
			break;
		default:
			newTexture.format = avs::TextureFormat::INVALID;
			UE_LOG(LogTeleport, Warning, TEXT("Invalid texture format on texture: %s"), *texture->GetName());
			break;
	};
	size_t numMips = textureSource.GetNumMips();
	size_t numImages = numMips;
	// start with size of the image count.
	size_t dataSize = sizeof(uint16_t) + numMips * sizeof(uint32_t);
	uint32_t offset = dataSize;
	std::vector<uint32_t> imageOffsets(numImages);
	for (int m = 0; m < numMips; m++)
	{
		FTexture2DMipMap mip = rpd->Mips[m];
		imageOffsets[m]=offset;
		size_t imageDataSize = mip.SizeX * mip.SizeY * mip.SizeZ * newTexture.bytesPerPixel;
		offset += imageDataSize;
		dataSize += imageDataSize;
	}
	newTexture.data.resize(dataSize);
	uint8_t *target=newTexture.data.data();
	*((uint16_t *)target) = (uint16_t)numMips;
	target += sizeof(uint16_t);
	memcpy(target, imageOffsets.data(), numImages * sizeof(uint32_t));
	target += numImages * sizeof(uint32_t);
	for (int m = 0; m < numMips; m++)
	{
		FTexture2DMipMap mip = rpd->Mips[m];
		TArray64<uint8> mipData;
		textureSource.GetMipData(mipData, 0,0,m);

		size_t imageDataSize = mip.SizeX * mip.SizeY * mip.SizeZ * newTexture.bytesPerPixel;

		//Flip red and blue channels from BGR to RGB, eventually we will do this in a compute shader.
		for (uint32_t i = 0; i <imageDataSize; i += 4)
		{
			unsigned char red = mipData[i];
			target[i+0] = mipData[i+2];
			target[i+1] = mipData[i+1];
			target[i+2] = mipData[i+0];
			target[i+3] = mipData[i+3];
		}
		target += imageDataSize;
	}
	std::string basisFileLocation;

	FString GameSavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

	//Create a unique name based on the filepath.
	FString uniqueName = FPaths::ConvertRelativePathToFull(texture->AssetImportData->SourceData.SourceFiles[0].RelativeFilename);
	uniqueName = uniqueName.Replace(TEXT("/"), TEXT("#")); //Replaces slashes with hashes.
	uniqueName = uniqueName.RightChop(2); //Remove drive.
	uniqueName = uniqueName.Right(255); //Restrict name length.

	basisFileLocation = TCHAR_TO_ANSI(*FPaths::Combine(GameSavedDir, uniqueName + FString(".basis")));
	
	std::string path = ToStdString(GetResourcePath(texture));
	teleport::server::GeometryStore::GetInstance().storeTexture(textureID, "", path, GetAssetImportTimestamp(texture->AssetImportData), newTexture, false, false, false);

	return textureID;
}

void GeometrySource::GetDefaultTexture(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture)
{
	TArray<UTexture*> outTextures;
	materialInterface->GetTexturesInPropertyChain(propertyChain, outTextures, nullptr, nullptr);

	UTexture *texture = outTextures.Num() ? outTextures[0] : nullptr;
	
	if(texture)
	{
		outTexture = {AddTexture(texture), DUMMY_TEX_COORD};
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, float &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			if (expressionIndex >= outExpressions.Num())
			{
				LOG_MATERIAL_INTERFACE(materialInterface);
				return size_t(0);
			}
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("ConstantBiasScale"))
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}
			else if(name.Contains("Constant"))
			{
				outFactor = Cast<UMaterialExpressionConstant>(outExpressions[expressionIndex])->R;
			}
			else if(name.Contains("ScalarParameter"))
			{
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetScalarParameterValue(outExpressions[expressionIndex]->GetParameterName(), outFactor);
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, vec3 &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("Constant3Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant3Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B};
			}
			else if(name.Contains("VectorParameter"))
			{
				FLinearColor colour;
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetVectorParameterValue(outExpressions[expressionIndex]->GetParameterName(), colour);

				outFactor = {colour.R, colour.G, colour.B};
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, vec4 &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("Constant3Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant3Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else if(name.Contains("Constant4Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant4Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else if(name.Contains("VectorParameter"))
			{
				FLinearColor colour;
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetVectorParameterValue(outExpressions[expressionIndex]->GetParameterName(), colour);

				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

size_t GeometrySource::DecomposeTextureSampleExpression(UMaterialInterface* materialInterface, UMaterialExpressionTextureSample* textureSample, avs::TextureAccessor& outTexture)
{
	size_t subExpressionsHandled = 0;
	outTexture = {AddTexture(textureSample->Texture), DUMMY_TEX_COORD};

	//Extract tiling data for this texture.
	if(textureSample->Coordinates.Expression)
	{
		//Name of the coordinate expression.
		FString coordExpName = textureSample->Coordinates.Expression->GetName();

		if(coordExpName.Contains("Multiply"))
		{
			UMaterialExpressionMultiply* mulExp = Cast<UMaterialExpressionMultiply>(textureSample->Coordinates.Expression);
			UMaterialExpression* inputA = mulExp->A.Expression, * inputB = mulExp->B.Expression;

			if(inputA && inputB)
			{
				FString inputAName = mulExp->A.Expression->GetName(), inputBName = mulExp->B.Expression->GetName();

				//Swap, so A is texture coordinate, if B is texture coordinate.
				if(inputBName.Contains("TextureCoordinate"))
				{
					std::swap(inputA, inputB);
					std::swap(inputAName, inputBName);
				}

				if(inputAName.Contains("TextureCoordinate"))
				{
					bool isBSupported = true;
					float scalarValue = 0;

					if(inputBName.Contains("Constant"))
					{
						scalarValue = Cast<UMaterialExpressionConstant>(inputB)->R;
					}
					else if(inputBName.Contains("ScalarParameter"))
					{
						///INFO: Just using the parameter's name won't work for layered materials.
						materialInterface->GetScalarParameterValue(inputB->GetParameterName(), scalarValue);
					}
					else
					{
						isBSupported = false;
						LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, inputBName)
					}

					if(isBSupported)
					{
						UMaterialExpressionTextureCoordinate* texCoordExp = Cast<UMaterialExpressionTextureCoordinate>(inputA);
						outTexture.tiling = {texCoordExp->UTiling * scalarValue, texCoordExp->VTiling * scalarValue};
					}
				}
				else
				{
					UE_LOG(LogTeleport, Warning, TEXT("Material <%s> contains multiply expression <%s> with missing inputs."), *materialInterface->GetName(), *coordExpName)
				}
			}

			subExpressionsHandled += (inputA ? 1 : 0) + (inputB ? 1 : 0); //Handled multiplication inputs.
		}
		else if(coordExpName.Contains("TextureCoordinate"))
		{
			UMaterialExpressionTextureCoordinate* texCoordExp = Cast<UMaterialExpressionTextureCoordinate>(textureSample->Coordinates.Expression);
			outTexture.tiling = {texCoordExp->UTiling, texCoordExp->VTiling};
		}
		else
		{
			LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, coordExpName)
		}

		++subExpressionsHandled; //Handled UV expression.
	}

	return subExpressionsHandled;
}

int64 GeometrySource::GetAssetImportTimestamp(UAssetImportData* importData)
{
	check(importData);
	
	return (!importData || importData->SourceData.SourceFiles.Num() == 0 ? 0 : importData->SourceData.SourceFiles[0].Timestamp.ToUnixTimestamp());
}
