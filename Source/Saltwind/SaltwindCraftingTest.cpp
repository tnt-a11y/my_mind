#include "SaltwindPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SaltwindCampfire.h"
#include "SaltwindInventoryWidget.h"
#include "SaltwindWeather.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaltwindCraftingTest, "Saltwind.Exploration.CraftingAndCampfire",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

class FSaltwindCraftingStep : public IAutomationLatentCommand
{
public:
    explicit FSaltwindCraftingStep(FAutomationTestBase& InTest) : Test(InTest) {}

    bool Update() override
    {
        if (FPlatformTime::Seconds() - Start > 76.0)
        {
            Test.AddError(FString::Printf(TEXT("Crafting timed out in stage %d."), Stage));
            return true;
        }
        ASaltwindPlayerController* PC = FindController();
        if (!PC || !PC->GetPawn() || PC->GetWorld()->GetTimeSeconds() < Next)
        {
            return false;
        }
        UWorld& World = *PC->GetWorld();
        double Delay = .24;
        switch (Stage)
        {
        case 0:
        {
            PC->ConsoleCommand(TEXT("DisableAllScreenMessages"));
            int32 StoneCount = 0;
            TSet<USaltwindItemDefinition*> StoneKinds;
            for (TActorIterator<ASaltwindPickup> It(&World); It; ++It)
            {
                auto* Item = const_cast<USaltwindItemDefinition*>(It->GetItem());
                if (!Item)
                {
                    continue;
                }
                if (Item->Kind == ESaltwindItemKind::Stone)
                {
                    ++StoneCount;
                    StoneKinds.Add(Item);
                    if (It->GetActorLocation().Equals(FVector(-4370,-4065,0), 250)
                        && Item->DisplayName.ToString() == TEXT("Moss Stone"))
                    {
                        FirstStone = *It;
                        StoneA = Item;
                    }
                    if (It->GetActorLocation().Equals(FVector(-4330,-4360,0), 225)
                        && Item->DisplayName.ToString() == TEXT("Split Rock"))
                    {
                        SecondStone = *It;
                        StoneB = Item;
                    }
                }
                if (Item->Kind == ESaltwindItemKind::Wood && Item->DisplayName.ToString() == TEXT("Driftwood"))
                {
                    WoodA = Item;
                }
                if (Item->Kind == ESaltwindItemKind::Wood && Item->DisplayName.ToString() == TEXT("Pale Branch"))
                {
                    WoodB = Item;
                }
            }
            if (!Test.TestTrue(TEXT("Four stone variants are placed around Coral Reach"), StoneKinds.Num() == 4
                && StoneCount == 38 && FirstStone.IsValid() && SecondStone.IsValid() && WoodA && WoodB))
            {
                return true;
            }
            PC->SetIgnoreLookInput(true);
            Approach(*PC, *FirstStone);
            break;
        }
        case 1:
            Key(*PC, EKeys::E, true);
            break;
        case 2:
            Key(*PC, EKeys::E, false);
            Test.TestTrue(TEXT("E collects a nearby stone into the shared inventory"), !FirstStone.IsValid() && PC->GetItemCount() == 1);
            Approach(*PC, *SecondStone);
            break;
        case 3:
            Key(*PC, EKeys::E, true);
            break;
        case 4:
            Key(*PC, EKeys::E, false);
            if (!Test.TestTrue(TEXT("A different rock is collected in its own slot"), !SecondStone.IsValid() && PC->GetItemCount() == 2))
            {
                return true;
            }
            PC->Collect(*WoodA, 2);
            PC->Collect(*WoodB, 2);
            PC->GetPawn()->SetActorLocation(FVector(-4240,-4135,193), false, nullptr, ETeleportType::TeleportPhysics);
            PC->SetControlRotation(FRotator(-19,-55,0));
            Key(*PC, EKeys::Q, true);
            break;
        case 5:
            Key(*PC, EKeys::Q, false);
            if (!Test.TestTrue(TEXT("Q opens crafting alongside the inventory"), PC->IsCraftingOpen() && PC->GetRecipeProduct()))
            {
                return true;
            }
            Ingredient(*PC, WoodA, 0);
            Ingredient(*PC, WoodB, 1);
            Ingredient(*PC, WoodA, 2);
            Ingredient(*PC, StoneA, 3);
            Test.TestTrue(TEXT("Four items fill the crafting grid and an incorrect ratio produces nothing"),
                          PC->GetIngredients().Num() == 4 && PC->GetCraftProduct() == nullptr && PC->GetItemCount() == 2);
            break;
        case 6:
        {
            auto& Widget = *PC->InventoryWidget;
            const auto Grid = Widget.Layout(Widget.GetCachedGeometry());
            Drag(Widget, Grid.IngredientPosition(2), Grid.Origin + FVector2D(49,175));
            Ingredient(*PC, StoneB, 2);
            Test.TestTrue(TEXT("Any two woods and any two stones unlock one campfire"), PC->GetCraftProduct() != nullptr);
            Capture(TEXT("Saltwind-Crafting"), true);
            break;
        }
        case 7:
        {
            auto& Widget = *PC->InventoryWidget;
            const auto Grid = Widget.Layout(Widget.GetCachedGeometry());
            Drag(Widget, Grid.ProductPosition(), Grid.Origin + FVector2D(48,175));
            Product = PC->GetRecipeProduct();
            Test.TestTrue(TEXT("Taking the product stores one campfire and consumes the four ingredients"),
                          PC->GetItemCount() == 3 && CountKind(*PC, ESaltwindItemKind::Campfire) == 1 && !PC->GetCraftProduct());
            Ingredient(*PC, WoodA, 0);
            PC->ToggleCrafting();
            Test.TestTrue(TEXT("Closing crafting returns unspent ingredients"), !PC->IsCraftingOpen() && PC->GetItemCount() == 3);
            PC->Collect(*StoneA, 1);
            PC->Collect(*StoneB, 1);
            PC->ToggleCrafting();
            break;
        }
        case 8:
        {
            Ingredient(*PC, WoodA, 0);
            Ingredient(*PC, WoodB, 1);
            Ingredient(*PC, StoneB, 2);
            Ingredient(*PC, StoneA, 3);
            auto& Widget = *PC->InventoryWidget;
            const auto Grid = Widget.Layout(Widget.GetCachedGeometry());
            Drag(Widget, Grid.ProductPosition(), FVector2D(165,350));
            for (TActorIterator<ASaltwindCampfire> It(&World); It; ++It)
            {
                WorldFire = *It;
                break;
            }
            if (!Test.TestTrue(TEXT("Dragging the product into the scene lights a non-collectible campfire"), WorldFire.IsValid()
                && WorldFire->IsBurning() && !WorldFire->Implements<USaltwindInteractable>()
                && PC->GetItemCount() == 1 && !PC->GetCraftProduct()))
            {
                return true;
            }
            PC->ToggleCrafting();
            Delay = 1.4;
            break;
        }
        case 9:
        {
            Test.TestTrue(TEXT("The lit fire has visible effects and begins with at most ten minutes of fuel"),
                WorldFire->FlameMaterials.Num() == 3 && WorldFire->SmokeMaterial && WorldFire->EmberMaterial
                && WorldFire->GetRemainingFuel() > 590 && WorldFire->GetRemainingFuel() <= 600);
            PC->FocusedItem.Reset();
            PC->FocusTime = 1.0f;
            Capture(TEXT("Saltwind-Campfire"));
            PC->GetPawn()->SetActorLocation(FVector(-4035,-4110,199), false, nullptr, ETeleportType::TeleportPhysics);
            PC->SetControlRotation(FRotator(-19,-15,0));
            break;
        }
        case 10:
        {
            const int32 Slot = InventoryIndex(*PC, Product);
            Test.TestTrue(TEXT("Dropping a stored campfire also places it lit"), PC->DropSlot(Slot, Product, 1));
            ASaltwindCampfire* DryFire = nullptr;
            for (TActorIterator<ASaltwindCampfire> It(&World); It; ++It)
            {
                if (*It != WorldFire.Get())
                {
                    DryFire = *It;
                }
            }
            if (Test.TestNotNull(TEXT("The stored fire spawned another world actor"), DryFire))
            {
                DryFire->Tick(599.0f);
                Test.TestTrue(TEXT("With no rain, campfire fuel survives until ten minutes"), DryFire->IsBurning());
                DryFire->Tick(2.0f);
                Test.TestTrue(TEXT("Dry campfires extinguish by ten minutes"), !DryFire->IsBurning() && !DryFire->Firelight->IsVisible());
            }
            auto* Weather = Cast<ASaltwindWeather>(UGameplayStatics::GetActorOfClass(&World, ASaltwindWeather::StaticClass()));
            if (!Test.TestNotNull(TEXT("The fire uses Coral Reach’s live rain actor"), Weather))
            {
                return true;
            }
            Weather->ToggleRain();
            Delay = 17.5;
            break;
        }
        case 11:
            Test.TestTrue(TEXT("A downpour extinguishes the fresh fire in under 18 seconds"), WorldFire.IsValid()
                          && !WorldFire->IsBurning() && !WorldFire->Firelight->IsVisible() && WorldFire->FireBed->IsVisible());
            Capture(TEXT("Saltwind-RainedOutCampfire"));
            PC->SetIgnoreLookInput(false);
            return true;
        }
        ++Stage;
        Next = World.GetTimeSeconds() + Delay;
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

    static void Key(ASaltwindPlayerController& PC, const FKey& Value, bool bDown)
    {
        PC.InputKey(FInputKeyParams(Value, bDown ? IE_Pressed : IE_Released, bDown ? 1.0 : 0.0));
    }

    static void Approach(ASaltwindPlayerController& PC, const ASaltwindPickup& Pickup)
    {
        PC.GetPawn()->SetActorLocation(Pickup.GetActorLocation() + FVector(-58, 0, 93), false, nullptr, ETeleportType::TeleportPhysics);
        PC.SetControlRotation(FRotator(-23,0,0));
    }

    static int32 InventoryIndex(const ASaltwindPlayerController& PC, const USaltwindItemDefinition* Item)
    {
        return PC.GetInventory().IndexOfByPredicate([Item](const FSaltwindInventorySlot& Slot) { return Slot.Item == Item; });
    }

    static int32 CountKind(const ASaltwindPlayerController& PC, ESaltwindItemKind Kind)
    {
        int32 Count = 0;
        for (const FSaltwindInventorySlot& Slot : PC.GetInventory())
        {
            Count += Slot.Item->Kind == Kind ? Slot.Count : 0;
        }
        return Count;
    }

    void Ingredient(ASaltwindPlayerController& PC, const USaltwindItemDefinition* Item, int32 Cell)
    {
        const int32 Index = InventoryIndex(PC, Item);
        if (!Test.TestTrue(TEXT("The ingredient is available in the inventory"), Index != INDEX_NONE))
        {
            return;
        }
        auto& Widget = *PC.InventoryWidget;
        const auto Grid = Widget.Layout(Widget.GetCachedGeometry());
        Drag(Widget, Grid.TilePosition(Index), Grid.IngredientPosition(Cell));
    }

    static void Drag(SSaltwindInventory& Widget, FVector2D From, FVector2D To)
    {
        const FGeometry Geometry = Widget.GetCachedGeometry();
        const float Scale = Widget.Layout(Geometry).Scale;
        const FVector2D A = Geometry.LocalToAbsolute((From + FVector2D(27,29))*Scale);
        const FVector2D B = Geometry.LocalToAbsolute((To + FVector2D(27,29))*Scale);
        const TSet<FKey> Buttons{EKeys::LeftMouseButton};
        Widget.OnMouseButtonDown(Geometry, FPointerEvent(0,A,A,Buttons,EKeys::LeftMouseButton,0,FModifierKeysState()));
        Widget.OnMouseMove(Geometry, FPointerEvent(0,B,A,Buttons,EKeys::LeftMouseButton,0,FModifierKeysState()));
        Widget.OnMouseButtonUp(Geometry, FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
    }

    static void Capture(const TCHAR* Name, bool bShowUI = false)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("SaltwindCapture")))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (FString(Name) + TEXT(".png")), bShowUI, false);
        }
    }

    FAutomationTestBase& Test;
    TWeakObjectPtr<ASaltwindPickup> FirstStone;
    TWeakObjectPtr<ASaltwindPickup> SecondStone;
    TWeakObjectPtr<ASaltwindCampfire> WorldFire;
    USaltwindItemDefinition* StoneA = nullptr;
    USaltwindItemDefinition* StoneB = nullptr;
    USaltwindItemDefinition* WoodA = nullptr;
    USaltwindItemDefinition* WoodB = nullptr;
    USaltwindItemDefinition* Product = nullptr;
    double Start = FPlatformTime::Seconds();
    double Next = 1.5;
    int32 Stage = 0;
};

bool FSaltwindCraftingTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSaltwindCraftingStep(*this));
    return true;
}

#endif
