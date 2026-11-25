#include "SaltwindCampfire.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SaltwindItems.h"
#include "SaltwindWeather.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
    const FName StrengthParameter(TEXT("FireStrength"));
    const FName RainParameter(TEXT("RainInfluence"));
    const FName SeedParameter(TEXT("FlameSeed"));
    constexpr float Cooldown = 5.0f;

    FRotator CardRotation(float Yaw)
    {
        return FRotationMatrix::MakeFromXY(FRotator(0, Yaw, 0).Vector(), FVector::UpVector).Rotator();
    }

    UMaterialInstanceDynamic* PrepareCard(UStaticMeshComponent& Card, UStaticMesh* Plane, const TCHAR* Path)
    {
        Card.SetStaticMesh(Plane);
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path))
        {
            return Card.CreateDynamicMaterialInstance(0, Material);
        }
        Card.SetVisibility(false);
        return nullptr;
    }
}

ASaltwindCampfire::ASaltwindCampfire()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Placement"));
    FireBed = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StoneRingAndLogs"));
    FireBed->SetupAttachment(RootComponent);
    FireBed->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    FireBed->SetCanEverAffectNavigation(false);
    const auto ConfigureCard = [this](UStaticMeshComponent* Card, const FVector& Location, float Yaw, const FVector& Scale)
    {
        Card->SetupAttachment(RootComponent);
        Card->SetMobility(EComponentMobility::Movable);
        Card->SetCollisionProfileName(TEXT("NoCollision"));
        Card->SetGenerateOverlapEvents(false);
        Card->SetCanEverAffectNavigation(false);
        Card->SetCastShadow(false);
        Card->SetReceivesDecals(false);
        Card->SetRelativeLocation(Location);
        Card->SetRelativeRotation(CardRotation(Yaw));
        Card->SetRelativeScale3D(Scale);
    };
    FlameCards.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameA")));
    FlameCards.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameB")));
    FlameCards.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameC")));
    ConfigureCard(FlameCards[0], FVector(0, -4, 43), 6, FVector(.63, .77, 1));
    ConfigureCard(FlameCards[1], FVector(-5, 1, 43), 65, FVector(.60, .77, 1));
    ConfigureCard(FlameCards[2], FVector(5, 2, 43), 126, FVector(.61, .77, 1));
    SmokeCard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Smoke"));
    ConfigureCard(SmokeCard, FVector(0, 0, 131), 0, FVector(1.18, 1.82, 1));
    EmberCard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sparks"));
    ConfigureCard(EmberCard, FVector(0, 0, 82), 0, FVector(.97, 1.37, 1));

    Firelight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlickeringLight"));
    Firelight->SetupAttachment(RootComponent);
    Firelight->SetRelativeLocation(FVector(0, 0, 51));
    Firelight->SetLightColor(FLinearColor(1.0, .34, .07));
    Firelight->SetAttenuationRadius(525.0f);
    Firelight->SetSourceRadius(18.0f);
    Firelight->SetIntensity(780.0f);
    Firelight->SetCastShadows(false);

    Crackle = CreateDefaultSubobject<UAudioComponent>(TEXT("Crackle"));
    Crackle->SetupAttachment(RootComponent);
    Crackle->bAutoActivate = false;
    FSoundAttenuationSettings Attenuation;
    Attenuation.AttenuationShapeExtents = FVector(120, 0, 0);
    Attenuation.FalloffDistance = 690;
    Crackle->AdjustAttenuation(Attenuation);
}

void ASaltwindCampfire::Initialize(const USaltwindItemDefinition& Item)
{
    FireBed->SetStaticMesh(Item.Mesh);
}

