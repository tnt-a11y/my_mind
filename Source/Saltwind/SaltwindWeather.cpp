#include "SaltwindWeather.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SaltwindDayCycle.h"
#include "SaltwindExplorer.h"
#include "SaltwindWaterMovement.h"
#include "Sound/SoundBase.h"

namespace
{
    const FName RainIntensity(TEXT("RainIntensity"));
    const FName RainWetness(TEXT("RainWetness"));
    const FTransform Hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
    constexpr int32 DropCount = 900;
    constexpr float DropRadius = 1150.0f;

    UMaterialInstanceDynamic* InitializeParticles(UInstancedStaticMeshComponent& Mesh, UStaticMesh* Plane,
                                                   const TCHAR* MaterialPath, int32 Count, bool bCustomAge)
    {
        Mesh.SetStaticMesh(Plane);
        Mesh.SetNumCustomDataFloats(bCustomAge ? 1 : 0);
        Mesh.PreAllocateInstancesMemory(Count);
        TArray<FTransform> Transforms;
        Transforms.Init(Hidden, Count);
        Mesh.AddInstances(Transforms, false, false, false);
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
        {
            return Mesh.CreateDynamicMaterialInstance(0, Material);
        }
        return nullptr;
    }
}

ASaltwindWeather::ASaltwindWeather()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("WeatherOrigin"));
    StreakMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FallingRain"));
    SplashMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RainImpacts"));
    DustMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FootstepDust"));
    for (UInstancedStaticMeshComponent* Mesh : {StreakMesh.Get(), SplashMesh.Get(), DustMesh.Get()})
    {
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionProfileName(TEXT("NoCollision"));
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetReceivesDecals(false);
        Mesh->SetVisibility(false);
    }
    RainAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("RainAmbience"));
    RainAudio->SetupAttachment(RootComponent);
    RainAudio->bAutoActivate = false;
}

void ASaltwindWeather::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_DedicatedServer)
    {
        UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
        StreakMaterial = InitializeParticles(*StreakMesh, Plane,
            TEXT("/Game/Saltwind/Materials/Weather/M_RainStreak.M_RainStreak"), DropCount, false);
        InitializeParticles(*SplashMesh, Plane,
            TEXT("/Game/Saltwind/Materials/Weather/M_RainImpact.M_RainImpact"), 112, true);
        InitializeParticles(*DustMesh, Plane,
            TEXT("/Game/Saltwind/Materials/Weather/M_FootDust.M_FootDust"), 64, true);
        Drops.SetNum(DropCount);
        DropTransforms.Init(Hidden, DropCount);
        Splashes.Particles.SetNum(112);
        Dust.Particles.SetNum(64);
        if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Saltwind/Audio/SW_Rain.SW_Rain")))
        {
            RainAudio->SetSound(Sound);
        }
    }
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
        if (Mesh->ComponentHasTag(TEXT("SaltwindSandTerrain")))
        {
            GroundMaterial = Mesh->CreateDynamicMaterialInstance(0);
        }
        else if (It->ActorHasTag(TEXT("SaltwindOceanSurface")))
        {
            OceanMaterial = Mesh->CreateDynamicMaterialInstance(0);
        }
    }
}

void ASaltwindWeather::ToggleRain()
{
    bRainTarget = !bRainTarget;
    PhaseRemaining = bRainTarget ? 95.0f : 170.0f;
}

void ASaltwindWeather::FootContact(const FHitResult& Contact, float Strength)
{
    if (Dust.Particles.IsEmpty() || Contact.ImpactNormal.Z < 0.58f)
    {
        return;
    }
    if (Wetness > 0.52f && Rain > 0.035f)
    {
        Splashes.Spawn({Contact.ImpactPoint, Contact.ImpactNormal, 0.0f, 0.35f, 20.0f * Strength});
    }
    if (Rain > 0.025f || Wetness > 0.18f)
    {
        return;
    }
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector Scatter(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f);
        const FVector Drift = Scatter * FMath::FRandRange(16.0f, 32.0f) + FVector(11.0f, 5.0f, 16.0f);
        Dust.Spawn({Contact.ImpactPoint + Scatter * 6.0f + FVector(0, 0, 7), Drift, 0.0f,
                    FMath::FRandRange(0.60f, 0.96f), FMath::FRandRange(22.0f, 38.0f) * Strength});
    }
}

