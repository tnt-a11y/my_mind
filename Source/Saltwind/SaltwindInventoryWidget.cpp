#include "SaltwindInventoryWidget.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "SaltwindPlayerController.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SaltwindInventoryWidget"

namespace
{
    constexpr int32 Columns = 4;
    constexpr int32 Rows = 3;
    constexpr double GridX = 23.0;
    constexpr double GridY = 112.0;
    constexpr double StepX = 126.0;
    constexpr double StepY = 119.0;
    const FVector2D CellSize(116, 109);
    const FVector2D CraftPanelSize(370, 432);
    const FVector2D IngredientSize(88, 83);
    const FVector2D ProductSize(107, 108);
    const FLinearColor Ink(.83f, .88f, .82f);
    const FLinearColor Muted(.43f, .55f, .48f);
    const FLinearColor Brass(.82f, .64f, .34f);
    const FSlateRoundedBoxBrush PanelBrush(FLinearColor(.015f, .029f, .025f, .97f), 19.0f,
        FLinearColor(.14f, .22f, .169f, .96f), 1.0f);
    const FSlateRoundedBoxBrush SlotBrush(FLinearColor(.029f, .050f, .042f, 1), 11.0f,
        FLinearColor(.10f, .16f, .127f, .92f), 1.0f);
    const FSlateRoundedBoxBrush EmptyBrush(FLinearColor(.017f, .035f, .029f, .80f), 11.0f,
        FLinearColor(.063f, .103f, .081f, .86f), 1.0f);
    const FSlateRoundedBoxBrush ActiveBrush(FLinearColor(.060f, .079f, .055f, .98f), 11.0f,
        FLinearColor(.59f, .43f, .225f, 1), 1.0f);
    const FSlateRoundedBoxBrush BadgeBrush(FLinearColor(.061f, .085f, .066f, .96f), 7.0f,
        FLinearColor(.19f, .25f, .17f, 1), 1.0f);

    bool InRect(FVector2D Point, FVector2D Origin, FVector2D Size)
    {
        const FVector2D Local = Point - Origin;
        return Local.X >= 0.0 && Local.Y >= 0.0 && Local.X < Size.X && Local.Y < Size.Y;
    }
}

void SSaltwindInventory::Construct(const FArguments& Args)
{
    Controller = Args._Controller;
}

FVector2D SSaltwindInventory::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(560, 520);
}

SSaltwindInventory::FLayout SSaltwindInventory::Layout(const FGeometry& Geometry) const
{
    const FVector2D View = Geometry.GetLocalSize();
    const ASaltwindPlayerController* Player = Controller.Get();
    const bool bCrafting = Player && Player->IsCraftingOpen();
    const float Scale = FMath::Clamp(float(FMath::Min(View.X / (bCrafting ? 1150.0 : 820.0), View.Y / 540.0)), .23f, 1.08f);
    const FVector2D Screen = View / Scale;
    const int32 SlotCount = Player ? Player->GetInventory().Num() : 0;
    const int32 VisibleRows = FMath::Clamp(FMath::DivideAndRoundUp(SlotCount + 1, Columns), 2, Rows);
    const FVector2D Panel(552, 551 - (Rows - VisibleRows) * StepY);
    const FVector2D Origin(Screen.X - Panel.X - 32, (Screen.Y - Panel.Y) * .5);
    const FVector2D CraftOrigin(Origin.X - CraftPanelSize.X - 16, (Screen.Y - CraftPanelSize.Y) * .5);
    return {Scale, Origin, Panel, Screen, CraftOrigin, VisibleRows, bCrafting};
}

bool SSaltwindInventory::FLayout::IsInPanel(FVector2D Position) const
{
    return IsInInventory(Position) || IsInCrafting(Position);
}

bool SSaltwindInventory::FLayout::IsInInventory(FVector2D Position) const
{
    return InRect(Position, Origin, PanelSize);
}

bool SSaltwindInventory::FLayout::IsInCrafting(FVector2D Position) const
{
    return bCrafting && InRect(Position, CraftOrigin, CraftPanelSize);
}

FVector2D SSaltwindInventory::FLayout::IngredientPosition(int32 Index) const
{
    return CraftOrigin + FVector2D(23 + 96 * (Index % 2), 147 + 93 * (Index / 2));
}

