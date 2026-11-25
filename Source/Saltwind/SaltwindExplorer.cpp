#include "SaltwindExplorer.h"
#include "SaltwindDayCycle.h"
#include "SaltwindPlayerController.h"
#include "SaltwindWaterMovement.h"
#include "SaltwindWeather.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName SwimSlot(TEXT("DefaultSlot"));
constexpr int32 MaxFootprints = 120;

USoundBase* AudioAsset(const FString& Name)
{
    return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Saltwind/Audio/%s.%s"), *Name, *Name));
}
}

ASaltwindExplorer::ASaltwindExplorer(const FObjectInitializer& Init)
    : Super(Init.SetDefaultSubobjectClass<USaltwindWaterMovement>(CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(39.0f, 89.0f);
    bUseControllerRotationYaw = false;

    USaltwindWaterMovement* Movement = GetWaterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0, 470, 0);
    Movement->MaxWalkSpeed = 395;
    Movement->MaxAcceleration = 1450;
    Movement->MinAnalogWalkSpeed = 23;
    Movement->AirControl = 0.28f;
    Movement->JumpZVelocity = 415;
    Movement->BrakingDecelerationWalking = 1160;
    Movement->bCanWalkOffLedges = true;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetRootComponent());
    CameraBoom->TargetArmLength = 345.0f;
    CameraBoom->SocketOffset = FVector(0, 37, 49);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 13.0f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->FieldOfView = 79.0f;

    GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -89), FRotator(0, -90, 0));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    GetMesh()->SetSkeletalMeshAsset(Manny.Object);
    static ConstructorHelpers::FClassFinder<UAnimInstance> Anim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    LandAnimation = Anim.Class;
    GetMesh()->SetAnimInstanceClass(LandAnimation);
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultKeys(TEXT("/Game/Input/IMC_Default.IMC_Default"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseKeys(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
    static ConstructorHelpers::FObjectFinder<UInputAction> MoveInput(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
    static ConstructorHelpers::FObjectFinder<UInputAction> ViewInput(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
    static ConstructorHelpers::FObjectFinder<UInputAction> MouseInput(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
    static ConstructorHelpers::FObjectFinder<UInputAction> JumpInput(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
    Keys = DefaultKeys.Object;
    Mouse = MouseKeys.Object;
    MoveAction = MoveInput.Object;
    LookAction = ViewInput.Object;
    MouseAction = MouseInput.Object;
    JumpAction = JumpInput.Object;

    Surf = CreateDefaultSubobject<UAudioComponent>(TEXT("CoastalSurf"));
    Surf->SetupAttachment(Camera);
    Surf->bAutoActivate = false;
    Surf->bAllowSpatialization = false;
    ForestAmbience = CreateDefaultSubobject<UAudioComponent>(TEXT("IslandAmbience"));
    ForestAmbience->SetupAttachment(Camera);
    ForestAmbience->bAutoActivate = false;
    ForestAmbience->bAllowSpatialization = false;
    UnderwaterAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("SubmergedSurf"));
    UnderwaterAudio->SetupAttachment(Camera);
    UnderwaterAudio->bAutoActivate = false;
    UnderwaterAudio->bAllowSpatialization = false;
}

USaltwindWaterMovement* ASaltwindExplorer::GetWaterMovement() const
{
    return CastChecked<USaltwindWaterMovement>(GetCharacterMovement());
}

void ASaltwindExplorer::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<ASaltwindWeather> ExistingWeather(GetWorld());
    Weather = ExistingWeather ? *ExistingWeather : GetWorld()->SpawnActor<ASaltwindWeather>();
    FootprintMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Saltwind/Materials/M_SandFootprint.M_SandFootprint"));
    Underwater = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Saltwind/Materials/Underwater/M_Underwater.M_Underwater"));
    SwimStroke = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Saltwind/Swimming/AS_SurfaceCrawl.AS_SurfaceCrawl"));
    WadeToSwim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Saltwind/Swimming/AS_WadeToSwim.AS_WadeToSwim"));
    SubmergedStroke = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Saltwind/Swimming/AS_SubmergedBreaststroke.AS_SubmergedBreaststroke"));
    TreadWater = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Saltwind/Swimming/AS_TreadWater.AS_TreadWater"));
    for (int32 Index = 1; Index <= 5; ++Index)
    {
        if (USoundBase* Sound = AudioAsset(FString::Printf(TEXT("SW_Sand_%02d"), Index)))
        {
            SandSteps.Add(Sound);
        }
        if (USoundBase* Sound = AudioAsset(FString::Printf(TEXT("SW_Leaves_%02d"), Index)))
        {
            LeafSteps.Add(Sound);
        }
    }
    if (USoundBase* Sound = AudioAsset(TEXT("SW_Wade_01")))
    {
        WaterSteps.Add(Sound);
    }
    if (USoundBase* Sound = AudioAsset(TEXT("SW_Surf")))
    {
        Surf->SetSound(Sound);
        Surf->SetVolumeMultiplier(0.26f);
        Surf->Play();
    }
    if (USoundBase* Sound = AudioAsset(TEXT("SW_Submerged")))
    {
        UnderwaterAudio->SetSound(Sound);
        UnderwaterAudio->SetVolumeMultiplier(0.0f);
        UnderwaterAudio->Play();
    }
    if (USoundBase* Sound = AudioAsset(TEXT("SW_Jungle")))
    {
        ForestAmbience->SetSound(Sound);
        ForestAmbience->SetVolumeMultiplier(0.0f);
        ForestAmbience->Play();
    }
}

void ASaltwindExplorer::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    const APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC)
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(Keys, 0);
            Subsystem->AddMappingContext(Mouse, 0);
        }
    }
    if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input))
    {
        Enhanced->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASaltwindExplorer::Move);
        Enhanced->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASaltwindExplorer::Look);
        Enhanced->BindAction(MouseAction, ETriggerEvent::Triggered, this, &ASaltwindExplorer::Look);
        Enhanced->BindAction(JumpAction, ETriggerEvent::Started, this, &ASaltwindExplorer::RiseOrJump);
        Enhanced->BindAction(JumpAction, ETriggerEvent::Completed, this, &ASaltwindExplorer::StopRiseOrJump);
        Enhanced->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ASaltwindExplorer::StopRiseOrJump);
    }
    Input->BindKey(EKeys::C, IE_Pressed, this, &ASaltwindExplorer::StartDive);
    Input->BindKey(EKeys::C, IE_Released, this, &ASaltwindExplorer::StopDive);
    Input->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &ASaltwindExplorer::StartDive);
    Input->BindKey(EKeys::Gamepad_LeftShoulder, IE_Released, this, &ASaltwindExplorer::StopDive);
    Input->BindKey(EKeys::LeftShift, IE_Pressed, this, &ASaltwindExplorer::StartSprint);
    Input->BindKey(EKeys::LeftShift, IE_Released, this, &ASaltwindExplorer::StopSprint);
    Input->BindKey(EKeys::RightShift, IE_Pressed, this, &ASaltwindExplorer::StartSprint);
    Input->BindKey(EKeys::RightShift, IE_Released, this, &ASaltwindExplorer::StopSprint);
    Input->BindKey(EKeys::N, IE_Pressed, this, &ASaltwindExplorer::AdvanceTime);
    Input->BindKey(EKeys::P, IE_Pressed, this, &ASaltwindExplorer::ToggleRain);
    Input->BindKey(EKeys::F1, IE_Pressed, this, &ASaltwindExplorer::ToggleControls);
    Input->BindKey(EKeys::R, IE_Pressed, this, &ASaltwindExplorer::ResetToBeach);
}