void ASaltwindWeather::FParticlePool::Spawn(const FParticle& Particle)
{
    Particles[Next] = Particle;
    Next = (Next + 1) % Particles.Num();
}

void ASaltwindWeather::FParticlePool::Advance(UInstancedStaticMeshComponent& Mesh, const FVector& View,
                                              float DeltaSeconds, bool bDust)
{
    Transforms.SetNum(Particles.Num());
    Ages.SetNum(Particles.Num());
    bool bActive = false;
    for (int32 Index = 0; Index < Particles.Num(); ++Index)
    {
        FParticle& Particle = Particles[Index];
        Particle.Age += DeltaSeconds;
        if (Particle.Age >= Particle.Duration)
        {
            Transforms[Index] = Hidden;
            Ages[Index] = 1.0f;
            continue;
        }
        bActive = true;
        const float Alpha = Particle.Age / Particle.Duration;
        const FVector Location = Particle.Position + (bDust ? Particle.Direction * Particle.Age : Particle.Direction * 2.1f);
        const FQuat Rotation = FRotationMatrix::MakeFromZ(bDust ? View - Location : Particle.Direction).ToQuat();
        const float Scale = 0.01f * Particle.Size * FMath::Lerp(0.36f, 1.0f, Alpha);
        Transforms[Index] = FTransform(Rotation, Location, FVector(Scale, Scale, 1));
        Ages[Index] = Alpha;
    }
    if (bActive)
    {
        Mesh.SetCustomData(0, Ages.Num() - 1, MakeArrayView(Ages), false);
        Mesh.BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
    }
    if (bVisible != bActive)
    {
        bVisible = bActive;
        Mesh.SetVisibility(bVisible);
    }
}

void ASaltwindWeather::SpawnDrop(FRaindrop& Drop, const FVector& Center, const AActor& Player, bool bDistribute)
{
    const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
    const float Radius = DropRadius * FMath::Sqrt(FMath::FRand());
    Drop.Start = Center + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), FMath::FRandRange(590.0f, 1040.0f));
    Drop.Velocity = FVector(FMath::FRandRange(72.0f, 155.0f), FMath::FRandRange(27.0f, 71.0f),
                            -FMath::FRandRange(960.0f, 1580.0f));
    Drop.Length = FMath::FRandRange(19.0f, 41.0f);
    Drop.Normal = FVector::UpVector;
    Drop.bImpact = false;
    Drop.Duration = 2.4f;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SaltwindRain), true, this);
    Params.AddIgnoredActor(&Player);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Drop.Start, Drop.Start + Drop.Velocity * Drop.Duration, ECC_Visibility, Params))
    {
        Drop.Duration *= Hit.Time;
        Drop.Impact = Hit.ImpactPoint;
        Drop.Normal = Hit.ImpactNormal;
        Drop.bImpact = true;
    }
    if (const ASaltwindExplorer* Explorer = Cast<ASaltwindExplorer>(&Player); Explorer && Drop.Start.Z > -30.0f)
    {
        const float SeaTime = float(-Drop.Start.Z / Drop.Velocity.Z);
        if (Drop.Duration > SeaTime - 0.04f)
        {
            const FVector SeaPoint = Drop.Start + Drop.Velocity * SeaTime;
            const float Surface = Explorer->GetWaterMovement()->GetWaterHeightAt(SeaPoint);
            const float WaterTime = float((Surface - Drop.Start.Z) / Drop.Velocity.Z);
            if (WaterTime > 0.0f && WaterTime < Drop.Duration)
            {
                Drop.Duration = WaterTime;
                Drop.Impact = Drop.Start + Drop.Velocity * WaterTime;
                Drop.Normal = FVector::UpVector;
                Drop.bImpact = true;
            }
        }
    }
    Drop.Age = bDistribute ? Drop.Duration * FMath::FRand() : FMath::FRandRange(-0.11f, 0.0f);
}

