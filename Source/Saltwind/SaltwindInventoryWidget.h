#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class ASaltwindPlayerController;
class USaltwindItemDefinition;
class UTexture2D;

class SSaltwindInventory : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SSaltwindInventory) {}
        SLATE_ARGUMENT(ASaltwindPlayerController*, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void OnMouseLeave(const FPointerEvent& Event) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

protected:
    virtual bool ComputeVolatility() const override { return true; }

private:
    friend class FSaltwindCollectionStep;
    friend class FSaltwindCraftingStep;

    enum class EDragSource : uint8 { Inventory, Ingredient, Product };

    struct FLayout
    {
        float Scale;
        FVector2D Origin;
        FVector2D PanelSize;
        FVector2D ScreenSize;
        FVector2D CraftOrigin;
        int32 VisibleRows;
        bool bCrafting;
        bool IsInPanel(FVector2D Position) const;
        bool IsInInventory(FVector2D Position) const;
        bool IsInCrafting(FVector2D Position) const;
        FVector2D TilePosition(int32 Index) const;
        FVector2D IngredientPosition(int32 Index) const;
        FVector2D ProductPosition() const;
        int32 IngredientAt(FVector2D Position) const;
        bool IsOnProduct(FVector2D Position) const;
        bool IsOnClose(FVector2D Position) const;
    };

    FLayout Layout(const FGeometry& Geometry) const;
    int32 SlotAt(const FLayout& Layout, FVector2D Position) const;
    int32 GetScrollLimit() const;
    void ClearDrag();
    const FSlateBrush* Icon(UTexture2D* Texture) const;

    TWeakObjectPtr<ASaltwindPlayerController> Controller;
    TWeakObjectPtr<USaltwindItemDefinition> DraggedItem;
    mutable TMap<UTexture2D*, TUniquePtr<FSlateBrush>> Icons;
    FVector2D Pointer = FVector2D(-1000, -1000);
    FVector2D DragStart = FVector2D::ZeroVector;
    int32 DragIndex = INDEX_NONE;
    int32 DragQuantity = 0;
    int32 FirstRow = 0;
    EDragSource DragSource = EDragSource::Inventory;
    bool bDragging = false;
};