void ASaltwindExplorer::Move(const FInputActionValue& Value)
{
    if (Controller)
    {
        const FVector2D Axes = Value.Get<FVector2D>();
        const FRotationMatrix Facing(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
        AddMovementInput(Facing.GetUnitAxis(EAxis::X), Axes.Y);
        AddMovementInput(Facing.GetUnitAxis(EAxis::Y), Axes.X);
    }
}

void ASaltwindExplorer::Look(const FInputActionValue& Value)
{
    const FVector2D Axes = Value.Get<FVector2D>();
    AddControllerYawInput(Axes.X);
    AddControllerPitchInput(Axes.Y);
}

void ASaltwindExplorer::RiseOrJump()
{
    bRising = true;
    if (!GetWaterMovement()->IsInOcean())
    {
        Jump();
    }
}

void ASaltwindExplorer::StopRiseOrJump()
{
    bRising = false;
    StopJumping();
}

void ASaltwindExplorer::StartDive() { bDiving = true; }
void ASaltwindExplorer::StopDive() { bDiving = false; }
void ASaltwindExplorer::StartSprint() { GetWaterMovement()->SetSprintHeld(true); }
void ASaltwindExplorer::StopSprint() { GetWaterMovement()->SetSprintHeld(false); }

void ASaltwindExplorer::ReleaseMovementControls()
{
    StopSprint();
    StopDive();
    StopRiseOrJump();
    if (GetWaterMovement()->IsMovingOnGround())
    {
        GetWaterMovement()->StopMovementImmediately();
    }
}
void ASaltwindExplorer::ToggleControls() { bShowControls = !bShowControls; }

void ASaltwindExplorer::ToggleRain()
{
    if (Weather)
    {
        Weather->ToggleRain();
    }
}

void ASaltwindExplorer::AdvanceTime()
{
    GetWorld()->GetSubsystem<USaltwindDayCycle>()->PreviewNextPhase();
}

void ASaltwindExplorer::ResetToBeach()
{
    if (AGameModeBase* GameMode = GetWorld()->GetAuthGameMode())
    {
        if (AController* ActiveController = GetController())
        {
            ActiveController->UnPossess();
            Destroy();
            GameMode->RestartPlayer(ActiveController);
        }
    }
}

void ASaltwindExplorer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!IsLocallyControlled())
    {
        return;
    }
    GetWaterMovement()->SetSwimLift(float(bRising) - float(bDiving));
    GetWaterMovement()->SetGroundWetness(Weather ? Weather->GetGroundWetness() : 0.0f);
    UpdateSwimming(DeltaTime);
    UpdateAudio(DeltaTime);
    UpdateFootsteps(DeltaTime);
}

