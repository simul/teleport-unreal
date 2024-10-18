#pragma once

#include "Visualizers/TeleportStreamableRootComponentVisualizer.h"
#include "Components/StreamableNode.h"
#include "Components/StreamableRootComponent.h"
#include "Components/SceneComponent.h"
#include "SceneManagement.h"
 
FTeleportStreamableRootComponentVisualizer::FTeleportStreamableRootComponentVisualizer()
{

}

FTeleportStreamableRootComponentVisualizer::~FTeleportStreamableRootComponentVisualizer()
{

}


// Begin FComponentVisualizer interface
void FTeleportStreamableRootComponentVisualizer::OnRegister()
{

}

void FTeleportStreamableRootComponentVisualizer::DrawVisualization(const UActorComponent *Component, const FSceneView *View, FPrimitiveDrawInterface *PDI)
{
	// cast the component into the expected component type
	if (const UStreamableRootComponent *StreamableRootComponent = Cast<UStreamableRootComponent>(Component))
	{
		// get colors for selected and unselected targets
		// This is an editor only uproperty of our targeting component, that way we can change the colors if we can't see them against the background
		const FLinearColor node_colour = {1.f,0.5f,1.f,1.f};
		const FLinearColor connector_colour = {0.3f, .3f, 0.3f, 1.f};

		FTransform transform = Component->GetOwner()->GetActorTransform();

		// convert offset from cm to metres.
		FVector root_pos = transform.GetTranslation();
		FQuat root_rot = transform.GetRotation();
		const FVector s = transform.GetScale3D();

		// Iterate over each target drawing a line and dot
		{
			FLinearColor Color = connector_colour;

			// Set our hit proxy
			//PDI->SetHitProxy(new HTargetProxy(Component, i));
			PDI->DrawLine(root_pos, root_pos + FVector(0, 0, 10.0f), Color, SDPG_Foreground);
			PDI->DrawPoint(root_pos, node_colour, 120.f, SDPG_Foreground);
			FVector SphereCenter(0.0f, 0.0f, 100.0f);
			float SphereRadius = 50.0f;
			FColor SphereColor = FColor::Blue;
			int32 SphereSegments = 12;
			//PDI->DrawSphere(Location, FVector(SphereRadius), SphereSegments, SphereSegments, SphereColor, SDPG_World);
			//PDI->SetHitProxy(NULL);
			const TMap<USceneComponent*,TWeakObjectPtr<UStreamableNode>> &StreamableNodes = StreamableRootComponent->GetStreamableNodes();
			for(auto i:StreamableNodes)
			{
				USceneComponent *c=i.Key;
				FTransform ntransform = c->GetComponentTransform();
				FVector node_pos = ntransform.GetTranslation();
				FQuat node_rot = ntransform.GetRotation();
				USceneComponent *p=c->GetAttachParent();
				if(p)
				{
					FVector parent_pos = p->GetComponentTransform().GetTranslation();
					PDI->DrawLine(parent_pos, node_pos, connector_colour, SDPG_Foreground);
				}
				PDI->DrawPoint(node_pos, node_colour, 50.f, SDPG_Foreground);
			}
		}
	}
}
void FTeleportStreamableRootComponentVisualizer::DrawVisualizationHUD(const UActorComponent *Component, const FViewport *Viewport, const FSceneView *View, FCanvas *Canvas)
{
}

#if 0
bool FTeleportStreamableRootComponentVisualizer::VisProxyHandleClick(FEditorViewportClient *InViewportClient, HComponentVisProxy *VisProxy, const FViewportClick &Click)
{
	return false;
}

void FTeleportStreamableRootComponentVisualizer::EndEditing()
{

}

bool FTeleportStreamableRootComponentVisualizer::GetWidgetLocation(const FEditorViewportClient *ViewportClient, FVector &OutLocation) const
{
return false;
}

bool FTeleportStreamableRootComponentVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient *ViewportClient, FMatrix &OutMatrix) const
{
return false;
}

bool FTeleportStreamableRootComponentVisualizer::FTeleportStreamableRootComponentVisualizer::HandleInputDelta(FEditorViewportClient *ViewportClient, FViewport *Viewport, FVector &DeltaTranslate, FRotator &DeltaRotate, FVector &DeltaScale)
{
return false;
}

bool FTeleportStreamableRootComponentVisualizer::HandleInputKey(FEditorViewportClient *ViewportClient, FViewport *Viewport, FKey Key, EInputEvent Event)
{
return false;
}

TSharedPtr<SWidget> FTeleportStreamableRootComponentVisualizer::GenerateContextMenu() const
{
return nullptr;
}

bool FTeleportStreamableRootComponentVisualizer::IsVisualizingArchetype()
{
return false;
}


// End FComponentVisualizer interface

/** Get the target component we are currently editing */
UStreamableRootComponent *FTeleportStreamableRootComponentVisualizer::GetEditedStreamableRootComponent() const
{
	return nullptr;
}
#endif