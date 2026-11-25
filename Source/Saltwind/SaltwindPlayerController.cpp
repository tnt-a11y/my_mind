#include "SaltwindPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SaltwindCampfire.h"
#include "SaltwindExplorer.h"
#include "SaltwindInventoryWidget.h"
#include "SaltwindWaterMovement.h"

#define LOCTEXT_NAMESPACE "SaltwindInventory"

namespace
{
    constexpr double Reach = 230.0;
}

ASaltwindPlayerController::ASaltwindPlayerController()
{
    Ingredients.SetNum(CraftCapacity);
}

void ASaltwindPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ASaltwindPlayerController::Interact);
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ASaltwindPlayerController::ToggleInventory);
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ASaltwindPlayerController::ToggleInventory);
    InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ASaltwindPlayerController::ToggleCrafting);
}

void ASaltwindPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (IsLocalController() && !IsInventoryOpen())
    {
        FocusTime -= DeltaSeconds;
        if (FocusTime <= 0.0f)
        {
            FocusedItem = FindInteractable();
            FocusTime = 0.10f;
        }
    }
}

void ASaltwindPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (InventoryWidget && GetWorld() && GetWorld()->GetGameViewport())
    {
        GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(InventoryWidget.ToSharedRef());
    }
    InventoryWidget.Reset();
    Super::EndPlay(Reason);
}

AActor* ASaltwindPlayerController::FindInteractable() const
{
    const APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn)
    {
        return nullptr;
    }
    const FVector Center = ControlledPawn->GetActorLocation();
    const FVector ViewDirection = GetControlRotation().Vector().GetSafeNormal2D();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindInteract), false, ControlledPawn);
    TArray<FOverlapResult> Nearby;
    GetWorld()->OverlapMultiByObjectType(Nearby, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic),
                                         FCollisionShape::MakeSphere(Reach), Query);
    AActor* Match = nullptr;
    double Best = TNumericLimits<double>::Max();
    TSet<AActor*> Considered;
    for (const FOverlapResult& Overlap : Nearby)
    {
        AActor* Actor = Overlap.GetActor();
        if (!Actor || Considered.Contains(Actor))
        {
            continue;
        }
        Considered.Add(Actor);
        const ISaltwindInteractable* Candidate = Cast<ISaltwindInteractable>(Actor);
        if (!Candidate || !Candidate->CanInteract(*this))
        {
            continue;
        }
        const FVector Point = Candidate->InteractionPoint();
        const FVector Offset = Point - Center;
        const double Distance = Offset.Size();
        const double Priority = Distance - 43.0 * FVector::DotProduct(ViewDirection, Offset.GetSafeNormal2D());
        if (Distance > Reach || Priority >= Best)
        {
            continue;
        }
        FHitResult Obstruction;
        if (GetWorld()->LineTraceSingleByChannel(Obstruction, Center, Point, ECC_Visibility, Query)
            && Obstruction.GetActor() != Actor)
        {
            continue;
        }
        Match = Actor;
        Best = Priority;
    }
    return Match;
}

void ASaltwindPlayerController::Interact()
{
    if (!IsInventoryOpen())
    {
        FocusedItem = FindInteractable();
        if (ISaltwindInteractable* Target = Cast<ISaltwindInteractable>(FocusedItem.Get()))
        {
            Target->Interact(*this);
            FocusedItem.Reset();
            FocusTime = 0.0f;
        }
    }
}

void ASaltwindPlayerController::Collect(USaltwindItemDefinition& Item, int32 Quantity)
{
    if (Quantity <= 0)
    {
        return;
    }
    AddToInventory(Item, Quantity);
    Notify(FText::Format(LOCTEXT("Collected", "+{0}  {1}"), FText::AsNumber(Quantity), Item.DisplayName));
}

void ASaltwindPlayerController::AddToInventory(USaltwindItemDefinition& Item, int32 Quantity)
{
    const int32 StackLimit = FMath::Max(1, Item.StackLimit);
    for (FSaltwindInventorySlot& Slot : Slots)
    {
        if (Slot.Item == &Item && Slot.Count < StackLimit)
        {
            const int32 Transfer = FMath::Min(Quantity, StackLimit - Slot.Count);
            Slot.Count += Transfer;
            Quantity -= Transfer;
            if (Quantity == 0)
            {
                break;
            }
        }
    }
    while (Quantity > 0)
    {
        const int32 Transfer = FMath::Min(Quantity, StackLimit);
        Slots.Add({&Item, Transfer});
        Quantity -= Transfer;
    }
}

bool ASaltwindPlayerController::DropSlot(int32 Index, USaltwindItemDefinition* ExpectedItem, int32 Quantity)
{
    if (!ExpectedItem || !ExpectedItem->Mesh || !Slots.IsValidIndex(Index) || Slots[Index].Item != ExpectedItem
        || Quantity <= 0 || Quantity > Slots[Index].Count)
    {
        return false;
    }
    if (!PlaceWorldItem(*ExpectedItem, Quantity))
    {
        return false;
    }
    Slots[Index].Count -= Quantity;
    if (Slots[Index].Count == 0)
    {
        Slots.RemoveAt(Index);
    }
    return true;
}

