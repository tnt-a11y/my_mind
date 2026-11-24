#pragma once

#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "SaltwindExplorer.generated.h"

class UAnimSequence;
class UAudioComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class USaltwindWaterMovement;
class USoundBase;
class USpringArmComponent;
struct FInputActionValue;

UCLASS()
class ASaltwindExplorer : public ACharacter
{
    GENERATED_BODY()

public:
    explicit ASaltwindExplorer(const FObjectInitializer& Init);
    USaltwindWaterMovement* GetWaterMovement() const;
    bool ShouldShowControls() const { return bShowControls; }
    virtual void Tick(float DeltaTime) override;
    virtual void Landed(const FHitResult& Hit) override;

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

private:
    friend class FSaltwindTraverseStep;
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void RiseOrJump();
    void StopRiseOrJump();
    void StartDive();
    void StopDive();
    void AdvanceTime();
    void ToggleControls();
    void ResetToBeach();
    void UpdateSwimming(float DeltaSeconds);
    void UpdateAudio(float DeltaSeconds);
    void PlayFootstep(const FHitResult* Surface = nullptr, float Volume = 0.7f);

    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY() TObjectPtr<UInputMappingContext> Keys;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mouse;
    UPROPERTY() TObjectPtr<UInputAction> MoveAction;
    UPROPERTY() TObjectPtr<UInputAction> LookAction;
    UPROPERTY() TObjectPtr<UInputAction> MouseAction;
    UPROPERTY() TObjectPtr<UInputAction> JumpAction;
    UPROPERTY() TSubclassOf<UAnimInstance> LandAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> SwimStroke;
    UPROPERTY() TObjectPtr<UAnimSequence> TreadWater;
    UPROPERTY() TObjectPtr<UAnimSequence> CurrentSwimAnimation;
    UPROPERTY() TObjectPtr<UMaterialInterface> Underwater;
    UPROPERTY() TObjectPtr<UAudioComponent> Surf;
    UPROPERTY() TObjectPtr<UAudioComponent> UnderwaterAudio;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> SandSteps;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> LeafSteps;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> WaterSteps;

    FVector LastStepPosition = FVector::ZeroVector;
    float DistanceBetweenSteps = 0.0f;
    float WaterViewWeight = 0.0f;
    float WaveAudioWeight = 0.0f;
    int32 PreviousFootstep = INDEX_NONE;
    bool bLeftFoot = false;
    bool bWasSwimming = false;
    bool bRising = false;
    bool bDiving = false;
    bool bShowControls = false;
};

UCLASS()
class ASaltwindHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};

UCLASS()
class ASaltwindGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASaltwindGameMode();
};