FVector2D SSaltwindInventory::FLayout::ProductPosition() const
{
    return CraftOrigin + FVector2D(239, 183);
}

int32 SSaltwindInventory::FLayout::IngredientAt(FVector2D Position) const
{
    if (bCrafting)
    {
        for (int32 Slot = 0; Slot < ASaltwindPlayerController::CraftCapacity; ++Slot)
        {
            if (InRect(Position, IngredientPosition(Slot), IngredientSize))
            {
                return Slot;
            }
        }
    }
    return INDEX_NONE;
}

bool SSaltwindInventory::FLayout::IsOnProduct(FVector2D Position) const
{
    return bCrafting && InRect(Position, ProductPosition(), ProductSize);
}

FVector2D SSaltwindInventory::FLayout::TilePosition(int32 Index) const
{
    return Origin + FVector2D(GridX + (Index % Columns) * StepX, GridY + (Index / Columns) * StepY);
}

bool SSaltwindInventory::FLayout::IsOnClose(FVector2D Position) const
{
    return InRect(Position, Origin + FVector2D(465, 23), FVector2D(62, 34));
}

int32 SSaltwindInventory::GetScrollLimit() const
{
    const ASaltwindPlayerController* Player = Controller.Get();
    const int32 Used = Player ? Player->GetInventory().Num() : 0;
    return FMath::Max(0, FMath::DivideAndRoundUp(Used + 1, Columns) - Rows);
}

int32 SSaltwindInventory::SlotAt(const FLayout& Grid, FVector2D Position) const
{
    for (int32 Tile = 0; Tile < Grid.VisibleRows * Columns; ++Tile)
    {
        if (InRect(Position, Grid.TilePosition(Tile), CellSize))
        {
            return Tile + FMath::Min(FirstRow, GetScrollLimit()) * Columns;
        }
    }
    return INDEX_NONE;
}

const FSlateBrush* SSaltwindInventory::Icon(UTexture2D* Texture) const
{
    TUniquePtr<FSlateBrush>& Brush = Icons.FindOrAdd(Texture);
    if (!Brush)
    {
        Brush = MakeUnique<FSlateBrush>();
        Brush->DrawAs = ESlateBrushDrawType::Image;
        Brush->SetResourceObject(Texture);
    }
    return Brush.Get();
}

