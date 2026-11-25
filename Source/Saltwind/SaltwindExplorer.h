#pragma once

#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "SaltwindExplorer.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class ASaltwindWeather;
class UAudioComponent;
class UCameraComponent;
class UDecalComponent;
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
    void ReleaseMovementControls();
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
    void StartSprint();
    void StopSprint();
    void AdvanceTime();
    void ToggleRain();
    void ToggleControls();
    void ResetToBeach();
    void UpdateSwimming(float DeltaSeconds);
    void UpdateAudio(float DeltaSeconds);
    void UpdateFootsteps(float DeltaSeconds);
    void PlayFootstep(const FHitResult& Surface, float Volume, bool bPrint);
    void StampFootprint(const FHitResult& Surface, float Wetness);

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
    UPROPERTY() TObjectPtr<UAnimSequence> WadeToSwim;
    UPROPERTY() TObjectPtr<UAnimSequence> SubmergedStroke;
    UPROPERTY() TObjectPtr<UAnimSequence> TreadWater;
    UPROPERTY() TObjectPtr<UAnimSequence> CurrentSwimAnimation;
    UPROPERTY() TObjectPtr<UAnimMontage> SwimMontage;
    UPROPERTY() TObjectPtr<UMaterialInterface> Underwater;
    UPROPERTY() TObjectPtr<UMaterialInterface> FootprintMaterial;
    UPROPERTY() TObjectPtr<UAudioComponent> Surf;
    UPROPERTY() TObjectPtr<UAudioComponent> ForestAmbience;
    UPROPERTY() TObjectPtr<UAudioComponent> UnderwaterAudio;
    UPROPERTY() TObjectPtr<ASaltwindWeather> Weather;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> SandSteps;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> LeafSteps;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> WaterSteps;

    TArray<TWeakObjectPtr<UDecalComponent>> Footprints;
    TArray<TWeakObjectPtr<UDecalComponent>> ShorelineFootprints;
    double LastFootHeight[2] = {};
    double LastContactTime[2] = {-1.0, -1.0};
    bool bFootPlanted[2] = {};
    float FootprintUpdateElapsed = 0.0f;
    float WaterViewWeight = 0.0f;
    float WaveAudioWeight = 0.0f;
    int32 PreviousFootstep = INDEX_NONE;
    bool bWasSwimming = false;
    bool bEntrySplashPending = false;
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
