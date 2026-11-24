#include "SaltwindWaterMovement.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
constexpr float SwimHalfHeight = 49.0f;
constexpr float EnterDepth = 122.0f;
constexpr float ExitDepth = 88.0f;
}

bool USaltwindWaterMovement::IsInOcean() const
{
    return MovementMode == MOVE_Custom && CustomMovementMode == OceanMode;
}

float USaltwindWaterMovement::GetWaterHeight() const
{
    if (!UpdatedComponent || !GetWorld())
    {
        return 0.0f;
    }

    // Same cross-swell phases and depth attenuation as the ocean material.
    const FVector P = UpdatedComponent->GetComponentLocation() * 0.01;
    const double T = GetWorld()->GetTimeSeconds();
    const double Along = 0.76 * P.X + 0.65 * P.Y;
    const double Across = -0.65 * P.X + 0.76 * P.Y;
    const double Bend = FMath::Sin(Across * 0.34 + T * 0.39) * 0.6 + FMath::Sin(Across * 0.72 - T * 0.22) * 0.27;
    const double Swell = FMath::Sin(Along * 1.57 + Bend - T * 1.78) * 10.5;
    const double Cross = FMath::Sin((-0.31 * P.X + 0.95 * P.Y) * 2.24 - T * 2.27) * 4.0;
    const double Ripple = FMath::Sin((0.94 * P.X - 0.34 * P.Y) * 4.19 - T * 2.64) * 1.4;
    return float((Swell + Cross + Ripple) * FMath::SmoothStep(-0.10, 0.83, -BottomHeight * 0.01));
}

float USaltwindWaterMovement::GetMaxSpeed() const
{
    if (IsInOcean())
    {
        return SwimSpeed;
    }
    if (bWading && IsMovingOnGround())
    {
        return FMath::GetMappedRangeValueClamped(FVector2D(8, EnterDepth), FVector2D(MaxWalkSpeed, 165),
                                                GetWaterHeight() - BottomHeight);
    }
    return Super::GetMaxSpeed();
}

void USaltwindWaterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    if (HasValidData() && !CharacterOwner->IsActorBeingDestroyed())
    {
        UpdateWaterContact();
    }
    Super::TickComponent(DeltaTime, TickType, Tick);
}

void USaltwindWaterMovement::UpdateWaterContact()
{
    const FVector Position = UpdatedComponent->GetComponentLocation();
    UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
    FHitResult Bed;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindSeaFloor), false, CharacterOwner);
    GetWorld()->LineTraceSingleByChannel(Bed, Position + FVector(0, 0, 48),
                                       FVector(Position.X, Position.Y, -3200), ECC_WorldStatic, Query);
    BottomHeight = Bed.bBlockingHit ? Bed.ImpactPoint.Z : -2600.0f;
    const float WaterHeight = GetWaterHeight();
    const float Depth = WaterHeight - BottomHeight;
    const float Feet = float(Position.Z) - Capsule->GetScaledCapsuleHalfHeight();

    bWading = !IsInOcean() && IsMovingOnGround() && Feet < WaterHeight - 9 && Depth > 9;

    if (IsInOcean())
    {
        if (Depth < ExitDepth && Position.Z > WaterHeight - 85)
        {
            RestoreWalkingCapsule();
        }
    }
    else if (Depth > EnterDepth && Position.Z < WaterHeight + 31 && (IsFalling() || IsMovingOnGround()))
    {
        DryHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
        Capsule->SetCapsuleHalfHeight(SwimHalfHeight);
        Velocity.Z = FMath::Clamp(Velocity.Z, -42.0, 35.0);
        bNearSurface = true;
        bWading = false;
        SetMovementMode(MOVE_Custom, OceanMode);
    }
}

void USaltwindWaterMovement::RestoreWalkingCapsule()
{
    UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
    const float Raise = DryHalfHeight - Capsule->GetUnscaledCapsuleHalfHeight();
    const float Clearance = FMath::Max(0.0f, BottomHeight + DryHalfHeight + 3.0f - UpdatedComponent->GetComponentLocation().Z);
    FHitResult Hit;
    SafeMoveUpdatedComponent(FVector(0, 0, FMath::Max(Raise, Clearance)), UpdatedComponent->GetComponentQuat(), true, Hit);
    if (Hit.bBlockingHit)
    {
        return;
    }
    Capsule->SetCapsuleHalfHeight(DryHalfHeight);
    Velocity.Z = 0;
    SetMovementMode(MOVE_Falling);
}

void USaltwindWaterMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
    if (!IsInOcean())
    {
        Super::PhysCustom(DeltaTime, Iterations);
        return;
    }

    while (DeltaTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && HasValidData())
    {
        const float Step = GetSimulationTimeStep(DeltaTime, ++Iterations);
        DeltaTime -= Step;
        const FVector Location = UpdatedComponent->GetComponentLocation();
        const float Surface = GetWaterHeight();
        const float Depth = Surface - Location.Z;
        const float Input = SwimLift;
        if (Input < 0.0f)
        {
            bNearSurface = false;
        }
        else if (Depth < 33.0f)
        {
            bNearSurface = true;
        }

        const FVector Plane = FVector(Acceleration.X, Acceleration.Y, 0.0f).GetClampedToMaxSize(GetMaxAcceleration());
        const FVector Drift = FVector(5.0, FMath::Sin(Location.X * 0.0006 + GetWorld()->GetTimeSeconds() * 0.24) * 7.0, 0.0);
        const FVector Target = Plane * (SwimSpeed / GetMaxAcceleration()) + Drift;
        FVector Horizontal(Velocity.X, Velocity.Y, 0.0);
        Horizontal = FMath::VInterpTo(Horizontal, Target, Step, Plane.IsNearlyZero() ? 2.1f : 3.1f);

        const float SwimZ = Input * (Input > 0.0f ? 150.0f : 116.0f);
        const float SurfaceZ = FMath::Clamp((Depth - 20.0f) * 3.5f, -100.0f, 155.0f);
        const float TargetZ = Input < 0 ? SwimZ : (bNearSurface || Depth < 31.0f ? SurfaceZ : SwimZ);
        const float Vertical = FMath::FInterpTo(float(Velocity.Z), TargetZ, Step, 4.5f);
        const FVector Wanted(Horizontal.X, Horizontal.Y, Vertical);

        FHitResult Hit;
        SafeMoveUpdatedComponent(Wanted * Step, UpdatedComponent->GetComponentQuat(), true, Hit);
        if (Hit.bBlockingHit)
        {
            HandleImpact(Hit, Step, Wanted * Step);
            SlideAlongSurface(Wanted * Step, 1.0f - Hit.Time, Hit.Normal, Hit, true);
        }
        Velocity = (UpdatedComponent->GetComponentLocation() - Location) / Step;
    }
}
