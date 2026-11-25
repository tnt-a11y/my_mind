#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "SaltwindItems.generated.h"

class ASaltwindPlayerController;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class ESaltwindItemKind : uint8
{
    Other,
    Wood,
    Stone,
    Campfire
};

UCLASS(BlueprintType)
class USaltwindItemDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item") ESaltwindItemKind Kind = ESaltwindItemKind::Other;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item") TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item") TObjectPtr<UTexture2D> Thumbnail;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item", meta=(ClampMin="1")) int32 StackLimit = 16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item", meta=(ClampMin="1", Units="cm")) float Length = 56.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Item") FRotator RestRotation = FRotator::ZeroRotator;
};

USTRUCT()
struct FSaltwindInventorySlot
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<USaltwindItemDefinition> Item;
    UPROPERTY() int32 Count = 0;
};

UCLASS(BlueprintType)
class USaltwindCraftRecipe : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Recipe") TArray<ESaltwindItemKind> Ingredients;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Recipe") TObjectPtr<USaltwindItemDefinition> Product;

    bool Matches(const TArray<FSaltwindInventorySlot>& Slots) const;
};

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class USaltwindInteractable : public UInterface
{
    GENERATED_BODY()
};

class ISaltwindInteractable
{
    GENERATED_BODY()

public:
    virtual bool CanInteract(const ASaltwindPlayerController& Player) const = 0;
    virtual FVector InteractionPoint() const = 0;
    virtual FText InteractionName() const = 0;
    virtual void Interact(ASaltwindPlayerController& Player) = 0;
};

UCLASS()
class ASaltwindPickup : public AActor, public ISaltwindInteractable
{
    GENERATED_BODY()

public:
    ASaltwindPickup();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool CanInteract(const ASaltwindPlayerController& Player) const override;
    virtual FVector InteractionPoint() const override;
    virtual FText InteractionName() const override;
    virtual void Interact(ASaltwindPlayerController& Player) override;

    void Initialize(USaltwindItemDefinition& InItem, int32 InCount, bool bInFloating);
    const USaltwindItemDefinition* GetItem() const { return Item; }
    int32 GetQuantity() const { return Quantity; }

private:
    UPROPERTY(VisibleAnywhere, Category="Pickup") TObjectPtr<UStaticMeshComponent> PickupMesh;
    UPROPERTY(VisibleAnywhere, Category="Pickup") TObjectPtr<USphereComponent> InteractionBounds;
    UPROPERTY(EditAnywhere, Category="Pickup") TObjectPtr<USaltwindItemDefinition> Item;
    UPROPERTY(EditAnywhere, Category="Pickup", meta=(ClampMin="1")) int32 Quantity = 1;
    bool bFloating = false;
};
