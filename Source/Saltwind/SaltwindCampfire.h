#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SaltwindCampfire.generated.h"

class ASaltwindWeather;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class USaltwindItemDefinition;

UCLASS()
class ASaltwindCampfire : public AActor
{
    GENERATED_BODY()

public:
    ASaltwindCampfire();
    void Initialize(const USaltwindItemDefinition& Item);
    bool IsBurning() const { return FuelSeconds > 0.0f; }
    float GetRemainingFuel() const { return FuelSeconds; }
    static constexpr float MaxBurnSeconds = 600.0f;
    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    friend class FSaltwindCraftingStep;

    void AdvanceBurn(float DeltaSeconds, float Rain);
    void UpdateEffects(float Strength, float Rain);

    UPROPERTY() TObjectPtr<UStaticMeshComponent> FireBed;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FlameCards;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> SmokeCard;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> EmberCard;
    UPROPERTY() TObjectPtr<UPointLightComponent> Firelight;
    UPROPERTY() TObjectPtr<UAudioComponent> Crackle;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameMaterials;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SmokeMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> EmberMaterial;

    TWeakObjectPtr<ASaltwindWeather> Weather;
    float FuelSeconds = MaxBurnSeconds;
    float Soak = 0.0f;
    float CoolingSeconds = 0.0f;
};
