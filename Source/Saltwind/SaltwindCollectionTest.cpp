#include "SaltwindPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SaltwindInventoryWidget.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaltwindCollectionTest, "Saltwind.Exploration.CollectionAndInventory",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

class FSaltwindCollectionStep final : public IAutomationLatentCommand
{
public:
    explicit FSaltwindCollectionStep(FAutomationTestBase& InTest) : Test(InTest) {}

    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 55.0)
        {
            Test.AddError(FString::Printf(TEXT("Collection test timed out in stage %d."), Stage));
            return true;
        }
        ASaltwindPlayerController* Player = FindController();
        if (!Player || !Player->GetPawn() || Player->GetWorld()->GetTimeSeconds() < Next)
        {
            return false;
        }
        UWorld& World = *Player->GetWorld();
        switch (Stage)
        {
        case 0:
        {
            for (TActorIterator<ASaltwindPickup> It(&World); It; ++It)
            {
                if (const USaltwindItemDefinition* Item = It->GetItem(); Item && It->ActorHasTag(TEXT("SaltwindPlacedWood")))
                {
                    Items.Add(const_cast<USaltwindItemDefinition*>(Item));
                    ++WoodActors;
                }
            }
            Test.TestTrue(TEXT("Six wood variants are scattered across the island"), Items.Num() == 6 && WoodActors == 42);
            for (const USaltwindItemDefinition* Item : Items)
            {
                Test.TestTrue(TEXT("Each wood variant has a mesh and a rendered inventory thumbnail"), Item->Mesh && Item->Thumbnail);
            }
            Player->SetIgnoreLookInput(true);
            Player->SetControlRotation(FRotator(-10, -7, 0));
            ASaltwindPickup* Pickup = Cast<ASaltwindPickup>(Player->FindInteractable());
            if (!Test.TestNotNull(TEXT("Walking within reach resolves a real nearby pickup"), Pickup))
            {
                return true;
            }
            FirstPickup = Pickup;
            FirstPosition = Pickup->GetActorLocation();
            Test.TestEqual(TEXT("The nearest starter pickup is driftwood"), Pickup->InteractionName().ToString(), FString(TEXT("Driftwood")));
            Capture(TEXT("Saltwind-Interact"));
            break;
        }
        case 1:
            Test.TestFalse(TEXT("The proximity prompt has an item name"), Player->GetFocusedItem().IsEmpty());
            Press(*Player, EKeys::E);
            break;
        case 2:
            Release(*Player, EKeys::E);
            if (!Test.TestTrue(TEXT("E collects the pickup and removes its world actor"), Player->GetItemCount() == 1 && !FirstPickup.IsValid()))
            {
                return true;
            }
            Player->GetPawn()->SetActorLocation(FirstPosition + FVector(0, 0, 90), false, nullptr, ETeleportType::TeleportPhysics);
            break;
        case 3:
            Press(*Player, EKeys::E);
            break;
        case 4:
            Release(*Player, EKeys::E);
            if (!Test.TestTrue(TEXT("Collecting identical wood increments a single stack"),
                Player->Slots.Num() == 1 && Player->Slots[0].Count == 2))
            {
                return true;
            }
            Driftwood = Player->Slots[0].Item;
            for (USaltwindItemDefinition* Item : Items)
            {
                if (Item != Driftwood)
                {
                    Player->Collect(*Item, 1);
                }
            }
            Test.TestEqual(TEXT("Different item types create separate slots"), Player->Slots.Num(), 6);
            Press(*Player, EKeys::Tab);
            break;
        case 5:
        {
            Release(*Player, EKeys::Tab);
            if (!Test.TestTrue(TEXT("Tab opens the inventory and releases the mouse cursor"),
                Player->IsInventoryOpen() && Player->bShowMouseCursor && Player->IsMoveInputIgnored()))
            {
                return true;
            }
            Capture(TEXT("Saltwind-Inventory"), true);
            break;
        }
        case 6:
        {
            SSaltwindInventory& Widget = *Player->InventoryWidget;
            const FGeometry Geometry = Widget.GetCachedGeometry();
            const auto Grid = Widget.Layout(Geometry);
            const FVector2D Tile = Geometry.LocalToAbsolute((Grid.TilePosition(0) + FVector2D(51, 45)) * Grid.Scale);
            DropPointer = Geometry.LocalToAbsolute(FVector2D(310, 400) * Grid.Scale);
            Widget.OnMouseButtonDown(Geometry, Pointer(Tile, Tile, true, true));
            Widget.OnMouseMove(Geometry, Pointer(DropPointer, Tile, true, true));
            Test.TestTrue(TEXT("Shift-drag starts dragging one item from the selected stack"), Widget.bDragging && Widget.DragQuantity == 1);
            Capture(TEXT("Saltwind-InventoryDrag"), true);
            break;
        }
        case 7:
        {
            SSaltwindInventory& Widget = *Player->InventoryWidget;
            Widget.OnMouseButtonUp(Widget.GetCachedGeometry(), Pointer(DropPointer, DropPointer, false, true));
            Test.TestTrue(TEXT("Releasing the drag onto the scene removes one item"), Player->GetItemCount() == 6 && Player->Slots[0].Count == 1);
            for (TActorIterator<ASaltwindPickup> It(&World); It; ++It)
            {
                if (!It->ActorHasTag(TEXT("SaltwindPlacedWood")) && It->GetItem() == Driftwood)
                {
                    Dropped = *It;
                    break;
                }
            }
            if (!Test.TestTrue(TEXT("Dropped wood reappears in front of the player"), Dropped.IsValid() && Dropped->GetQuantity() == 1
                && FVector::DotProduct(Dropped->GetActorLocation() - Player->GetPawn()->GetActorLocation(),
                                       Player->GetControlRotation().Vector().GetSafeNormal2D()) > 80.0))
            {
                return true;
            }
            const FGeometry Geometry = Widget.GetCachedGeometry();
            const auto Grid = Widget.Layout(Geometry);
            const FVector2D From = Geometry.LocalToAbsolute((Grid.TilePosition(0) + FVector2D(20, 25)) * Grid.Scale);
            const FVector2D To = Geometry.LocalToAbsolute((Grid.TilePosition(1) + FVector2D(28, 35)) * Grid.Scale);
            Widget.OnMouseButtonDown(Geometry, Pointer(From, From, true));
            Widget.OnMouseMove(Geometry, Pointer(To, From, true));
            Widget.OnMouseButtonUp(Geometry, Pointer(To, From, false));
            Test.TestEqual(TEXT("Dragging inside the inventory keeps the item"), Player->GetItemCount(), 6);
            Widget.OnKeyDown(Geometry, FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            Test.TestFalse(TEXT("Escape closes the inventory"), Player->IsInventoryOpen());
            break;
        }
        case 8:
            Capture(TEXT("Saltwind-DroppedWood"));
            Press(*Player, EKeys::E);
            break;
        case 9:
        {
            Release(*Player, EKeys::E);
            Test.TestTrue(TEXT("The dropped item can be collected again"), !Dropped.IsValid() && Player->GetItemCount() == 7);
            Test.TestFalse(TEXT("World release cannot subtract an invalid quantity"), Player->DropSlot(0, Driftwood, 99));
            Player->ToggleInventory();
            break;
        }
        case 10:
        {
            TSharedPtr<SSaltwindInventory> Widget = Player->InventoryWidget;
            if (!Widget)
            {
                Test.AddError(TEXT("The inventory did not open for the full-stack drop."));
                return true;
            }
            const FGeometry Geometry = Widget->GetCachedGeometry();
            const auto Grid = Widget->Layout(Geometry);
            const FVector2D From = Geometry.LocalToAbsolute((Grid.TilePosition(0) + FVector2D(28, 31)) * Grid.Scale);
            const FVector2D To = Geometry.LocalToAbsolute(FVector2D(310, 400) * Grid.Scale);
            Widget->OnMouseButtonDown(Geometry, Pointer(From, From, true));
            Widget->OnMouseMove(Geometry, Pointer(To, From, true));
            Test.TestEqual(TEXT("Dragging without Shift takes the whole stack"), Widget->DragQuantity, 2);
            Widget->OnMouseButtonUp(Geometry, Pointer(To, From, false));
            Test.TestTrue(TEXT("Dropping the full stack frees its inventory slot"),
                          Player->GetItemCount() == 5 && Player->Slots.Num() == 5);
            for (TActorIterator<ASaltwindPickup> It(&World); It; ++It)
            {
                if (!It->ActorHasTag(TEXT("SaltwindPlacedWood")) && It->GetItem() == Driftwood)
                {
                    Dropped = *It;
                    break;
                }
            }
            Test.TestTrue(TEXT("A dropped stack retains its exact count in the world"), Dropped.IsValid() && Dropped->GetQuantity() == 2);
            Player->ToggleInventory();
            break;
        }
        case 11:
            Press(*Player, EKeys::E);
            break;
        case 12:
        {
            Release(*Player, EKeys::E);
            Test.TestTrue(TEXT("Recollecting a stack returns every item"), !Dropped.IsValid() && Player->GetItemCount() == 7);
            const int32 DriftIndex = Player->Slots.IndexOfByPredicate([this](const FSaltwindInventorySlot& Slot) { return Slot.Item == Driftwood; });
            Player->Collect(*Driftwood, 16 * 9);
            Test.TestEqual(TEXT("Large stacks preserve the full quantity"), Player->GetItemCount(), 151);
            Test.TestTrue(TEXT("Overflow creates more slots instead of exceeding the stack limit"), Player->Slots.Num() == 15
                && Player->Slots.IsValidIndex(DriftIndex) && Player->Slots[DriftIndex].Count == Driftwood->StackLimit);
            Player->ToggleInventory();
            Test.TestTrue(TEXT("The slot grid scrolls when the inventory grows past one page"),
                          Player->InventoryWidget.IsValid() && Player->InventoryWidget->GetScrollLimit() > 0);
            if (Player->IsInventoryOpen())
            {
                Player->ToggleInventory();
            }
            Player->SetIgnoreLookInput(false);
            Test.AddInfo(FString::Printf(TEXT("Collected, stacked, split, dropped and recollected wood in Coral Reach. %d field pickups, %d scan variants."),
                                        WoodActors, Items.Num()));
            return true;
        }
        }
        ++Stage;
        Next = World.GetTimeSeconds() + 0.25;
        return false;
    }

