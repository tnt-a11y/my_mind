#include "SaltwindItems.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "SaltwindExplorer.h"
#include "SaltwindPlayerController.h"
#include "SaltwindWaterMovement.h"

#define LOCTEXT_NAMESPACE "SaltwindItems"

bool USaltwindCraftRecipe::Matches(const TArray<FSaltwindInventorySlot>& Slots) const
{
    if (!Product || Ingredients.IsEmpty() || Ingredients.Num() > Slots.Num())
    {
        return false;
    }
    TArray<ESaltwindItemKind, TInlineAllocator<4>> Needed(Ingredients);
    for (const FSaltwindInventorySlot& Slot : Slots)
    {
        if (!Slot.Item)
        {
            continue;
        }
        const int32 Index = Needed.Find(Slot.Item->Kind);
        if (Slot.Count != 1 || Index == INDEX_NONE)
        {
            return false;
        }
        Needed.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    }
    return Needed.IsEmpty();
}

ASaltwindPickup::ASaltwindPickup()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Placement"));
    PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
    PickupMesh->SetupAttachment(RootComponent);
    PickupMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    PickupMesh->SetGenerateOverlapEvents(false);
    PickupMesh->SetCanEverAffectNavigation(false);
    InteractionBounds = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionBounds"));
    InteractionBounds->SetupAttachment(RootComponent);
    InteractionBounds->InitSphereRadius(20.0f);
    InteractionBounds->SetRelativeLocation(FVector(0, 0, 12));
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionBounds->SetGenerateOverlapEvents(false);
    InteractionBounds->SetCanEverAffectNavigation(false);
}

void ASaltwindPickup::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UStaticMesh* Mesh = Item ? Item->Mesh.Get() : nullptr;
    PickupMesh->SetStaticMesh(Mesh);
    if (Mesh)
    {
        const FBox Bounds = Mesh->GetBoundingBox().TransformBy(FTransform(Item->RestRotation));
        const FVector Size = Bounds.GetSize();
        const double Scale = Item->Length / FMath::Max(1.0, Size.GetMax());
        const FVector Center = Bounds.GetCenter();
        PickupMesh->SetRelativeScale3D(FVector(Scale));
        PickupMesh->SetRelativeRotation(Item->RestRotation);
        PickupMesh->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Bounds.Min.Z) * Scale);
    }
    Quantity = FMath::Max(1, Quantity);
    SetActorTickEnabled(bFloating);
}

void ASaltwindPickup::Initialize(USaltwindItemDefinition& InItem, int32 InCount, bool bInFloating)
{
    Item = &InItem;
    Quantity = InCount;
    bFloating = bInFloating;
}

void ASaltwindPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (const ASaltwindExplorer* Player = Cast<ASaltwindExplorer>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        FVector Position = GetActorLocation();
        if (FVector::DistSquared2D(Position, Player->GetActorLocation()) < FMath::Square(2800.0))
        {
            Position.Z = Player->GetWaterMovement()->GetWaterHeightAt(Position) - 1.5;
            SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
}

bool ASaltwindPickup::CanInteract(const ASaltwindPlayerController& Player) const
{
    return Player.GetPawn() && Item && Item->Mesh && Quantity > 0 && !IsActorBeingDestroyed();
}

FVector ASaltwindPickup::InteractionPoint() const
{
    return PickupMesh->Bounds.Origin;
}

FText ASaltwindPickup::InteractionName() const
{
    if (Item && Quantity > 1)
    {
        return FText::Format(LOCTEXT("WorldStack", "{0} ×{1}"), Item->DisplayName, FText::AsNumber(Quantity));
    }
    return Item ? Item->DisplayName : FText::GetEmpty();
}

void ASaltwindPickup::Interact(ASaltwindPlayerController& Player)
{
    if (CanInteract(Player) && Destroy())
    {
        Player.Collect(*Item, Quantity);
    }
}

#undef LOCTEXT_NAMESPACE
