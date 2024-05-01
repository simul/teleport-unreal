#pragma once

#include "ComponentVisualizer.h"
#include "Components/StreamableRootComponent.h"
#include "Framework/Commands/UICommandList.h"

class FTeleportStreamableRootComponentVisualizer : public FComponentVisualizer
{
public:
	FTeleportStreamableRootComponentVisualizer();
	virtual ~FTeleportStreamableRootComponentVisualizer();

    // Begin FComponentVisualizer interface
    virtual void OnRegister() override;
	virtual void DrawVisualization(const UActorComponent *Component, const FSceneView *View, FPrimitiveDrawInterface *PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent *Component, const FViewport *Viewport, const FSceneView *View, FCanvas *Canvas) override;
	/*virtual bool VisProxyHandleClick(FEditorViewportClient *InViewportClient, HComponentVisProxy *VisProxy, const FViewportClick &Click) override;
    virtual void EndEditing() override;
    virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
    virtual bool GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const override;
    virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
    virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual TSharedPtr<SWidget> GenerateContextMenu() const override;
	virtual bool IsVisualizingArchetype();

    // End FComponentVisualizer interface

    // Get the target component we are currently editing
	UStreamableRootComponent *GetEditedStreamableRootComponent() const;*/

private:

    /**Output log commands*/
	TSharedPtr<FUICommandList> StreamableRootComponentVisualizerActions;
};