void ASaltwindExplorer::UpdateSwimming(float DeltaSeconds)
{
    USaltwindWaterMovement* Movement = GetWaterMovement();
    UAnimInstance* Animation = GetMesh()->GetAnimInstance();
    const bool bSwimming = Movement->IsInOcean();
    const float SwimDepth = Movement->GetWaterHeight() - GetActorLocation().Z;
    if (bSwimming)
    {
        const float EntryTime = Movement->GetGroundEntryTime();
        if (!bWasSwimming && EntryTime >= 0.0f && !bDiving && Animation && WadeToSwim)
        {
            SwimMontage = Animation->PlaySlotAnimationAsDynamicMontage(WadeToSwim, SwimSlot, 0.14f, 0.18f);
            if (SwimMontage)
            {
                CurrentSwimAnimation = WadeToSwim;
                bEntrySplashPending = true;
            }
        }
        const bool bBlendingFromEntry = WadeToSwim && CurrentSwimAnimation == WadeToSwim && SwimMontage;
        const bool bPlayEntry = bBlendingFromEntry && Animation && !bDiving
            && Animation->Montage_IsPlaying(SwimMontage)
            && Animation->Montage_GetPosition(SwimMontage) < WadeToSwim->GetPlayLength() - 0.15f;
        const float StrokeThreshold = CurrentSwimAnimation == TreadWater ? 56.0f : 34.0f;
        const float StrokeSpeed = (bDiving || bRising) ? Movement->Velocity.Size() : Movement->Velocity.Size2D();
        const bool bSubmergedStroke = bDiving || SwimDepth > (CurrentSwimAnimation == SubmergedStroke ? 52.0f : 75.0f);
        UAnimSequence* Stroke = bSubmergedStroke ? SubmergedStroke.Get() : SwimStroke.Get();
        UAnimSequence* Next = StrokeSpeed > StrokeThreshold && Stroke ? Stroke : TreadWater.Get();
        if (!bPlayEntry && Animation && Next && (Next != CurrentSwimAnimation || !Animation->Montage_IsPlaying(SwimMontage)))
        {
            const float BlendIn = bBlendingFromEntry ? 0.22f : (bWasSwimming ? 0.42f : 0.34f);
            const float StartTime = Next == Stroke && !bBlendingFromEntry ? Next->GetPlayLength() * 0.15f : 0.0f;
            SwimMontage = Animation->PlaySlotAnimationAsDynamicMontage(Next, SwimSlot, BlendIn, 0.55f,
                                                                       1.0f, 10000, -1.0f, StartTime);
            CurrentSwimAnimation = Next;
        }
        if (Animation && SwimMontage && !bPlayEntry)
        {
            const float SpeedFraction = FMath::Clamp(StrokeSpeed / USaltwindWaterMovement::SwimSpeed, 0.0f, 1.0f);
            Animation->Montage_SetPlayRate(SwimMontage, CurrentSwimAnimation == TreadWater
                ? 0.80f : FMath::Lerp(0.70f, 1.05f, SpeedFraction));
        }
        if ((!bWasSwimming && !bEntrySplashPending) || (bEntrySplashPending && (EntryTime > 0.36f || !bPlayEntry)))
        {
            bEntrySplashPending = false;
            if (!WaterSteps.IsEmpty())
            {
                FVector Splash = GetActorLocation();
                Splash.Z = Movement->GetWaterHeight();
                UGameplayStatics::PlaySoundAtLocation(this, WaterSteps[0], Splash, 0.57f, 0.87f);
            }
        }
    }
    else if (bWasSwimming)
    {
        if (Animation && SwimMontage)
        {
            Animation->Montage_Stop(0.60f, SwimMontage);
        }
        SwimMontage = nullptr;
        CurrentSwimAnimation = nullptr;
        bEntrySplashPending = false;
    }
    const float SwimMeshZ = CurrentSwimAnimation == TreadWater ? -71.0f
        : (CurrentSwimAnimation == SubmergedStroke ? -95.0f : -88.0f);
    const float MeshZ = FMath::FInterpTo(float(GetMesh()->GetRelativeLocation().Z), bSwimming ? SwimMeshZ : -89.0f, DeltaSeconds, 3.7f);
    GetMesh()->SetRelativeLocation(FVector(0, 0, MeshZ));
    const float CameraHeight = bSwimming
        ? FMath::GetMappedRangeValueClamped(FVector2D(45.0, 115.0), FVector2D(68.0, 34.0), SwimDepth)
        : 49.0f;
    CameraBoom->SocketOffset.Z = FMath::FInterpTo(float(CameraBoom->SocketOffset.Z), CameraHeight, DeltaSeconds, 3);
    const float CameraPitch = FMath::FInterpTo(float(Camera->GetRelativeRotation().Pitch),
        bSwimming ? -11.5f : 0.0f, DeltaSeconds, 4.0f);
    Camera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
    Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, Movement->IsSprinting() ? 86.0f : 79.0f, DeltaSeconds, 4.2f));
    GetMesh()->GlobalAnimRateScale = FMath::FInterpTo(GetMesh()->GlobalAnimRateScale,
        Movement->IsSprinting() ? 1.18f : 1.0f, DeltaSeconds, 5.0f);
    bWasSwimming = bSwimming;
}

