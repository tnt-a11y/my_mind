#include "SaltwindWeather.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "SaltwindExplorer.h"
#include "SaltwindWaterMovement.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaltwindWeatherTest, "Saltwind.Exploration.WeatherAndShore",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

class FSaltwindWeatherStep final : public IAutomationLatentCommand
{
public:
    explicit FSaltwindWeatherStep(FAutomationTestBase& InTest) : Test(InTest) {}

    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 65.0)
        {
            Test.AddError(FString::Printf(TEXT("Shore weather timed out in stage %d."), Stage));
            return true;
        }
        UWorld* World = nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World() && Context.World()->IsGameWorld())
            {
                World = Context.World();
                break;
            }
        }
        ASaltwindExplorer* Player = World ? Cast<ASaltwindExplorer>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
        ASaltwindWeather* Weather = World ? Cast<ASaltwindWeather>(UGameplayStatics::GetActorOfClass(World, ASaltwindWeather::StaticClass())) : nullptr;
        if (!Weather || !Player || World->GetTimeSeconds() < 1.2)
        {
            return false;
        }
        const double Now = World->GetTimeSeconds();
        switch (Stage)
        {
        case 0:
        {
            Player->GetController()->SetIgnoreLookInput(true);
            Player->GetController()->SetControlRotation(FRotator(-9.0, -71.0, 0.0));
            VerifyShore(*World, *Player);
            Test.TestNotNull(TEXT("Rain streaks have a material"), Weather->StreakMaterial.Get());
            Test.TestNotNull(TEXT("Rain sound is loaded"), Weather->RainAudio->Sound.Get());
            Test.TestTrue(TEXT("Ground and ocean have dynamic weather materials"), Weather->GroundMaterial && Weather->OceanMaterial);
            const FVector Start = Player->GetActorLocation() + FVector(0, -18, -35);
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindWeatherContact), false, Player);
            World->LineTraceSingleByChannel(Contact, Start, Start - FVector(0, 0, 250), ECC_Visibility, Query);
            Test.TestTrue(TEXT("A footstep contact on the beach uses real sand collision"), Player->GetWaterMovement()->IsSandContact(Contact));
            Next = Now + 1.06;
            ++Stage;
            break;
        }
        case 1:
            if (Now >= Next)
            {
                Test.TestTrue(TEXT("Walking on dry sand creates drifting dust"), Weather->Dust.bVisible && Weather->Dust.Next > 0);
                Capture(TEXT("Saltwind-DryShore"));
                Weather->ToggleRain();
                Test.TestTrue(TEXT("Rain builds up after the shower starts"), Weather->Rain < 0.1f);
                Next = Now + 10.6;
                ++Stage;
            }
            else
            {
                Player->AddMovementInput(FVector(1, 0, 0));
            }
            break;
        case 2:
            if (Now >= Next)
            {
                Test.TestTrue(TEXT("The shower spawns visible rain and world impacts"), Weather->Rain > 0.92f
                    && Weather->StreakMesh->IsVisible() && Weather->Splashes.bVisible);
                Test.TestTrue(TEXT("The rain gradually saturates the ground"), Weather->Wetness > 0.67f);
                if (Weather->GroundMaterial && Weather->OceanMaterial)
                {
                    Test.TestTrue(TEXT("Terrain receives rain wetness"), Weather->GroundMaterial->K2_GetScalarParameterValue(TEXT("RainWetness")) > .65f);
                    Test.TestTrue(TEXT("Ocean receives rain for capillary rings"), Weather->OceanMaterial->K2_GetScalarParameterValue(TEXT("RainIntensity")) > .9f);
                }
                const int32 DustCount = Weather->Dust.Next;
                const int32 SplashCount = Weather->Splashes.Next;
                Weather->FootContact(Contact, 1.0f);
                Test.TestEqual(TEXT("Wet sand suppresses dry dust"), Weather->Dust.Next, DustCount);
                Test.TestNotEqual(TEXT("Wet contact makes a small splash"), Weather->Splashes.Next, SplashCount);
                Capture(TEXT("Saltwind-Rain"));
                WetAtStop = Weather->Wetness;
                Weather->ToggleRain();
                Next = Now + 4.2;
                ++Stage;
            }
            break;
        case 3:
            if (Now >= Next)
            {
                Test.TestTrue(TEXT("The rain clears and the wet ground dries slowly"), Weather->Rain < .01f
                    && Weather->Wetness < WetAtStop && Weather->Wetness > WetAtStop - .12f);
                Capture(TEXT("Saltwind-WetShore"));
                Player->GetController()->SetIgnoreLookInput(false);
                return true;
            }
            break;
        }
        return false;
    }