bool ASaltwindPlayerController::PlaceWorldItem(USaltwindItemDefinition& Item, int32 Count)
{
    const bool bFire = Item.Kind == ESaltwindItemKind::Campfire;
    if (bFire && Count != 1)
    {
        return false;
    }
    FTransform Transform;
    bool bFloating = false;
    if (!FindDropTransform(Item, Transform, bFloating))
    {
        Notify(bFire ? LOCTEXT("NoFireSpace", "Find a clear, level spot above the water.")
                    : LOCTEXT("NoDropSpace", "There’s no clear spot to drop this here."));
        return false;
    }
    if (bFire)
    {
        ASaltwindCampfire* Fire = GetWorld()->SpawnActorDeferred<ASaltwindCampfire>(ASaltwindCampfire::StaticClass(), Transform,
            nullptr, GetPawn(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Fire)
        {
            return false;
        }
        Fire->Initialize(Item);
        Fire->FinishSpawning(Transform);
        Notify(LOCTEXT("FirePlaced", "Campfire lit"));
    }
    else
    {
        ASaltwindPickup* Pickup = GetWorld()->SpawnActorDeferred<ASaltwindPickup>(ASaltwindPickup::StaticClass(), Transform,
            nullptr, GetPawn(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pickup)
        {
            return false;
        }
        Pickup->Initialize(Item, Count, bFloating);
        Pickup->FinishSpawning(Transform);
        Notify(FText::Format(LOCTEXT("Dropped", "Dropped {0} ×{1}"), Item.DisplayName, FText::AsNumber(Count)));
    }
    return true;
}

bool ASaltwindPlayerController::FindDropTransform(const USaltwindItemDefinition& Item, FTransform& OutTransform,
                                                   bool& bFloating) const
{
    const ASaltwindExplorer* Explorer = Cast<ASaltwindExplorer>(GetPawn());
    if (!Explorer)
    {
        return false;
    }
    const FVector Center = Explorer->GetActorLocation();
    const FVector Forward = FRotator(0, GetControlRotation().Yaw, 0).Vector();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const bool bFire = Item.Kind == ESaltwindItemKind::Campfire;
    const FVector2D Candidates[] = {{148,0}, {125,-57}, {125,57}, {91,0}};
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindDrop), false, Explorer);
    const USaltwindWaterMovement* Movement = Explorer->GetWaterMovement();
    const double Radius = bFire ? Item.Length * .48 : FMath::Clamp(Item.Length * 0.42, 12.0, 37.0);
    for (const FVector2D& Candidate : Candidates)
    {
        FVector Point = Center + Forward*(Candidate.X + (bFire ? 38 : 0)) + Right*Candidate.Y;
        FHitResult Ground;
        const bool bGround = GetWorld()->LineTraceSingleByChannel(Ground, Point + FVector(0,0,100),
            Point - FVector(0,0,380), ECC_Visibility, Query);
        const double WaterHeight = Movement->GetWaterHeightAt(FVector(Point.X, Point.Y, 0));
        const bool bWater = (bGround && Ground.ImpactPoint.Z < WaterHeight - 10.0)
            || (!bGround && Center.Z < WaterHeight + 92.0 && Movement->IsInOcean());
        if (bFire && (bWater || (bGround && Ground.ImpactPoint.Z < WaterHeight + 10.0)))
        {
            continue;
        }
        const bool bFloat = bWater && Item.Kind != ESaltwindItemKind::Stone;
        if (!bFloat && (!bGround || Ground.ImpactNormal.Z < (bFire ? .90 : .68) || FMath::Abs(Ground.ImpactPoint.Z - Center.Z) > 172.0))
        {
            continue;
        }
        Point.Z = bFloat ? WaterHeight - 1.5 : Ground.ImpactPoint.Z - 0.8;
        if (GetWorld()->LineTraceTestByChannel(Center, Point + FVector(0,0,28), ECC_Visibility, Query)
            || GetWorld()->OverlapBlockingTestByChannel(Point + FVector(0,0,17), FQuat::Identity, ECC_Visibility,
                FCollisionShape::MakeBox(FVector(Radius, Radius, 7)), Query))
        {
            continue;
        }
        const FVector Normal = bFloat ? FVector::UpVector : FVector(Ground.ImpactNormal);
        OutTransform = FTransform(FRotationMatrix::MakeFromZX(Normal, Right).ToQuat(), Point);
        bFloating = bFloat;
        return true;
    }
    return false;
}

void ASaltwindPlayerController::ToggleInventory()
{
    SetInventoryOpen(!IsInventoryOpen());
}

void ASaltwindPlayerController::ToggleCrafting()
{
    SetInventoryOpen(!IsCraftingOpen(), true);
}

USaltwindItemDefinition* ASaltwindPlayerController::GetRecipeProduct() const
{
    return CampfireRecipe ? CampfireRecipe->Product.Get() : nullptr;
}