void ASaltwindExplorer::UpdateAudio(float DeltaSeconds)
{
    const USaltwindWaterMovement* Movement = GetWaterMovement();
    const float Depth = Movement->GetImmersionDepth(Camera->GetComponentLocation());
    const float TargetWater = FMath::SmoothStep(1.0f, 28.0f, Depth);
    WaterViewWeight = FMath::FInterpTo(WaterViewWeight, TargetWater, DeltaSeconds, 5.5f);
    if (Underwater)
    {
        Camera->AddOrUpdateBlendable(Underwater, WaterViewWeight);
    }
    const float Coast = 1.0f - FMath::SmoothStep(140.0f, 720.0f, Movement->GetBottomHeight());
    const float Offshore = 1.0f - 0.56f * FMath::SmoothStep(500.0f, 1850.0f, -Movement->GetBottomHeight());
    WaveAudioWeight = FMath::FInterpTo(WaveAudioWeight, Coast * Offshore * (1.0f - WaterViewWeight), DeltaSeconds, 1.55f);
    Surf->SetVolumeMultiplier(0.64f * WaveAudioWeight);
    UnderwaterAudio->SetVolumeMultiplier(0.73f * WaterViewWeight);
    const float Hour = GetWorld()->GetSubsystem<USaltwindDayCycle>()->GetHour();
    const float Birds = FMath::SmoothStep(4.6f, 6.3f, Hour) * (1.0f - FMath::SmoothStep(17.0f, 20.2f, Hour));
    const float ForestWeight = FMath::SmoothStep(60.0f, 420.0f, Movement->GetBottomHeight());
    ForestAmbience->SetVolumeMultiplier(Birds * ForestWeight * (1.0f - WaterViewWeight) * 0.38f);
}

