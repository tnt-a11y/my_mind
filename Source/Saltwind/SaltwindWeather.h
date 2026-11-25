#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SaltwindWeather.generated.h"

class UAudioComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class ASaltwindWeather : public AActor
{
    GENERATED_BODY()

public:
    ASaltwindWeather();
    void ToggleRain();
    void FootContact(const FHitResult& Contact, float Strength);
    float GetGroundWetness() const { return Wetness; }
    float GetRainIntensity() const { return Rain; }
    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    friend class FSaltwindWeatherStep;

    struct FRaindrop
    {
        FVector Start = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        FVector Impact = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;
        float Age = 0.0f;
        float Duration = 0.0f;
        float Length = 0.0f;
        bool bImpact = false;
    };

    struct FParticle
    {
        FVector Position = FVector::ZeroVector;
        FVector Direction = FVector::ZeroVector;
        float Age = 1.0f;
        float Duration = 0.0f;
        float Size = 0.0f;
    };

    struct FParticlePool
    {
        TArray<FParticle> Particles;
        TArray<FTransform> Transforms;
        TArray<float> Ages;
        int32 Next = 0;
        bool bVisible = false;
        void Spawn(const FParticle& Particle);
        void Advance(UInstancedStaticMeshComponent& Mesh, const FVector& View, float DeltaSeconds, bool bDust);
    };

    void SpawnDrop(FRaindrop& Drop, const FVector& Center, const AActor& Player, bool bDistribute);
    void UpdateRain(const AActor& Player, const FVector& View, const FVector& Forward, float DeltaSeconds);
    void UpdateSurfaces();

    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StreakMesh;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SplashMesh;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> DustMesh;
    UPROPERTY() TObjectPtr<UAudioComponent> RainAudio;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StreakMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GroundMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OceanMaterial;

    TArray<FRaindrop> Drops;
    TArray<FTransform> DropTransforms;
    FParticlePool Splashes;
    FParticlePool Dust;
    float Rain = 0.0f;
    float Wetness = 0.0f;
    float PhaseRemaining = 90.0f;
    bool bRainTarget = false;
};
