// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "ScopedTransaction.h"
#include "Components/StreamableNode.h"
#include "Components/StreamableRootComponent.h"

class FStreamableNodeDetailCustomization : public IDetailCustomization
{
public:
	FStreamableNodeDetailCustomization();
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FStreamableNodeDetailCustomization);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
 
	/** Per class customization */
	template<typename ClassToCustomize>
	void CustomizeDetailsForClass(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UObject>>& ObjectsBeingCustomized) {}

	/** Template specializations */
	template<>
	void CustomizeDetailsForClass<UStreamableNode>(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UObject>>& ObjectsBeingCustomized);

	/** Template specializations */
	template<>
	void CustomizeDetailsForClass<UStreamableRootComponent>(IDetailLayoutBuilder& DetailBuilder,const TArray<TWeakObjectPtr<UObject>>& ObjectsBeingCustomized);

	void CustomizeDetailsForSceneComponent(IDetailLayoutBuilder& DetailBuilder,const TArray<TWeakObjectPtr<UObject>>& ObjectsBeingCustomized);

	/** Transaction to handle value changed. It's being effective once the value committed */
	TSharedPtr<FScopedTransaction> ValueChangedTransaction;
};