int32 SSaltwindInventory::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
    FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const ASaltwindPlayerController* Player = Controller.Get();
    if (!Player)
    {
        return Layer;
    }
    const FLayout Grid = Layout(Geometry);
    const FVector2D Origin = Grid.Origin;
    const FSlateBrush* Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const auto Place = [&Geometry, &Grid](FVector2D Position, FVector2D Size)
    {
        return Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(Grid.Scale, FVector2f(Position * Grid.Scale)));
    };
    const auto Box = [&Out, &Place, &Style](int32 Level, FVector2D Position, FVector2D Size, const FSlateBrush* Brush,
                                   FLinearColor Tint = FLinearColor::White)
    {
        FSlateDrawElement::MakeBox(Out, Level, Place(Position, Size), Brush, ESlateDrawEffect::None, Tint * Brush->GetTint(Style));
    };
    const auto Text = [&Out, &Place](int32 Level, FVector2D Position, const FText& Label, int32 Size,
                                     const FLinearColor& Tint, bool bBold = false, bool bRight = false)
    {
        const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
        if (bRight)
        {
            Position.X -= FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label, Font).X;
        }
        FSlateDrawElement::MakeText(Out, Level, Place(Position, FVector2D(540, Size + 8)), Label, Font,
                                    ESlateDrawEffect::None, Tint);
    };

    Box(Layer, FVector2D::ZeroVector, Grid.ScreenSize, Solid, FLinearColor(.005f,.010f,.007f,.12f));
    Box(Layer + 1, Origin + FVector2D(7, 10), Grid.PanelSize, &PanelBrush, FLinearColor(.30f,.30f,.30f,.67f));
    Box(Layer + 2, Origin, Grid.PanelSize, &PanelBrush);
    Box(Layer + 3, Origin + FVector2D(24, 34), FVector2D(4, 33), Solid, Brass);
    Text(Layer + 4, Origin + FVector2D(40, 28), LOCTEXT("InventoryTitle", "Inventory"), 34, Ink, true);
    const FText ItemCount = FText::Format(LOCTEXT("ItemCount", "{0} {0}|plural(one=item,other=items)"), Player->GetItemCount());
    Text(Layer + 4, Origin + FVector2D(524, 68), ItemCount, 14, Brass, true, true);
    Box(Layer + 3, Origin + FVector2D(465, 23), FVector2D(62, 34), Grid.IsOnClose(Pointer) ? &ActiveBrush : &BadgeBrush);
    Text(Layer + 4, Origin + FVector2D(482, 31), LOCTEXT("Esc", "Esc"), 14, Ink, true);
    Box(Layer + 3, Origin + FVector2D(23, 96), FVector2D(504, 1), Solid, FLinearColor(.11f,.17f,.133f,1));

    const TArray<FSaltwindInventorySlot>& Slots = Player->GetInventory();
    const int32 ScrollLimit = GetScrollLimit();
    const int32 Row = FMath::Min(FirstRow, ScrollLimit);
    const int32 Hover = SlotAt(Grid, Pointer);
    for (int32 Tile = 0; Tile < Grid.VisibleRows * Columns; ++Tile)
    {
        const int32 Index = Tile + Row * Columns;
        const FVector2D Cell = Grid.TilePosition(Tile);
        const FSaltwindInventorySlot* Slot = Slots.IsValidIndex(Index) ? &Slots[Index] : nullptr;
        const bool bSelected = bDragging && DragSource == EDragSource::Inventory && DragIndex == Index;
        Box(Layer + 3, Cell, CellSize, Slot ? ((Index == Hover || bSelected) ? &ActiveBrush : &SlotBrush) : &EmptyBrush);
        if (!Slot || !Slot->Item)
        {
            continue;
        }
        if (UTexture2D* Thumbnail = Slot->Item->Thumbnail)
        {
            const FLinearColor Tint(1, 1, 1, bSelected ? .34f : 1);
            Box(Layer + 4, Cell + FVector2D(2, 1), FVector2D(112, 81), Icon(Thumbnail), Tint);
        }
        Box(Layer + 5, Cell + FVector2D(80, 8), FVector2D(28, 23), &BadgeBrush);
        const int32 Reserved = Grid.IngredientAt(Pointer) != INDEX_NONE ? 1 : DragQuantity;
        const FText Count = FText::AsNumber(bSelected ? Slot->Count - Reserved : Slot->Count);
        Text(Layer + 6, Cell + FVector2D(102, 11), Count, 13, Brass, true, true);
        Text(Layer + 5, Cell + FVector2D(9, 83), Slot->Item->DisplayName, 13, Ink, true);
    }
    if (Slots.IsEmpty())
    {
        const FVector2D Callout = Origin + FVector2D(74, 179);
        Box(Layer + 4, Callout, FVector2D(404, 92), &PanelBrush);
        Text(Layer + 5, Callout + FVector2D(24, 17), LOCTEXT("EmptyTitle", "Your inventory is empty"), 21, Ink, true);
        Text(Layer + 5, Callout + FVector2D(24, 53), LOCTEXT("EmptyHint", "Press E near a collectible to pick it up."), 13, Muted);
    }
    if (ScrollLimit > 0)
    {
        const double Height = 344;
        const double Thumb = FMath::Max(34.0, Height * Rows / (Rows + ScrollLimit));
        const FVector2D Rail = Origin + FVector2D(530, GridY);
        Box(Layer + 3, Rail, FVector2D(3, Height), Solid, Muted.CopyWithNewOpacity(.25f));
        Box(Layer + 4, Rail + FVector2D(0, (Height - Thumb) * Row / ScrollLimit), FVector2D(3, Thumb), Solid, Brass);
    }

    const double Footer = Grid.PanelSize.Y - 77;
    Box(Layer + 3, Origin + FVector2D(23, Footer), FVector2D(504, 1), Solid, FLinearColor(.11f,.17f,.133f,1));
    Text(Layer + 4, Origin + FVector2D(24, Footer + 11), LOCTEXT("DropHint", "Drag into the scene to drop a stack"), 13, Ink);
    Text(Layer + 4, Origin + FVector2D(526, Footer + 11), LOCTEXT("SplitHint", "Shift-drag to drop one"), 12, Muted, false, true);
    const FText Notice = Player->GetNotice();
    if (!Notice.IsEmpty())
    {
        Text(Layer + 4, Origin + FVector2D(24, Footer + 41), Notice, 13, Brass, true);
    }
    if (Grid.bCrafting)
    {
        const FVector2D Craft = Grid.CraftOrigin;
        const auto& Ingredients = Player->GetIngredients();
        const USaltwindItemDefinition* Ready = Player->GetCraftProduct();
        int32 Wood = 0;
        int32 Stone = 0;
        for (const FSaltwindInventorySlot& Entry : Ingredients)
        {
            Wood += Entry.Item && Entry.Item->Kind == ESaltwindItemKind::Wood;
            Stone += Entry.Item && Entry.Item->Kind == ESaltwindItemKind::Stone;
        }
        Box(Layer + 1, Craft + FVector2D(7,10), CraftPanelSize, &PanelBrush, FLinearColor(.30f,.30f,.30f,.67f));
        Box(Layer + 2, Craft, CraftPanelSize, &PanelBrush);
        Box(Layer + 3, Craft + FVector2D(24,34), FVector2D(4,33), Solid, Brass);
        Text(Layer + 4, Craft + FVector2D(40,28), LOCTEXT("CraftTitle", "Crafting"), 34, Ink, true);
        Box(Layer + 3, Craft + FVector2D(305,23), FVector2D(41,34), &BadgeBrush);
        Text(Layer + 4, Craft + FVector2D(318,31), LOCTEXT("CraftKey", "Q"), 14, Brass, true);
        Box(Layer + 3, Craft + FVector2D(23,87), FVector2D(324,1), Solid, Muted.CopyWithNewOpacity(.43f));
        Text(Layer + 4, Craft + FVector2D(23,103), FText::Format(LOCTEXT("WoodProgress", "Wood {0}/2"), Wood), 14, Wood == 2 ? Brass : Ink, true);
        Text(Layer + 4, Craft + FVector2D(119,103), FText::Format(LOCTEXT("StoneProgress", "Stone {0}/2"), Stone), 14, Stone == 2 ? Brass : Ink, true);
        Text(Layer + 4, Craft + FVector2D(240,155), LOCTEXT("ProductLabel", "Product"), 13, Muted, true);
        Text(Layer + 4, Craft + FVector2D(214,220), LOCTEXT("CraftArrow", "›"), 23, Brass, true);
        for (int32 Index = 0; Index < Ingredients.Num(); ++Index)
        {
            const FVector2D Cell = Grid.IngredientPosition(Index);
            const USaltwindItemDefinition* Material = Ingredients[Index].Item.Get();
            const bool bTarget = Grid.IngredientAt(Pointer) == Index;
            Box(Layer + 3, Cell, IngredientSize, bTarget ? &ActiveBrush : (Material ? &SlotBrush : &EmptyBrush));
            if (Material)
            {
                if (UTexture2D* Thumbnail = Material->Thumbnail)
                {
                    Box(Layer + 4, Cell + FVector2D(3,1), FVector2D(82,59), Icon(Thumbnail));
                }
                Text(Layer + 5, Cell + FVector2D(7,60), Material->DisplayName, 11, Ink, true);
            }
            else
            {
                Text(Layer + 4, Cell + FVector2D(35,24), LOCTEXT("EmptyIngredient", "+"), 21, Muted);
            }
        }
        const FVector2D Result = Grid.ProductPosition();
        Box(Layer + 3, Result, ProductSize, Ready ? &ActiveBrush : &EmptyBrush);
        if (const USaltwindItemDefinition* Recipe = Player->GetRecipeProduct())
        {
            if (UTexture2D* Thumbnail = Recipe->Thumbnail)
            {
                Box(Layer + 4, Result + FVector2D(2,0), FVector2D(103,78), Icon(Thumbnail), FLinearColor(1,1,1,Ready ? 1 : .26f));
            }
            Text(Layer + 5, Result + FVector2D(10,80), Recipe->DisplayName, 13, Ready ? Brass : Muted, true);
        }
        Box(Layer + 3, Craft + FVector2D(23,343), FVector2D(324,1), Solid, Muted.CopyWithNewOpacity(.43f));
        Text(Layer + 4, Craft + FVector2D(23,359), Ready ? LOCTEXT("CraftReady", "Campfire ready")
             : LOCTEXT("CraftAdd", "Drag one piece into each ingredient slot"), 14, Ready ? Brass : Ink, true);
        Text(Layer + 4, Craft + FVector2D(23,390), Ready ? LOCTEXT("CraftDestination", "Drag it to the inventory or into the scene.")
             : LOCTEXT("CraftReturn", "Drag a piece back to the inventory to remove it."), 12, Muted);
    }
    if (const USaltwindItemDefinition* Item = bDragging ? DraggedItem.Get() : nullptr)
    {
        const bool bOverWorld = !Grid.IsInPanel(Pointer) && DragSource != EDragSource::Ingredient;
        const bool bMaterialTarget = DragSource == EDragSource::Inventory && Grid.IngredientAt(Pointer) != INDEX_NONE;
        FText TargetHint = LOCTEXT("DragOutside", "Move the stack out of the inventory");
        if (DragSource == EDragSource::Product)
        {
            TargetHint = bOverWorld ? LOCTEXT("ReleaseCampfire", "Release to place the lit campfire")
                                    : LOCTEXT("StoreCampfire", "Release over the inventory to store it");
        }
        else if (DragSource == EDragSource::Ingredient)
        {
            TargetHint = LOCTEXT("ReturnMaterial", "Release over the inventory to return it");
        }
        else if (bMaterialTarget)
        {
            TargetHint = LOCTEXT("AddMaterial", "Release to add one ingredient");
        }
        else if (bOverWorld)
        {
            TargetHint = LOCTEXT("Release", "Release to drop in front of you");
        }
        const FVector2D DropZone(32, Grid.ScreenSize.Y - 111);
        Box(Layer + 7, DropZone, FVector2D(310, 59), bOverWorld ? &ActiveBrush : &PanelBrush);
        Text(Layer + 8, DropZone + FVector2D(17, 19), TargetHint, 14, bOverWorld ? Brass : Ink, true);
        const FVector2D Ghost = Pointer + FVector2D(16, 17);
        Box(Layer + 9, Ghost, CellSize, &ActiveBrush);
        if (UTexture2D* Thumbnail = Item->Thumbnail)
        {
            Box(Layer + 10, Ghost + FVector2D(2, 1), FVector2D(112, 81), Icon(Thumbnail));
        }
        Box(Layer + 11, Ghost + FVector2D(80, 8), FVector2D(28, 23), &BadgeBrush);
        Text(Layer + 12, Ghost + FVector2D(102, 11), FText::AsNumber(bMaterialTarget ? 1 : DragQuantity), 13, Brass, true, true);
        Text(Layer + 11, Ghost + FVector2D(9, 83), Item->DisplayName, 13, Ink, true);
    }
    return Layer + 12;
}

