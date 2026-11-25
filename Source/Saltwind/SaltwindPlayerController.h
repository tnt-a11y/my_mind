#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SaltwindItems.h"
#include "SaltwindPlayerController.generated.h"

class SSaltwindInventory;

UCLASS()
class ASaltwindPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    ASaltwindPlayerController();
    void Collect(USaltwindItemDefinition& Item, int32 Quantity);
    bool DropSlot(int32 Index, USaltwindItemDefinition* ExpectedItem, int32 Quantity);
    void ToggleInventory();
    void ToggleCrafting();
    bool IsInventoryOpen() const { return InventoryWidget.IsValid(); }
    bool IsCraftingOpen() const { return IsInventoryOpen() && bCraftingOpen; }
    const TArray<FSaltwindInventorySlot>& GetInventory() const { return Slots; }
    const TArray<FSaltwindInventorySlot>& GetIngredients() const { return Ingredients; }
    USaltwindItemDefinition* GetCraftProduct() const;
    USaltwindItemDefinition* GetRecipeProduct() const;
    bool MoveToCrafting(int32 Slot, const USaltwindItemDefinition* Expected, int32 Destination);
    bool MoveIngredient(int32 Slot, const USaltwindItemDefinition* Expected, int32 Destination);
    bool TakeCraftProduct(const USaltwindItemDefinition* Expected, bool bPlaceInWorld);
    static constexpr int32 CraftCapacity = 4;
    int32 GetItemCount() const;
    FText GetFocusedItem() const;
    FText GetNotice() const;

protected:
    virtual void SetupInputComponent() override;

private:
    friend class FSaltwindCollectionStep;
    friend class FSaltwindCraftingStep;

    AActor* FindInteractable() const;
    void Interact();
    bool FindDropTransform(const USaltwindItemDefinition& Item, FTransform& OutTransform, bool& bFloating) const;
    bool PlaceWorldItem(USaltwindItemDefinition& Item, int32 Count);
    void AddToInventory(USaltwindItemDefinition& Item, int32 Count);
    void SetInventoryOpen(bool bOpen, bool bWithCrafting = false);
    void ReturnIngredients();
    void Notify(const FText& Message);

    UPROPERTY() TArray<FSaltwindInventorySlot> Slots;
    UPROPERTY() TArray<FSaltwindInventorySlot> Ingredients;
    UPROPERTY() TObjectPtr<USaltwindCraftRecipe> CampfireRecipe;
    TSharedPtr<SSaltwindInventory> InventoryWidget;
    TWeakObjectPtr<AActor> FocusedItem;
    FText Notice;
    double NoticeUntil = 0.0;
    float FocusTime = 0.0f;
    bool bCraftingOpen = false;
};
