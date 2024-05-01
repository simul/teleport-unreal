// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamableNodeDetailCustomizations.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "StreamableNodeDetailCustomization"
FStreamableNodeDetailCustomization::FStreamableNodeDetailCustomization()
{
	//std::cout<<"FStreamableNodeDetailCustomization\n";
}
template<typename ClassToCustomize>
TArray<ClassToCustomize*> GetCustomizedObjects(const TArray<TWeakObjectPtr<UObject>>& InObjectsBeingCustomized)
{
	TArray<ClassToCustomize*> CustomizedObjects;
	CustomizedObjects.Reserve(InObjectsBeingCustomized.Num());

	auto IsOfCustomType = [](TWeakObjectPtr<UObject> InObject) { return InObject.Get() && InObject->IsA<ClassToCustomize>(); };
	auto CastAsCustomType = [](TWeakObjectPtr<UObject> InObject) { return Cast<ClassToCustomize>(InObject); };
	Algo::TransformIf(InObjectsBeingCustomized, CustomizedObjects, IsOfCustomType, CastAsCustomType);
	
	return CustomizedObjects;
}

void FStreamableNodeDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized = DetailBuilder.GetSelectedObjects();
	ObjectsBeingCustomized.RemoveAll( [](const TWeakObjectPtr<UObject>& InObject) { return !InObject.IsValid(); } );

	// if no selected objects
	if (ObjectsBeingCustomized.IsEmpty())
	{
		return;
	}
	
	// make sure all types are of the same class
	const UClass* DetailsClass = ObjectsBeingCustomized[0]->GetClass();
	const int32 NumObjects = ObjectsBeingCustomized.Num();
	for (int32 Index = 1; Index < NumObjects; Index++)
	{
		if (ObjectsBeingCustomized[Index]->GetClass() != DetailsClass)
		{
			// multiple different things - fallback to default details panel behavior
			return;
		}
	}

	// assuming the classes are all the same
	if (ObjectsBeingCustomized[0]->IsA<UStreamableNode>())
	{
		return CustomizeDetailsForClass<UStreamableNode>(DetailBuilder, ObjectsBeingCustomized);
	}
	if(ObjectsBeingCustomized[0]->IsA<UStreamableRootComponent>())
	{
		return CustomizeDetailsForClass<UStreamableRootComponent>(DetailBuilder,ObjectsBeingCustomized);
	}
	if(ObjectsBeingCustomized[0]->IsA<USceneComponent>())
	{
		return CustomizeDetailsForSceneComponent(DetailBuilder,ObjectsBeingCustomized);
	}
}

template <>
void FStreamableNodeDetailCustomization::CustomizeDetailsForClass<UStreamableNode>(
	IDetailLayoutBuilder& DetailBuilder,
	const TArray<TWeakObjectPtr<UObject>>& InObjectsBeingCustomized)
{
	const TArray<UStreamableNode*> Nodes = GetCustomizedObjects<UStreamableNode>(InObjectsBeingCustomized);
	if (Nodes.IsEmpty())
	{
		return;
	}
	auto& TestCategory=DetailBuilder.EditCategory(FName("StreamableNode"));
	//SpriteCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UStreamableNode,PixelsPerUnrealUnit));

}

template <>
void FStreamableNodeDetailCustomization::CustomizeDetailsForClass<UStreamableRootComponent>(
	IDetailLayoutBuilder& DetailBuilder,
	const TArray<TWeakObjectPtr<UObject>>& InObjectsBeingCustomized)
{
	const TArray<UStreamableRootComponent*> roots=GetCustomizedObjects<UStreamableRootComponent>(InObjectsBeingCustomized);
	if(roots.IsEmpty())
	{
		return;
	}
	IDetailCategoryBuilder& TestCategory=DetailBuilder.EditCategory(FName("StreamableRootComponent"));
	for(auto *r:roots)
	{
		r->InitializeStreamableNodes();
		//TestCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UStreamableRootComponent,Nodes));
		for(TObjectPtr<UStreamableNode> n:r->Nodes)
		{
			UStreamableNode *N=n.Get();
			FText Node=LOCTEXT("Node","Signing Node");
			FText uidtxt=FText::AsCultureInvariant(n->GetUidString());
			USceneComponent*sc=N->GetSceneComponent();
			FText scenetxt=FText::AsCultureInvariant(sc->GetName());
			FText datatxt;
			UStaticMeshComponent *m=N->GetMesh();
			if(m)
			{
				UStaticMesh *staticMesh=m->GetStaticMesh();
				datatxt=FText::AsCultureInvariant(staticMesh->GetName());
			}
			TestCategory.AddCustomRow(Node,false)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
						.Padding(4.0f,0.0f)
						.MaxWidth(250)
						[
							SNew(STextBlock)
							//.Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
							.Text(scenetxt)
						]
					+SHorizontalBox::Slot()
						.Padding(4.0f,0.0f)
						.MaxWidth(50)
						[
							SNew(STextBlock)
							//.Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
							.Text(uidtxt)
						]
					+SHorizontalBox::Slot()
						.Padding(4.0f,0.0f)
						.MaxWidth(250)
						[
							SNew(STextBlock)
							//.Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
						.Text(datatxt)
						]
					];
		}
	}
}

void FStreamableNodeDetailCustomization::CustomizeDetailsForSceneComponent(IDetailLayoutBuilder& DetailBuilder,const TArray<TWeakObjectPtr<UObject>>& InObjectsBeingCustomized)
{
	const TArray<USceneComponent*> scs=GetCustomizedObjects<USceneComponent>(InObjectsBeingCustomized);
	if(scs.IsEmpty())
	{
		return;
	}
	for(USceneComponent *s:scs)
	{
		AActor *A=s->GetOwner();
		if(!A)
			continue;
		UStreamableRootComponent* streamableRoot=A->FindComponentByClass<UStreamableRootComponent>();
		if(!streamableRoot)
			continue;
		IDetailCategoryBuilder& TestCategory=DetailBuilder.EditCategory(FName("StreamableRootComponent"));
		TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> nodes=streamableRoot->GetStreamableNodes();
		auto nn=nodes.Find(s);
		if(nn)
			continue;
		TWeakObjectPtr<UStreamableNode> n=*nn;
		UStreamableNode *N=n.Get();
		FText Node=LOCTEXT("Node","Node");
		FText uidtxt=FText::AsCultureInvariant(N->GetUidString());
		FText scenetxt=FText::AsCultureInvariant(N->GetSceneComponent()->GetName());
		TestCategory.AddCustomRow(Node,false)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
					.Padding(4.0f,0.0f)
					.MaxWidth(50)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular",14))
					.Text(scenetxt)
					]
				+SHorizontalBox::Slot()
					.Padding(4.0f,0.0f)
					.MaxWidth(50)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular",14))
					.Text(uidtxt)
					]
			];
	}
}


#undef LOCTEXT_NAMESPACE