FReply SSaltwindInventory::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    ASaltwindPlayerController* Player = Controller.Get();
    if (!Player || Event.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Handled();
    }
    const FLayout Grid = Layout(Geometry);
    Pointer = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) / Grid.Scale;
    if (Grid.IsOnClose(Pointer))
    {
        Player->ToggleInventory();
        return FReply::Handled();
    }
    if (Grid.bCrafting && InRect(Pointer, Grid.CraftOrigin + FVector2D(305,23), FVector2D(41,34)))
    {
        Player->ToggleCrafting();
        return FReply::Handled();
    }
    if (Grid.IsOnProduct(Pointer) && Player->GetCraftProduct())
    {
        DragSource = EDragSource::Product;
        DragIndex = 0;
        DraggedItem = Player->GetCraftProduct();
        DragQuantity = 1;
    }
    else if (const int32 Cell = Grid.IngredientAt(Pointer); Player->GetIngredients().IsValidIndex(Cell)
             && Player->GetIngredients()[Cell].Item)
    {
        DragSource = EDragSource::Ingredient;
        DragIndex = Cell;
        DraggedItem = Player->GetIngredients()[Cell].Item;
        DragQuantity = 1;
    }
    else
    {
        const int32 Index = SlotAt(Grid, Pointer);
        const auto& Slots = Player->GetInventory();
        if (!Slots.IsValidIndex(Index))
        {
            return FReply::Handled();
        }
        DragSource = EDragSource::Inventory;
        DragIndex = Index;
        DraggedItem = Slots[Index].Item;
        DragQuantity = Event.IsShiftDown() ? 1 : Slots[Index].Count;
    }
    DragStart = Pointer;
    bDragging = false;
    return FReply::Handled().CaptureMouse(AsShared());
}

