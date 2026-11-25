#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "SaltwindWaterMovement.generated.h"

class UStaticMeshComponent;

UCLASS()
class USaltwindWaterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    static constexpr uint8 OceanMode = 1;
    static constexpr float SwimSpeed = 205.0f;

    bool IsInOcean() const;
    bool IsWading() const { return bWading; }
    bool IsSprinting() const;
    bool IsSandContact(const FHitResult& Hit) const;
    bool IsLooseSoilContact(const FHitResult& Hit) const;
    float GetWaterHeight() const;
    float GetWaterHeightAt(const FVector& Position) const;
    float GetImmersionDepth(const FVector& Position) const;
    float GetBottomHeight() const { return BottomHeight; }
    float GetGroundEntryTime() const { return GroundEntryTime; }
    void SetSwimLift(float Input) { SwimLift = Input; }
    void SetSprintHeld(bool bHeld) { bSprintHeld = bHeld; }
    void SetGroundWetness(float Wetness) { GroundWetness = Wetness; }

    virtual float GetMaxSpeed() const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

protected:
    virtual void BeginPlay() override;
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

private:
    bool IsOverOcean(const FVector& Position) const;
    float FindBottomHeight(const FVector& Position, float SeaLevel) const;
    float SampleWaterHeight(const FVector& Position, float SeaLevel, float FloorHeight) const;
    void UpdateWaterContact(float DeltaTime);
    void RestoreWalkingCapsule(float DeltaTime);

    TWeakObjectPtr<UStaticMeshComponent> OceanSurface;
    float BottomHeight = 0.0f;
    float ImmersionDepth = 0.0f;
    float SwimLift = 0.0f;
    float DryHalfHeight = 89.0f;
    float DryRadius = 39.0f;
    float SwimClearanceHeight = 0.0f;
    float GroundEntryTime = -1.0f;
    float EntryHeightAboveWater = 0.0f;
    float GroundWetness = 0.0f;
    bool bSprintHeld = false;
    bool bOnSand = false;
    bool bWading = false;
    bool bNearSurface = true;
};