void ASaltwindExplorer::UpdateFootsteps(float DeltaSeconds)
{
    const USaltwindWaterMovement* Movement = GetWaterMovement();
    FootprintUpdateElapsed += DeltaSeconds;
    if (FootprintUpdateElapsed > 0.12f)
    {
        FootprintUpdateElapsed = 0.0f;
        for (int32 Index = ShorelineFootprints.Num() - 1; Index >= 0; --Index)
        {
            UDecalComponent* Mark = ShorelineFootprints[Index].Get();
            if (!Mark || Movement->GetImmersionDepth(Mark->GetComponentLocation()) > 4.0f)
            {
                if (Mark)
                {
                    Mark->SetFadeOut(0.0f, 1.4f, false);
                    Mark->SetLifeSpan(1.4f);
                }
                ShorelineFootprints.RemoveAtSwap(Index);
            }
        }
    }

    const bool bMoving = Movement->IsMovingOnGround() && Movement->Velocity.Size2D() > 36.0;
    const double Now = GetWorld()->GetTimeSeconds();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindFootContact), false, this);
    static const FName Toes[] = {TEXT("ball_l"), TEXT("ball_r")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Toes); ++Index)
    {
        const FVector Foot = GetMesh()->GetSocketLocation(Toes[Index]);
        FHitResult Contact;
        const bool bContact = bMoving && GetWorld()->LineTraceSingleByChannel(Contact, Foot + FVector(0, 0, 17),
                         Foot - FVector(0, 0, 18), ECC_WorldStatic, Query)
                         && Contact.ImpactNormal.Z > 0.56
                         && Foot.Z - Contact.ImpactPoint.Z < (bFootPlanted[Index] ? 12.0 : 8.0);
        if (bContact && !bFootPlanted[Index] && Foot.Z <= LastFootHeight[Index] + 1.2
            && Now - LastContactTime[Index] >= 0.24)
        {
            const float Volume = Movement->IsSprinting() ? 0.93f : 0.66f;
            PlayFootstep(Contact, Volume, true);
            LastContactTime[Index] = Now;
            bFootPlanted[Index] = true;
        }
        else if (!bContact)
        {
            bFootPlanted[Index] = false;
        }
        LastFootHeight[Index] = Foot.Z;
    }
}