FReply SSaltwindInventory::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton || DragIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }
    const FLayout Grid = Layout(Geometry);
    Pointer = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) / Grid.Scale;
    if (ASaltwindPlayerController* Player = Controller.Get(); Player && bDragging && InRect(Pointer, FVector2D::ZeroVector, Grid.ScreenSize))
    {
        const int32 Destination = Grid.IngredientAt(Pointer);
        const bool bWorld = !Grid.IsInPanel(Pointer);
        switch (DragSource)
        {
        case EDragSource::Inventory:
            if (Destination != INDEX_NONE)
            {
                Player->MoveToCrafting(DragIndex, DraggedItem.Get(), Destination);
            }
            else if (bWorld)
            {
                Player->DropSlot(DragIndex, DraggedItem.Get(), DragQuantity);
            }
            break;
        case EDragSource::Ingredient:
            if (Destination != INDEX_NONE || Grid.IsInInventory(Pointer))
            {
                Player->MoveIngredient(DragIndex, DraggedItem.Get(), Destination);
            }
            break;
        case EDragSource::Product:
            if (bWorld || Grid.IsInInventory(Pointer))
            {
                Player->TakeCraftProduct(DraggedItem.Get(), bWorld);
            }
            break;
        }
        FirstRow = FMath::Min(FirstRow, GetScrollLimit());
    }
    ClearDrag();
    return FReply::Handled().ReleaseMouseCapture();
}

