#include "SaltwindWaterMovement.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

namespace
{
constexpr float SwimHalfHeight = 34.0f;
constexpr float SwimRadius = 29.0f;
constexpr float EnterDepth = 56.0f;
constexpr float ExitDepth = 36.0f;
constexpr float SurfaceSwimDepth = 26.0f;
constexpr float MaximumWaveHeight = 45.0f;
}

void USaltwindWaterMovement::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(TEXT("SaltwindOceanSurface")))
        {
            OceanSurface = It->GetStaticMeshComponent();
            break;
        }
    }
}

bool USaltwindWaterMovement::IsInOcean() const
{
    return MovementMode == MOVE_Custom && CustomMovementMode == OceanMode;
}

bool USaltwindWaterMovement::IsSprinting() const
{
    return bSprintHeld && IsMovingOnGround() && !bWading;
}

bool USaltwindWaterMovement::IsSandContact(const FHitResult& Hit) const
{
    return Hit.bBlockingHit && Hit.GetComponent()
        && Hit.GetComponent()->ComponentHasTag(TEXT("SaltwindSandTerrain"))
        && Hit.ImpactPoint.Z < 290.0 && Hit.ImpactNormal.Z > 0.87;
}

bool USaltwindWaterMovement::IsLooseSoilContact(const FHitResult& Hit) const
{
    if (IsSandContact(Hit))
    {
        return true;
    }
    if (!Hit.bBlockingHit || !Hit.GetComponent() || !Hit.GetComponent()->ComponentHasTag(TEXT("SaltwindSandTerrain"))
        || Hit.ImpactNormal.Z < 0.87 || Hit.ImpactPoint.Z > 1300.0)
    {
        return false;
    }
    const FVector P = Hit.ImpactPoint * 0.01;
    // Match the exposed trail painted by the terrain material.
    const double TrailX = -39 + 0.41 * (P.Y + 36) + FMath::Sin((P.Y + 38) / 9) * 4.6;
    return P.Y > -45 && P.Y < 57 && FMath::Abs(P.X - TrailX) < 1.65;
}

float USaltwindWaterMovement::GetWaterHeight() const
{
    if (!UpdatedComponent || !OceanSurface.IsValid())
    {
        return 0.0f;
    }
    return SampleWaterHeight(UpdatedComponent->GetComponentLocation(), OceanSurface->Bounds.Origin.Z, BottomHeight);
}

bool USaltwindWaterMovement::IsOverOcean(const FVector& Position) const
{
    return OceanSurface.IsValid() && OceanSurface->Bounds.GetBox().IsInsideXY(Position);
}

float USaltwindWaterMovement::GetImmersionDepth(const FVector& Position) const
{
    if (!IsOverOcean(Position))
    {
        return 0.0f;
    }
    const float SeaLevel = OceanSurface->Bounds.Origin.Z;
    if (Position.Z >= SeaLevel + MaximumWaveHeight)
    {
        return 0.0f;
    }
    return FMath::Max(0.0f, GetWaterHeightAt(Position) - float(Position.Z));
}

float USaltwindWaterMovement::GetWaterHeightAt(const FVector& Position) const
{
    if (!IsOverOcean(Position))
    {
        return float(Position.Z);
    }
    const float SeaLevel = OceanSurface->Bounds.Origin.Z;
    return SampleWaterHeight(Position, SeaLevel, FindBottomHeight(Position, SeaLevel));
}

float USaltwindWaterMovement::FindBottomHeight(const FVector& Position, float SeaLevel) const
{
    FHitResult Bed;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindSeaFloor), false, CharacterOwner);
    Query.AddIgnoredActor(OceanSurface->GetOwner());
    const double TraceBottom = FMath::Min(Position.Z - 300.0, SeaLevel - 3200.0);
    GetWorld()->LineTraceSingleByObjectType(Bed, Position + FVector(0, 0, 48),
                                          FVector(Position.X, Position.Y, TraceBottom),
                                          FCollisionObjectQueryParams(ECC_WorldStatic), Query);
    return Bed.bBlockingHit ? float(Bed.ImpactPoint.Z) : SeaLevel - 3200.0f;
}

