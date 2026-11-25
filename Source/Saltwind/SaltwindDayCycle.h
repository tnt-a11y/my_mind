#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SaltwindDayCycle.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
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
    void PreviewNextPhase();
    void SetRainCover(float Value) { RainCover = Value; }
    static constexpr float SecondsPerHour = 80.0f;
    float GetHour() const { return Hour; }

private:
    void UpdateLighting();

    TWeakObjectPtr<ADirectionalLight> Sun;
    TWeakObjectPtr<ADirectionalLight> Moon;
    TWeakObjectPtr<ASkyLight> Sky;
    TWeakObjectPtr<AExponentialHeightFog> Fog;
    TWeakObjectPtr<APostProcessVolume> Grading;
    float Hour = 7.0f;
    float UpdateElapsed = 0.0f;
    float PreviewHoursLeft = 0.0f;
    float PreviewHoursPerSecond = 0.0f;
    float RainCover = 0.0f;
};