void ASaltwindCampfire::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<ASaltwindWeather> ExistingWeather(GetWorld());
    Weather = ExistingWeather ? *ExistingWeather : nullptr;
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    for (int32 Index = 0; Index < FlameCards.Num(); ++Index)
    {
        if (UMaterialInstanceDynamic* Material = PrepareCard(*FlameCards[Index], Plane,
            TEXT("/Game/Saltwind/Materials/Campfire/M_CampfireFlame.M_CampfireFlame")))
        {
            Material->SetScalarParameterValue(SeedParameter, Index * 2.83f + FMath::FRand());
            FlameMaterials.Add(Material);
        }
    }
    SmokeMaterial = PrepareCard(*SmokeCard, Plane, TEXT("/Game/Saltwind/Materials/Campfire/M_CampfireSmoke.M_CampfireSmoke"));
    EmberMaterial = PrepareCard(*EmberCard, Plane, TEXT("/Game/Saltwind/Materials/Campfire/M_CampfireEmbers.M_CampfireEmbers"));
    if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Saltwind/Audio/SW_Campfire.SW_Campfire")))
    {
        Crackle->SetSound(Sound);
        Crackle->SetVolumeMultiplier(.52f);
        Crackle->Play();
    }
}

void ASaltwindCampfire::AdvanceBurn(float DeltaSeconds, float Rain)
{
    if (!IsBurning())
    {
        CoolingSeconds = FMath::Max(0.0f, CoolingSeconds - DeltaSeconds);
        return;
    }
    FuelSeconds = FMath::Max(0.0f, FuelSeconds - DeltaSeconds * (1.0f + Rain * 2.2f));
    Soak = FMath::Clamp(Soak + DeltaSeconds * FMath::Lerp(-.021f, .080f, Rain), 0.0f, 1.0f);
    if (Soak >= 1.0f || FuelSeconds <= 0.0f)
    {
        FuelSeconds = 0.0f;
        CoolingSeconds = Cooldown;
    }
}

void ASaltwindCampfire::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Rain = Weather.IsValid() ? Weather->GetRainIntensity() : 0.0f;
    const bool bWasBurning = IsBurning();
    AdvanceBurn(DeltaSeconds, Rain);
    if (bWasBurning && !IsBurning())
    {
        for (UStaticMeshComponent* Card : FlameCards)
        {
            Card->SetVisibility(false);
        }
        EmberCard->SetVisibility(false);
        Firelight->SetVisibility(false);
        Crackle->FadeOut(0.85f, 0.0f);
    }
    const float Strength = FMath::Min(1.0f, FuelSeconds / 40.0f) * (1.0f - Soak * .85f);
    UpdateEffects(Strength, Rain);
    if (!IsBurning() && CoolingSeconds <= 0.0f)
    {
        SmokeCard->SetVisibility(false);
        SetActorTickEnabled(false);
    }
}

void ASaltwindCampfire::UpdateEffects(float Strength, float Rain)
{
    for (UMaterialInstanceDynamic* Material : FlameMaterials)
    {
        Material->SetScalarParameterValue(StrengthParameter, Strength);
        Material->SetScalarParameterValue(RainParameter, Rain);
    }
    if (SmokeMaterial)
    {
        const float Smoke = IsBurning() ? .47f + Rain * .37f : (CoolingSeconds / Cooldown) * .78f;
        SmokeMaterial->SetScalarParameterValue(StrengthParameter, Smoke);
        SmokeMaterial->SetScalarParameterValue(RainParameter, Rain);
    }
    if (EmberMaterial)
    {
        EmberMaterial->SetScalarParameterValue(StrengthParameter, Strength * (1.0f - Rain * .65f));
    }
    if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        const float Yaw = (Camera->GetCameraLocation() - GetActorLocation()).Rotation().Yaw + 90;
        SmokeCard->SetWorldRotation(CardRotation(Yaw));
        EmberCard->SetWorldRotation(CardRotation(Yaw));
    }
    if (IsBurning())
    {
        const double Time = GetWorld()->GetTimeSeconds();
        const float Flicker = .87f + .09f * FMath::Sin(Time * 16.5) + .047f * FMath::Sin(Time * 31.3);
        Firelight->SetIntensity(890.0f * Strength * Flicker);
        Crackle->SetVolumeMultiplier(.52f * Strength);
    }
}
