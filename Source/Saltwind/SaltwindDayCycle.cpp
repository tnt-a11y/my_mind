#include "SaltwindDayCycle.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"

namespace
{
// A clear late-season day at 13 degrees north, compressed to twelve minutes.
constexpr float DaySeconds = 720.0f;

FVector SolarDirection(float Hour)
{
    const double Angle = FMath::DegreesToRadians((Hour - 12.0) * 15.0);
    const double Latitude = FMath::DegreesToRadians(13.0);
    const double Declination = FMath::DegreesToRadians(8.0);
    const double East = -FMath::Cos(Declination) * FMath::Sin(Angle);
    const double North = FMath::Cos(Latitude) * FMath::Sin(Declination)
                       - FMath::Sin(Latitude) * FMath::Cos(Declination) * FMath::Cos(Angle);
    const double Up = FMath::Sin(Latitude) * FMath::Sin(Declination)
                    + FMath::Cos(Latitude) * FMath::Cos(Declination) * FMath::Cos(Angle);
    return FVector(East, North, Up).RotateAngleAxis(34.0, FVector::UpVector);
}
}

bool USaltwindDayCycle::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USaltwindDayCycle::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
    {
        if (It->ActorHasTag(TEXT("SaltwindSun")))
        {
            Sun = *It;
        }
        else if (It->ActorHasTag(TEXT("SaltwindMoon")))
        {
            Moon = *It;
        }
    }
    for (TActorIterator<ASkyLight> It(&World); It; ++It)
    {
        Sky = *It;
        break;
    }
    UpdateLighting();
}

void USaltwindDayCycle::AdvanceHours(float Offset)
{
    Hour = FMath::Fmod(Hour + Offset + 24.0f, 24.0f);
    UpdateLighting();
}

void USaltwindDayCycle::Tick(float DeltaTime)
{
    Hour = FMath::Fmod(Hour + DeltaTime * 24.0f / DaySeconds, 24.0f);
    UpdateElapsed += DeltaTime;
    if (UpdateElapsed >= 0.20f)
    {
        UpdateElapsed = 0.0f;
        UpdateLighting();
    }
}

void USaltwindDayCycle::UpdateLighting()
{
    const FVector Solar = SolarDirection(Hour);
    if (ADirectionalLight* Light = Sun.Get())
    {
        Light->SetActorRotation((-Solar).Rotation());
        const float Daylight = FMath::SmoothStep(-0.065, 0.13, Solar.Z);
        Light->GetLightComponent()->SetIntensity(68000.0f * Daylight);
    }
    if (ADirectionalLight* Light = Moon.Get())
    {
        const FVector Lunar = -SolarDirection(Hour - 0.8f);
        Light->SetActorRotation((-Lunar).Rotation());
        Light->GetLightComponent()->SetIntensity(0.85f * FMath::SmoothStep(-0.02, 0.21, Lunar.Z)
                                                * (1.0f - FMath::SmoothStep(-0.14, 0.03, Solar.Z)));
    }
    if (ASkyLight* Light = Sky.Get())
    {
        Light->GetLightComponent()->SetIntensity(FMath::Lerp(0.72f, 1.35f,
                                                  float(FMath::SmoothStep(-0.18, 0.24, Solar.Z))));
    }
}

TStatId USaltwindDayCycle::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USaltwindDayCycle, STATGROUP_Tickables);
}