USaltwindItemDefinition* ASaltwindPlayerController::GetCraftProduct() const
{
    return CampfireRecipe && CampfireRecipe->Matches(Ingredients) ? CampfireRecipe->Product.Get() : nullptr;
}

bool ASaltwindPlayerController::MoveToCrafting(int32 Index, const USaltwindItemDefinition* Expected, int32 Destination)
{
    if (!Expected || !Slots.IsValidIndex(Index) || Slots[Index].Item != Expected || Slots[Index].Count < 1
        || !Ingredients.IsValidIndex(Destination) || Ingredients[Destination].Item)
    {
        return false;
    }
    if (Expected->Kind != ESaltwindItemKind::Wood && Expected->Kind != ESaltwindItemKind::Stone)
    {
        Notify(LOCTEXT("IngredientNeeded", "Use wood or stone in the crafting slots."));
        return false;
    }
    Ingredients[Destination] = {Slots[Index].Item, 1};
    if (--Slots[Index].Count == 0)
    {
        Slots.RemoveAt(Index);
    }
    return true;
}

bool ASaltwindPlayerController::MoveIngredient(int32 Index, const USaltwindItemDefinition* Expected, int32 Destination)
{
    if (!Expected || !Ingredients.IsValidIndex(Index) || Ingredients[Index].Item != Expected)
    {
        return false;
    }
    if (Destination == INDEX_NONE)
    {
        AddToInventory(*Ingredients[Index].Item, 1);
    }
    else if (Ingredients.IsValidIndex(Destination) && !Ingredients[Destination].Item)
    {
        Ingredients[Destination] = Ingredients[Index];
    }
    else
    {
        return false;
    }
    Ingredients[Index] = {};
    return true;
}

bool ASaltwindPlayerController::TakeCraftProduct(const USaltwindItemDefinition* Expected, bool bPlaceInWorld)
{
    USaltwindItemDefinition* Product = GetCraftProduct();
    if (!Product || Product != Expected || (bPlaceInWorld && !PlaceWorldItem(*Product, 1)))
    {
        return false;
    }
    for (FSaltwindInventorySlot& Ingredient : Ingredients)
    {
        Ingredient = {};
    }
    if (!bPlaceInWorld)
    {
        AddToInventory(*Product, 1);
        Notify(FText::Format(LOCTEXT("Crafted", "Crafted {0}"), Product->DisplayName));
    }
    return true;
}

void ASaltwindPlayerController::ReturnIngredients()
{
    for (FSaltwindInventorySlot& Ingredient : Ingredients)
    {
        if (Ingredient.Item)
        {
            AddToInventory(*Ingredient.Item, Ingredient.Count);
            Ingredient = {};
        }
    }
}

void ASaltwindPlayerController::SetInventoryOpen(bool bOpen, bool bWithCrafting)
{
    UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!Viewport || !IsLocalController())
    {
        return;
    }
    if (bWithCrafting && bOpen && !CampfireRecipe)
    {
        CampfireRecipe = LoadObject<USaltwindCraftRecipe>(nullptr,
            TEXT("/Game/Saltwind/Items/Recipes/DA_CampfireRecipe.DA_CampfireRecipe"));
    }
    bCraftingOpen = bOpen && bWithCrafting;
    if (!bCraftingOpen)
    {
        ReturnIngredients();
    }
    if (InventoryWidget && !bOpen)
    {
        const TSharedPtr<SSaltwindInventory> Closing = MoveTemp(InventoryWidget);
        Viewport->RemoveViewportWidgetContent(Closing.ToSharedRef());
        bShowMouseCursor = false;
        SetInputMode(FInputModeGameOnly());
        SetIgnoreMoveInput(false);
        SetIgnoreLookInput(false);
        FocusTime = 0.0f;
    }
    else if (!InventoryWidget && bOpen)
    {
        if (!GetPawn())
        {
            return;
        }
        if (ASaltwindExplorer* Explorer = Cast<ASaltwindExplorer>(GetPawn()))
        {
            Explorer->ReleaseMovementControls();
        }
        FocusedItem.Reset();
        InventoryWidget = SNew(SSaltwindInventory).Controller(this);
        Viewport->AddViewportWidgetContent(InventoryWidget.ToSharedRef(), 30);
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(InventoryWidget).SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
        bShowMouseCursor = true;
    }
    FlushPressedKeys();
}

int32 ASaltwindPlayerController::GetItemCount() const
{
    int32 Count = 0;
    for (const FSaltwindInventorySlot& Slot : Slots)
    {
        Count += Slot.Count;
    }
    return Count;
}

FText ASaltwindPlayerController::GetFocusedItem() const
{
    const ISaltwindInteractable* Target = Cast<ISaltwindInteractable>(FocusedItem.Get());
    return Target && Target->CanInteract(*this) ? Target->InteractionName() : FText::GetEmpty();
}

void ASaltwindPlayerController::Notify(const FText& Message)
{
    Notice = Message;
    NoticeUntil = GetWorld()->GetTimeSeconds() + 2.8;
}

FText ASaltwindPlayerController::GetNotice() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < NoticeUntil ? Notice : FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