void ASaltwindExplorer::Landed(const FHitResult& Hit)
{
    const float Force = FMath::GetMappedRangeValueClamped(FVector2D(100, 660), FVector2D(0.37, 1.0), -GetVelocity().Z);
    Super::Landed(Hit);
    if (Force > 0.45f)
    {
        PlayFootstep(Hit, Force, false);
    }
}

void ASaltwindExplorer::PlayFootstep(const FHitResult& Surface, float Volume, bool bPrint)
{
    const USaltwindWaterMovement* Movement = GetWaterMovement();
    if (Movement->IsInOcean())
    {
        return;
    }
    const float ContactDepth = Movement->GetImmersionDepth(Surface.ImpactPoint);
    const bool bWet = ContactDepth > 2.0f;
    const bool bSand = Movement->IsSandContact(Surface);
    if (Weather && !bWet && Movement->IsLooseSoilContact(Surface))
    {
        Weather->FootContact(Surface, FMath::Clamp(0.50f + Volume, 0.55f, 1.35f));
    }
    if (bPrint && bSand && ContactDepth < 6.0f)
    {
        const float TideWetness = 1.0f - FMath::SmoothStep(0.0f, 58.0f,
                                        float(Surface.ImpactPoint.Z - Movement->GetWaterHeight()));
        StampFootprint(Surface, FMath::Max(TideWetness, Weather ? Weather->GetGroundWetness() : 0.0f));
    }
    const TArray<TObjectPtr<USoundBase>>& Set = bWet && !WaterSteps.IsEmpty() ? WaterSteps
        : (bSand || LeafSteps.IsEmpty() ? SandSteps : LeafSteps);
    if (Set.IsEmpty())
    {
        return;
    }
    int32 Pick = FMath::RandHelper(Set.Num());
    if (PreviousFootstep == Pick && Set.Num() > 1)
    {
        Pick = (Pick + 1) % Set.Num();
    }
    PreviousFootstep = Pick;
    UGameplayStatics::PlaySoundAtLocation(this, Set[Pick], Surface.ImpactPoint,
                                         Volume * FMath::FRandRange(0.82f, 1.08f), FMath::FRandRange(0.91f, 1.10f));
}

void ASaltwindExplorer::StampFootprint(const FHitResult& Surface, float Wetness)
{
    if (!FootprintMaterial)
    {
        return;
    }
    const FRotator Rotation = FRotationMatrix::MakeFromXZ(-Surface.ImpactNormal, GetActorForwardVector()).Rotator();
    UDecalComponent* Mark = UGameplayStatics::SpawnDecalAtLocation(this, FootprintMaterial, FVector(5.5, 8.5, 16.5),
                                  FVector(Surface.ImpactPoint) + Surface.ImpactNormal * 0.7, Rotation);
    if (!Mark)
    {
        return;
    }
    if (UMaterialInstanceDynamic* Pigment = Mark->CreateDynamicMaterialInstance())
    {
        Pigment->SetScalarParameterValue(TEXT("Wetness"), Wetness);
    }
    const float Duration = FMath::Lerp(54.0f, 18.0f, Wetness);
    Mark->SetFadeOut(Duration, 6.0f, false);
    Mark->SetLifeSpan(Duration + 6.0f);
    Footprints.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Item) { return !Item.IsValid(); });
    if (Footprints.Num() == MaxFootprints)
    {
        Footprints[0]->DestroyComponent();
        Footprints.RemoveAt(0);
    }
    Footprints.Add(Mark);
    if (GetWaterMovement()->GetWaterHeightAt(Surface.ImpactPoint) + 34.0f > Surface.ImpactPoint.Z)
    {
        ShorelineFootprints.Add(Mark);
    }
}