float USaltwindWaterMovement::SampleWaterHeight(const FVector& Position, float SeaLevel, float FloorHeight) const
{
    // Keep the displacement phases and shoaling range in sync with island_materials.water_material.
    const FVector P = Position * 0.01;
    const double T = GetWorld()->GetTimeSeconds();
    const double Along = 0.76 * P.X + 0.65 * P.Y;
    const double Across = -0.65 * P.X + 0.76 * P.Y;
    const double Bend = FMath::Sin(Across * 0.16 + T * 0.31) * 0.52 + FMath::Sin(Across * 0.41 - T * 0.21) * 0.21;
    const double Phase = Along * 0.36 + Bend - T * 1.80;
    const double Crest = (FMath::Sin(Phase) - 0.12 * FMath::Cos(2.0 * Phase)) * 23.0;
    const double Cross = FMath::Sin((-0.31 * P.X + 0.95 * P.Y) * 0.55 - T * 2.30) * 8.5;
    const double Chop = FMath::Sin((0.94 * P.X - 0.34 * P.Y) * 0.78 - T * 2.73) * 3.5;
    return SeaLevel + float((Crest + Cross + Chop) * FMath::SmoothStep(-0.10, 1.40, (SeaLevel - FloorHeight) * 0.01));
}

float USaltwindWaterMovement::GetMaxSpeed() const
{
    if (IsInOcean())
    {
        return SwimSpeed;
    }
    if (bWading && IsMovingOnGround())
    {
        return FMath::GetMappedRangeValueClamped(FVector2D(8, EnterDepth), FVector2D(235, 92),
                                                ImmersionDepth);
    }
    if (IsSprinting())
    {
        return bOnSand ? 595.0f : 630.0f;
    }
    if (bOnSand && IsMovingOnGround())
    {
        return 0.91f * MaxWalkSpeed;
    }
    return Super::GetMaxSpeed();
}

void USaltwindWaterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    if (HasValidData() && !CharacterOwner->IsActorBeingDestroyed())
    {
        UpdateWaterContact(DeltaTime);
    }
    Super::TickComponent(DeltaTime, TickType, Tick);
}

void USaltwindWaterMovement::UpdateWaterContact(float DeltaTime)
{
    const FVector Position = UpdatedComponent->GetComponentLocation();
    bOnSand = IsMovingOnGround() && IsSandContact(CurrentFloor.HitResult);
    GroundFriction = bOnSand ? 6.2f : 8.0f;
    BrakingDecelerationWalking = bOnSand ? 880.0f : 1160.0f;
    if (GroundWetness > 0.35f && IsMovingOnGround() && IsLooseSoilContact(CurrentFloor.HitResult))
    {
        const float Mud = FMath::SmoothStep(0.35f, 0.95f, GroundWetness);
        GroundFriction *= 1.0f - Mud * 0.24f;
        BrakingDecelerationWalking *= 1.0f - Mud * 0.15f;
    }
    bWading = false;
    if (!IsOverOcean(Position))
    {
        if (IsInOcean())
        {
            RestoreWalkingCapsule(DeltaTime);
        }
        return;
    }
    UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
    BottomHeight = FindBottomHeight(Position, OceanSurface->Bounds.Origin.Z);
    const float WaterHeight = GetWaterHeight();
    const float Depth = WaterHeight - BottomHeight;
    ImmersionDepth = WaterHeight - float(Position.Z) + Capsule->GetScaledCapsuleHalfHeight();
    SwimClearanceHeight = BottomHeight + SwimHalfHeight * Capsule->GetShapeScale() + 4.0f;

    if (IsInOcean())
    {
        if (GroundEntryTime >= 0.0f)
        {
            GroundEntryTime += DeltaTime;
        }
        if (Depth < ExitDepth && Position.Z > WaterHeight - 33.0f)
        {
            RestoreWalkingCapsule(DeltaTime);
        }
        if (IsInOcean())
        {
            Capsule->SetCapsuleSize(
                FMath::FInterpConstantTo(Capsule->GetUnscaledCapsuleRadius(), SwimRadius, DeltaTime, 55.0f),
                FMath::FInterpConstantTo(Capsule->GetUnscaledCapsuleHalfHeight(), SwimHalfHeight, DeltaTime, 193.0f));
        }
        return;
    }

    bWading = IsMovingOnGround() && ImmersionDepth > 7.0f && Depth > 7.0f;
    if (Depth > EnterDepth && ImmersionDepth > EnterDepth - 5.0f && (IsFalling() || IsMovingOnGround()))
    {
        GroundEntryTime = IsMovingOnGround() ? 0.0f : -1.0f;
        EntryHeightAboveWater = FMath::Clamp(float(Position.Z) - WaterHeight, -10.0f, 42.0f);
        DryHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
        DryRadius = Capsule->GetUnscaledCapsuleRadius();
        Velocity.Z = FMath::Clamp(Velocity.Z, -110.0, 20.0);
        bNearSurface = Position.Z > WaterHeight - 54.0f;
        bWading = false;
        SetMovementMode(MOVE_Custom, OceanMode);
    }
}

