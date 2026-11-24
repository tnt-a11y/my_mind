#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "SaltwindWaterMovement.generated.h"

UCLASS()
class USaltwindWaterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    static constexpr uint8 OceanMode = 1;
    static constexpr float SwimSpeed = 235.0f;

    bool IsInOcean() const;
    bool IsWading() const { return bWading; }
    float GetWaterHeight() const;
    float GetBottomHeight() const { return BottomHeight; }
    void SetSwimLift(float Input) { SwimLift = Input; }

    virtual float GetMaxSpeed() const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

protected:
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

private:
    void UpdateWaterContact();
    void RestoreWalkingCapsule();

    float BottomHeight = 0.0f;
    float SwimLift = 0.0f;
    float DryHalfHeight = 89.0f;
    bool bWading = false;
    bool bNearSurface = true;
};