void ASaltwindHUD::DrawHUD()
{
    Super::DrawHUD();
    const ASaltwindExplorer* Player = Cast<ASaltwindExplorer>(GetOwningPawn());
    if (!Player || !Canvas)
    {
        return;
    }
    UFont* Font = GEngine->GetSmallFont();
    if (const ASaltwindPlayerController* Explorer = Cast<ASaltwindPlayerController>(PlayerOwner))
    {
        if (Explorer->IsInventoryOpen())
        {
            return;
        }
        const FLinearColor Background(.012f,.026f,.023f,.89f);
        const FLinearColor Brass(.83f,.66f,.37f);
        const FLinearColor Ivory(.91f,.94f,.86f);
        const float Right = Canvas->ClipX - 24.0f;
        const int32 Total = Explorer->GetItemCount();
        const FString Count = FString::Printf(TEXT("%d %s"), Total, Total == 1 ? TEXT("item") : TEXT("items"));
        float CountWidth, CountHeight;
        GetTextSize(Count, CountWidth, CountHeight, Font);
        DrawRect(Background, Right - 87 - CountWidth, 23, 87 + CountWidth, 34);
        DrawRect(Brass, Right - 87 - CountWidth, 23, 2, 34);
        DrawText(TEXT("TAB"), Brass, Right - 74 - CountWidth, 34, Font);
        DrawText(Count, Ivory, Right - 12 - CountWidth, 34, Font);

        const FText Notice = Explorer->GetNotice();
        if (!Notice.IsEmpty())
        {
            float Width, Height;
            GetTextSize(Notice.ToString(), Width, Height, Font);
            DrawRect(Background, Right - Width - 24, 64, Width + 24, 29);
            DrawText(Notice.ToString(), Brass, Right - Width - 12, 72, Font);
        }
        const FText Target = Explorer->GetFocusedItem();
        if (!Target.IsEmpty())
        {
            const FString Name = Target.ToString();
            float Width, Height;
            GetTextSize(Name, Width, Height, Font, 1.1f);
            const float PanelWidth = FMath::Max(164.0f, Width + 82.0f);
            const float X = (Canvas->ClipX - PanelWidth) * .5f;
            const float Y = Canvas->ClipY * .69f;
            DrawRect(Background, X, Y, PanelWidth, 58);
            DrawRect(Brass, X, Y, PanelWidth, 1);
            DrawRect(FLinearColor(.16f,.19f,.13f,.95f), X + 12, Y + 13, 34, 32);
            DrawText(TEXT("E"), Brass, X + 23, Y + 22, Font, 1.1f);
            DrawText(TEXT("Interact?"), Ivory, X + 57, Y + 10, Font, 1.1f);
            DrawText(Name, Brass, X + 57, Y + 32, Font, 1.1f);
        }
    }
    const bool bShow = Player->ShouldShowControls();
    if (!bShow && GetWorld()->GetTimeSeconds() > 13.0f)
    {
        return;
    }
    const float Left = 26.0f;
    const float Top = Canvas->ClipY - 108.0f;
    DrawRect(FLinearColor(0.012f,0.026f,0.023f,0.72f), Left-9,Top-6, 380, 84);
    DrawText(TEXT("WASD  Move      Mouse  Look      Space  Jump / Surface"), FLinearColor::White, Left,Top,Font);
    DrawText(TEXT("Shift  Sprint    C  Dive    N  Preview light    R  Reset"), FLinearColor::White, Left,Top+18,Font);
    DrawText(TEXT("E  Interact    Tab  Inventory    Q  Craft"), FLinearColor::White, Left,Top+36,Font);
    DrawText(TEXT("P  Preview rain    F1  Toggle controls"), FLinearColor(0.65f,0.84f,0.76f), Left,Top+54,Font);
}

ASaltwindGameMode::ASaltwindGameMode()
{
    DefaultPawnClass = ASaltwindExplorer::StaticClass();
    PlayerControllerClass = ASaltwindPlayerController::StaticClass();
    HUDClass = ASaltwindHUD::StaticClass();
}