void ASaltwindWeather::UpdateRain(const AActor& Player, const FVector& View, const FVector& Forward, float DeltaSeconds)
{
    const int32 Active = FMath::Clamp(FMath::RoundToInt(DropCount * Rain), 0, DropCount);
    if (Active == 0)
    {
        if (StreakMesh->IsVisible())
        {
            StreakMesh->SetVisibility(false);
        }
        return;
    }
    const FVector Center = View + Forward.GetSafeNormal2D() * 360.0f;
    for (int32 Index = 0; Index < Drops.Num(); ++Index)
    {
        FRaindrop& Drop = Drops[Index];
        if (Index >= Active)
        {
            Drop.Duration = 0.0f;
            DropTransforms[Index] = Hidden;
            continue;
        }
        Drop.Age += DeltaSeconds;
        if (Drop.Duration <= 0.0f || FVector::DistSquared2D(Drop.Start, Center) > FMath::Square(DropRadius * 1.22f))
        {
            SpawnDrop(Drop, Center, Player, true);
        }
        else if (Drop.Age >= Drop.Duration)
        {
            if (Drop.bImpact && FVector::DistSquared(Drop.Impact, View) < FMath::Square(930.0f) && FMath::FRand() < 0.27f)
            {
                Splashes.Spawn({Drop.Impact, Drop.Normal, 0.0f, FMath::FRandRange(0.24f, 0.43f),
                                FMath::FRandRange(10.0f, 25.0f)});
            }
            SpawnDrop(Drop, Center, Player, false);
        }
        if (Drop.Age < 0.0f)
        {
            DropTransforms[Index] = Hidden;
            continue;
        }
        const FVector Axis = Drop.Velocity.GetSafeNormal();
        const FVector Location = Drop.Start + Drop.Velocity * Drop.Age - Axis * (Drop.Length * 0.5f);
        const FQuat Rotation = FRotationMatrix::MakeFromXY(FVector::CrossProduct(Axis, View - Location), Axis).ToQuat();
        DropTransforms[Index] = FTransform(Rotation, Location, FVector(0.024f, Drop.Length * 0.01f, 1));
    }
    if (StreakMaterial)
    {
        StreakMaterial->SetScalarParameterValue(RainIntensity, Rain);
        StreakMesh->BatchUpdateInstancesTransforms(0, DropTransforms, true, true, true);
        StreakMesh->SetVisibility(true);
    }
}

void ASaltwindWeather::UpdateSurfaces()
{
    if (USaltwindDayCycle* Cycle = GetWorld()->GetSubsystem<USaltwindDayCycle>())
    {
        Cycle->SetRainCover(Rain);
    }
    if (GroundMaterial)
    {
        GroundMaterial->SetScalarParameterValue(RainWetness, Wetness);
        GroundMaterial->SetScalarParameterValue(RainIntensity, Rain);
    }
    if (OceanMaterial)
    {
        OceanMaterial->SetScalarParameterValue(RainIntensity, Rain);
    }
}

void ASaltwindWeather::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    PhaseRemaining -= DeltaSeconds;
    if (PhaseRemaining <= 0.0f)
    {
        ToggleRain();
    }
    const float PreviousWetness = Wetness;
    const float PreviousRain = Rain;
    Rain = FMath::FInterpConstantTo(Rain, bRainTarget ? 1.0f : 0.0f, DeltaSeconds, 0.31f);
    Wetness = FMath::FInterpConstantTo(Wetness, bRainTarget ? 1.0f : 0.0f, DeltaSeconds,
                                      bRainTarget ? Rain * 0.095f : 0.011f);
    if (Rain != PreviousRain || Wetness != PreviousWetness)
    {
        UpdateSurfaces();
    }
    const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Camera || !Player || Drops.IsEmpty())
    {
        return;
    }
    const FVector View = Camera->GetCameraLocation();
    const ASaltwindExplorer* Explorer = Cast<ASaltwindExplorer>(Player);
    const bool bUnderwater = Explorer && Explorer->GetWaterMovement()->GetImmersionDepth(View) > 8.0f;
    if (Rain > 0.001f && RainAudio->Sound)
    {
        RainAudio->SetVolumeMultiplier(Rain * (bUnderwater ? 0.025f : 0.34f));
        if (!RainAudio->IsPlaying())
        {
            RainAudio->Play();
        }
    }
    else if (RainAudio->IsPlaying())
    {
        RainAudio->Stop();
    }
    if (bUnderwater)
    {
        StreakMesh->SetVisibility(false);
        if (Splashes.bVisible || Dust.bVisible)
        {
            Splashes.Advance(*SplashMesh, View, 1.0f, false);
            Dust.Advance(*DustMesh, View, 1.0f, true);
        }
        return;
    }
    UpdateRain(*Player, View, Camera->GetCameraRotation().Vector(), DeltaSeconds);
    Splashes.Advance(*SplashMesh, View, DeltaSeconds, false);
    Dust.Advance(*DustMesh, View, DeltaSeconds, true);
}