FReply SSaltwindInventory::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Pointer = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) / Layout(Geometry).Scale;
    if (DragIndex != INDEX_NONE && FVector2D::DistSquared(Pointer, DragStart) > FMath::Square(6.0))
    {
        bDragging = true;
    }
    return FReply::Handled();
}

FReply SSaltwindInventory::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    const FLayout Grid = Layout(Geometry);
    if (DragIndex == INDEX_NONE && Grid.IsInInventory(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) / Grid.Scale))
    {
        FirstRow = FMath::Clamp(FirstRow - FMath::Sign(FMath::RoundToInt(Event.GetWheelDelta() * 10)), 0, GetScrollLimit());
    }
    return FReply::Handled();
}

void SSaltwindInventory::OnMouseLeave(const FPointerEvent& Event)
{
    SLeafWidget::OnMouseLeave(Event);
    if (DragIndex == INDEX_NONE)
    {
        Pointer = FVector2D(-1000, -1000);
    }
}

void SSaltwindInventory::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    ClearDrag();
}

FReply SSaltwindInventory::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if (!Event.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::I || Key == EKeys::Q))
    {
        if (ASaltwindPlayerController* Player = Controller.Get())
        {
            ClearDrag();
            if (Key == EKeys::Q)
            {
                Player->ToggleCrafting();
            }
            else
            {
                Player->ToggleInventory();
            }
            return FReply::Handled().ReleaseMouseCapture();
        }
    }
    return FReply::Handled();
}

FCursorReply SSaltwindInventory::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
    if (bDragging)
    {
        return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
    }
    const FLayout Grid = Layout(Geometry);
    if (Grid.IsOnClose(Pointer))
    {
        return FCursorReply::Cursor(EMouseCursor::Hand);
    }
    const ASaltwindPlayerController* Player = Controller.Get();
    const int32 Ingredient = Grid.IngredientAt(Pointer);
    const bool bHasItem = Player && (Player->GetInventory().IsValidIndex(SlotAt(Grid, Pointer))
        || (Grid.IsOnProduct(Pointer) && Player->GetCraftProduct())
        || (Player->GetIngredients().IsValidIndex(Ingredient) && Player->GetIngredients()[Ingredient].Item));
    return FCursorReply::Cursor(bHasItem ? EMouseCursor::GrabHand : EMouseCursor::Default);
}

void SSaltwindInventory::ClearDrag()
{
    DragIndex = INDEX_NONE;
    DragQuantity = 0;
    DraggedItem.Reset();
    bDragging = false;
}

#undef LOCTEXT_NAMESPACE
