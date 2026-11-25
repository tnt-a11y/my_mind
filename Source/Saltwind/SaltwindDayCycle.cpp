#include "SaltwindDayCycle.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"

namespace
{
struct FLightingKey
{
    float Hour;
    FLinearColor Sun;
    FLinearColor Sky;
    FLinearColor Haze;
    float FogDensity;
    float Exposure;
    float Saturation;
};

const FLightingKey Lighting[] = {
    {2.0f,  {0.65f,0.74f,1.0f}, {0.31f,0.48f,0.78f}, {0.016f,0.036f,0.090f}, .009f, -2.30f, .72f},
    {7.0f,  {1.0f,0.76f,0.52f}, {0.73f,0.88f,1.0f}, {0.29f,0.40f,0.46f}, .019f, .30f, .93f},
    {12.0f, {1.0f,0.97f,0.91f}, {0.91f,0.98f,1.0f}, {0.28f,0.45f,0.58f}, .006f, -.23f, 1.04f},
    {15.5f, {1.0f,0.87f,0.69f}, {0.86f,0.92f,1.0f}, {0.34f,0.44f,0.48f}, .010f, -.06f, 1.08f},
    {17.8f, {1.0f,0.61f,0.34f}, {0.67f,0.72f,1.0f}, {0.34f,0.19f,0.24f}, .016f, .42f, 1.00f},
    {22.5f, {0.72f,0.76f,1.0f}, {0.48f,0.62f,0.85f}, {0.028f,0.056f,0.082f}, .012f, -1.25f, .82f},
};
constexpr float PreviewSeconds = 20.0f;

float WrappedHours(float Hours)
{
    return FMath::Fmod(FMath::Fmod(Hours, 24.0f) + 24.0f, 24.0f);
}

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
    for (TActorIterator<AExponentialHeightFog> It(&World); It; ++It)
    {
        Fog = *It;
        break;
    }
    for (TActorIterator<APostProcessVolume> It(&World); It; ++It)
    {
        if (It->bUnbound)
        {
            Grading = *It;
            break;
        }
    }
    UpdateLighting();
}

void USaltwindDayCycle::AdvanceHours(float Offset)
{
    Hour = WrappedHours(Hour + Offset);
    UpdateLighting();
}

void USaltwindDayCycle::PreviewNextPhase()
{
    if (PreviewHoursLeft > 0.0f)
    {
        return;
    }
    float Offset = 24.0f;
    for (const FLightingKey& Key : Lighting)
    {
        const float Distance = WrappedHours(Key.Hour - Hour);
        if (Distance > 0.12f)
        {
            Offset = FMath::Min(Offset, Distance);
        }
    }
    PreviewHoursLeft = Offset;
    PreviewHoursPerSecond = Offset / PreviewSeconds;
}

void USaltwindDayCycle::Tick(float DeltaTime)
{
    float MoveHours = DeltaTime / SecondsPerHour;
    if (PreviewHoursLeft > 0.0f)
    {
        MoveHours = FMath::Min(PreviewHoursLeft, DeltaTime * PreviewHoursPerSecond);
        PreviewHoursLeft -= MoveHours;
    }
    Hour = WrappedHours(Hour + MoveHours);
    UpdateElapsed += DeltaTime;
    if (UpdateElapsed >= 0.20f)
    {
        UpdateElapsed = 0.0f;
        UpdateLighting();
    }
}