private:
    static ASaltwindPlayerController* FindController()
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (UWorld* World = Context.World(); World && World->IsGameWorld())
            {
                return Cast<ASaltwindPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
            }
        }
        return nullptr;
    }

    static void Press(ASaltwindPlayerController& Player, const FKey& Key)
    {
        Player.InputKey(FInputKeyParams(Key, IE_Pressed, 1.0));
    }

    static void Release(ASaltwindPlayerController& Player, const FKey& Key)
    {
        Player.InputKey(FInputKeyParams(Key, IE_Released, 0.0));
    }

    static FPointerEvent Pointer(FVector2D Position, FVector2D Previous, bool bDown, bool bShift = false)
    {
        const TSet<FKey> Pressed = bDown ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>();
        return FPointerEvent(0, Position, Previous, Pressed, EKeys::LeftMouseButton, 0.0f,
                             FModifierKeysState(bShift, false, false, false, false, false, false, false, false));
    }

    static void Capture(const TCHAR* Name, bool bShowUI = false)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("SaltwindCapture")))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (FString(Name) + TEXT(".png")), bShowUI, false);
        }
    }

    FAutomationTestBase& Test;
    double Started = FPlatformTime::Seconds();
    double Next = 1.5;
    int32 Stage = 0;
    int32 WoodActors = 0;
    TSet<USaltwindItemDefinition*> Items;
    USaltwindItemDefinition* Driftwood = nullptr;
    TWeakObjectPtr<ASaltwindPickup> FirstPickup;
    TWeakObjectPtr<ASaltwindPickup> Dropped;
    FVector FirstPosition = FVector::ZeroVector;
    FVector2D DropPointer = FVector2D::ZeroVector;
};

bool FSaltwindCollectionTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSaltwindCollectionStep(*this));
    return true;
}

#endif
