#include "SaltwindExplorer.h"
#include "SaltwindDayCycle.h"
#include "SaltwindWaterMovement.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float FootfallWalk = 117.0f;
constexpr float FootfallWade = 89.0f;

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
    LastStepPosition = GetActorLocation();
    Underwater = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Saltwind/Materials/Underwater/M_Underwater.M_Underwater"));
    SwimStroke = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Saltwind/Swimming/AS_SwimStroke.AS_SwimStroke"));
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
    Input->BindKey(EKeys::N, IE_Pressed, this, &ASaltwindExplorer::AdvanceTime);
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
void ASaltwindExplorer::ToggleControls() { bShowControls = !bShowControls; }

void ASaltwindExplorer::AdvanceTime()
{
    GetWorld()->GetSubsystem<USaltwindDayCycle>()->AdvanceHours(4.0f);
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
    UpdateSwimming(DeltaTime);
    UpdateAudio(DeltaTime);
}

void ASaltwindExplorer::UpdateSwimming(float DeltaSeconds)
{
    USaltwindWaterMovement* Movement = GetWaterMovement();
    const bool bSwimming = Movement->IsInOcean();
    if (bSwimming)
    {
        UAnimSequence* Next = Movement->Velocity.Size2D() > 62.0 ? SwimStroke.Get() : TreadWater.Get();
        if (Next && Next != CurrentSwimAnimation)
        {
            GetMesh()->PlayAnimation(Next, true);
            CurrentSwimAnimation = Next;
        }
        if (!bWasSwimming && !WaterSteps.IsEmpty())
        {
            UGameplayStatics::PlaySoundAtLocation(this, WaterSteps[0], GetActorLocation(), 0.57f, 0.87f);
        }
    }
    else if (bWasSwimming)
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(LandAnimation);
        CurrentSwimAnimation = nullptr;
    }
    const float MeshZ = FMath::FInterpTo(float(GetMesh()->GetRelativeLocation().Z), bSwimming ? -72.0f : -89.0f, DeltaSeconds, 8);
    GetMesh()->SetRelativeLocation(FVector(0,0,MeshZ));
    CameraBoom->SocketOffset.Z = FMath::FInterpTo(float(CameraBoom->SocketOffset.Z), bSwimming ? 34.0f : 49.0f, DeltaSeconds, 3);
    bWasSwimming = bSwimming;
}

void ASaltwindExplorer::UpdateAudio(float DeltaSeconds)
{
    const USaltwindWaterMovement* Movement = GetWaterMovement();
    const FVector Here = GetActorLocation();
    const float Depth = Movement->GetWaterHeight() - Camera->GetComponentLocation().Z;
    const float TargetWater = Movement->IsInOcean() ? FMath::SmoothStep(-2.0f, 24.0f, Depth) : 0.0f;
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

    const double Travel = FVector::Dist2D(Here, LastStepPosition);
    LastStepPosition = Here;
    if (Movement->IsMovingOnGround() && Travel < 140.0 && Movement->Velocity.Size2D() > 42.0)
    {
        DistanceBetweenSteps += Travel;
        const float Interval = Movement->IsWading() ? FootfallWade : FootfallWalk;
        if (DistanceBetweenSteps >= Interval)
        {
            PlayFootstep();
            DistanceBetweenSteps -= Interval;
        }
    }
    else
    {
        DistanceBetweenSteps = FMath::Min(DistanceBetweenSteps, 35.0f);
    }
}

void ASaltwindExplorer::Landed(const FHitResult& Hit)
{
    const float Force = FMath::GetMappedRangeValueClamped(FVector2D(100, 660), FVector2D(0.37, 1.0), -GetVelocity().Z);
    Super::Landed(Hit);
    PlayFootstep(&Hit, Force);
    DistanceBetweenSteps = 0;
}

void ASaltwindExplorer::PlayFootstep(const FHitResult* Surface, float Volume)
{
    const USaltwindWaterMovement* Movement = GetWaterMovement();
    const bool bWet = Movement->IsWading() || (Surface && Surface->ImpactPoint.Z < Movement->GetWaterHeight() - 6);
    const TArray<TObjectPtr<USoundBase>>& Set = bWet && !WaterSteps.IsEmpty() ? WaterSteps :
        (Movement->GetBottomHeight() > 270.0f && !LeafSteps.IsEmpty() ? LeafSteps : SandSteps);
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
    bLeftFoot = !bLeftFoot;
    const FVector Contact = Surface ? FVector(Surface->ImpactPoint) : GetMesh()->GetSocketLocation(bLeftFoot ? TEXT("foot_l") : TEXT("foot_r"));
    UGameplayStatics::PlaySoundAtLocation(this, Set[Pick], Contact, Volume * FMath::FRandRange(0.82f, 1.08f), FMath::FRandRange(0.91f, 1.10f));
}

void ASaltwindHUD::DrawHUD()
{
    Super::DrawHUD();
    const ASaltwindExplorer* Player = Cast<ASaltwindExplorer>(GetOwningPawn());
    if (!Player || !Canvas)
    {
        return;
    }
    const bool bShow = Player->ShouldShowControls();
    if (!bShow && GetWorld()->GetTimeSeconds() > 13.0f)
    {
        return;
    }
    UFont* Font = GEngine->GetSmallFont();
    const float Left = 26.0f;
    const float Top = Canvas->ClipY - 94.0f;
    DrawRect(FLinearColor(0.012f,0.026f,0.023f,0.72f), Left-9,Top-6, 380, 69);
    DrawText(TEXT("WASD  Move      Mouse  Look      Space  Jump / Surface"), FLinearColor::White, Left,Top,Font);
    DrawText(TEXT("C  Dive      N  Advance time      R  Return to beach"), FLinearColor::White, Left,Top+18,Font);
    DrawText(TEXT("F1  Toggle controls"), FLinearColor(0.65f,0.84f,0.76f), Left,Top+36,Font);
}

ASaltwindGameMode::ASaltwindGameMode()
{
    DefaultPawnClass = ASaltwindExplorer::StaticClass();
    HUDClass = ASaltwindHUD::StaticClass();
}
