#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SaltwindDayCycle.generated.h"

class ADirectionalLight;
class ASkyLight;

UCLASS()
class USaltwindDayCycle : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void AdvanceHours(float Offset);
    float GetHour() const { return Hour; }

private:
    void UpdateLighting();

    TWeakObjectPtr<ADirectionalLight> Sun;
    TWeakObjectPtr<ADirectionalLight> Moon;
    TWeakObjectPtr<ASkyLight> Sky;
    float Hour = 14.4f;
    float UpdateElapsed = 0.0f;
};