private:
    void VerifyShore(UWorld& World, const ASaltwindExplorer& Player)
    {
        const FName ShoreTag(TEXT("SaltwindShoreProp"));
        const FName OceanTag(TEXT("SaltwindOceanSurface"));
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindShorePropAudit), false, &Player);
        TSet<FName> PropKinds;
        int32 Vegetation = 0;
        int32 ShoreProps = 0;
        int32 TrunksHit = 0;
        int32 LogsHit = 0;
        for (TActorIterator<AActor> Actor(&World); Actor; ++Actor)
        {
            if (Actor->ActorHasTag(OceanTag))
            {
                continue;
            }
            const bool bShore = Actor->ActorHasTag(ShoreTag);
            TInlineComponentArray<UStaticMeshComponent*> Components(*Actor);
            for (UStaticMeshComponent* Component : Components)
            {
                UStaticMesh* Mesh = Component->GetStaticMesh();
                if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Saltwind/")))
                {
                    continue;
                }
                const bool bPlant = Mesh->GetPathName().Contains(TEXT("/Plants/"));
                const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
                const int32 Count = Instanced ? Instanced->GetInstanceCount() : 1;
                if (!Count || (!bShore && !bPlant && !Instanced))
                {
                    continue;
                }
                const FString Name = Mesh->GetPathName();
                Test.TestTrue(FString::Printf(TEXT("Pawn collision is enabled on %s"), *Name),
                    Component->IsQueryCollisionEnabled() && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
                UBodySetup* Body = Mesh->GetBodySetup();
                Test.TestTrue(FString::Printf(TEXT("Collision geometry exists on %s"), *Name), Body
                    && (Body->CollisionTraceFlag == CTF_UseComplexAsSimple || Body->AggGeom.GetElementCount() > 0));
                if (bPlant)
                {
                    Vegetation += Count;
                    if (Instanced && Name.Contains(TEXT("/royal_palm/")) && Mesh->GetFName() == TEXT("Trunk"))
                    {
                        FTransform Transform;
                        Instanced->GetInstanceTransform(0, Transform, true);
                        FHitResult Hit;
                        TrunksHit += Component->LineTraceComponent(Hit,
                            Transform.TransformPosition(FVector(-110,0,125)), Transform.TransformPosition(FVector(0,0,125)), Query);
                    }
                }
                if (bShore)
                {
                    ++ShoreProps;
                    for (const FName Tag : Actor->Tags)
                    {
                        if (Tag != ShoreTag)
                        {
                            PropKinds.Add(Tag);
                        }
                    }
                    if (Actor->ActorHasTag(TEXT("Log")))
                    {
                        const FVector P = Component->Bounds.Origin;
                        FHitResult Hit;
                        LogsHit += Component->LineTraceComponent(Hit,
                            P + FVector(0,0,200), P - FVector(0,0,150), Query);
                    }
                }
            }
        }
        Test.TestTrue(TEXT("A royal palm trunk and shoreline logs have physical trace hits"), TrunksHit > 0 && LogsHit > 0);
        Test.TestTrue(TEXT("The island has thousands of colliding vegetation instances"), Vegetation >= 2000);
        Test.TestTrue(TEXT("The shore has all nine prop kinds"), ShoreProps >= 35 && PropKinds.Num() == 9);
        Test.AddInfo(FString::Printf(TEXT("Verified pawn collision across %d vegetation instances and %d shore props."),
                                    Vegetation, ShoreProps));
    }

    static void Capture(const TCHAR* Name)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("SaltwindCapture")))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (FString(Name) + TEXT(".png")), false, false);
        }
    }

    FAutomationTestBase& Test;
    double Started = FPlatformTime::Seconds();
    double Next = 0.0;
    FHitResult Contact;
    float WetAtStop = 0.0f;
    int32 Stage = 0;
};

bool FSaltwindWeatherTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSaltwindWeatherStep(*this));
    return true;
}

#endif