void USaltwindWaterMovement::RestoreWalkingCapsule(float DeltaTime)
{
    UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
    const FVector Position = UpdatedComponent->GetComponentLocation();
    const float ScaledHalfHeight = DryHalfHeight * Capsule->GetShapeScale();
    FVector StandingPosition = Position;
    StandingPosition.Z = FMath::Max(Position.Z, BottomHeight + ScaledHalfHeight + 5.0);
    const FQuat Rotation = UpdatedComponent->GetComponentQuat();
    const FCollisionShape StandingCapsule = FCollisionShape::MakeCapsule(DryRadius * Capsule->GetShapeScale(), ScaledHalfHeight);
    if (OverlapTest(StandingPosition, Rotation, Capsule->GetCollisionObjectType(), StandingCapsule, CharacterOwner))
    {
        return;
    }
    FHitResult Hit;
    const FVector Rise = (StandingPosition - Position).GetClampedToMaxSize(160.0f * DeltaTime);
    SafeMoveUpdatedComponent(Rise, Rotation, true, Hit);
    if (!UpdatedComponent->GetComponentLocation().Equals(StandingPosition, 0.1))
    {
        return;
    }
    Capsule->SetCapsuleSize(DryRadius, DryHalfHeight);
    Velocity.Z = FMath::Min(Velocity.Z, 0.0);
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
        else if (Depth < SurfaceSwimDepth + 14.0f)
        {
            bNearSurface = true;
        }

        const FVector Plane(Acceleration.X, Acceleration.Y, 0.0f);
        const float EntryProgress = GroundEntryTime < 0.0f || Input < 0.0f
            ? 1.0f : FMath::SmoothStep(0.10f, 0.68f, GroundEntryTime);
        const float ActiveSwimSpeed = FMath::Lerp(96.0f, SwimSpeed, EntryProgress);
        const FVector Target = Plane / FMath::Max(GetMaxAcceleration(), 1.0f) * ActiveSwimSpeed;
        FVector Horizontal(Velocity.X, Velocity.Y, 0.0);
        Horizontal = FMath::VInterpTo(Horizontal, Target, Step, Plane.IsNearlyZero() ? 2.1f : 3.1f);

        const float FloatOffset = FMath::Lerp(EntryHeightAboveWater, -SurfaceSwimDepth, EntryProgress);
        const double FloatHeight = FMath::Max(double(Surface + FloatOffset), double(SwimClearanceHeight));
        const float SurfaceZ = FMath::Clamp(float(FloatHeight - Location.Z) * 3.25f, -125.0f, 160.0f);
        const float Clearance = FMath::Clamp(float((Location.Z - SwimClearanceHeight) / 42.0), 0.0f, 1.0f);
        const float TargetZ = Input < 0.0f ? Input * 125.0f * Clearance : (bNearSurface || Input > 0.0f ? SurfaceZ : 0.0f);
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