void USaltwindDayCycle::UpdateLighting()
{
    int32 Next = 0;
    while (Next < UE_ARRAY_COUNT(Lighting) && Lighting[Next].Hour <= Hour)
    {
        ++Next;
    }
    const FLightingKey& Previous = Lighting[(Next + UE_ARRAY_COUNT(Lighting) - 1) % UE_ARRAY_COUNT(Lighting)];
    const FLightingKey& Upcoming = Lighting[Next % UE_ARRAY_COUNT(Lighting)];
    const float Phase = WrappedHours(Hour - Previous.Hour) / WrappedHours(Upcoming.Hour - Previous.Hour);
    const float Blend = FMath::SmoothStep(0.15f, 0.85f, Phase);
    const FVector Solar = SolarDirection(Hour);
    const float Night = 1.0f - float(FMath::SmoothStep(-0.10, 0.12, Solar.Z));
    const float Cover = FMath::SmoothStep(0.0f, 1.0f, RainCover);
    if (ADirectionalLight* Light = Sun.Get())
    {
        Light->SetActorRotation((-Solar).Rotation());
        const float Daylight = FMath::SmoothStep(-0.15, 0.13, Solar.Z);
        Light->GetLightComponent()->SetIntensity(92000.0f * Daylight * FMath::Lerp(1.0f, 0.42f, Cover)
            * (0.30f + 0.70f * FMath::SmoothStep(0.02, 0.83, Solar.Z)));
        Light->GetLightComponent()->SetLightColor(FMath::Lerp(Previous.Sun, Upcoming.Sun, Blend), false);
    }
    if (ADirectionalLight* Light = Moon.Get())
    {
        const float MoonPhase = FMath::Clamp(WrappedHours(Hour - 17.5f) / 12.8f, 0.0f, 1.0f);
        const float Elevation = 12.0f + 45.0f * FMath::Sin(PI * MoonPhase);
        const FVector Lunar = FRotator(Elevation, -55.0f + 154.0f * MoonPhase, 0.0f).Vector();
        Light->SetActorRotation((-Lunar).Rotation());
        const float LateNight = 0.5f + 0.5f * FMath::Cos((Hour - 2.0f) * (PI / 12.0f));
        Light->GetLightComponent()->SetLightColor(FMath::Lerp(FLinearColor(.49f,.63f,.94f),
            FLinearColor(.69f,.80f,1.0f), LateNight), false);
        Light->GetLightComponent()->SetIntensity(Night * FMath::Lerp(11.0f, 32.0f, LateNight) * (1.0f - Cover * 0.26f));
    }
    if (ASkyLight* Light = Sky.Get())
    {
        Light->GetLightComponent()->SetLightColor(FMath::Lerp(Previous.Sky, Upcoming.Sky, Blend));
        Light->GetLightComponent()->SetIntensity(FMath::Lerp(0.78f, 1.35f,
                                                  float(FMath::SmoothStep(-0.18, 0.24, Solar.Z))));
    }
    if (AExponentialHeightFog* Atmosphere = Fog.Get())
    {
        UExponentialHeightFogComponent* Component = Atmosphere->GetComponent();
        Component->SetFogDensity(FMath::Lerp(Previous.FogDensity, Upcoming.FogDensity, Blend) + Cover * 0.033f);
        Component->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(Previous.Haze, Upcoming.Haze, Blend),
            FMath::Lerp(FLinearColor(.48f,.55f,.60f), FLinearColor(.044f,.058f,.084f), Night), Cover * 0.62f));
    }
    if (APostProcessVolume* Volume = Grading.Get())
    {
        Volume->Settings.bOverride_AutoExposureBias = true;
        Volume->Settings.bOverride_AutoExposureMinBrightness = true;
        Volume->Settings.bOverride_AutoExposureSpeedUp = true;
        Volume->Settings.bOverride_AutoExposureSpeedDown = true;
        Volume->Settings.bOverride_ColorSaturation = true;
        Volume->Settings.AutoExposureBias = FMath::Lerp(Previous.Exposure, Upcoming.Exposure, Blend) - Cover * 0.22f;
        Volume->Settings.AutoExposureMinBrightness = FMath::Lerp(-2.0f, -7.5f, Night);
        Volume->Settings.AutoExposureSpeedUp = 4.0f;
        Volume->Settings.AutoExposureSpeedDown = 5.0f;
        const float Saturation = FMath::Lerp(Previous.Saturation, Upcoming.Saturation, Blend) * (1.0f - Cover * 0.17f);
        Volume->Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
    }
}

TStatId USaltwindDayCycle::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USaltwindDayCycle, STATGROUP_Tickables);
}
