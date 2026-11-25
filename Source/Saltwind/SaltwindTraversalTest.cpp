#include "SaltwindDayCycle.h"
#include "SaltwindExplorer.h"
#include "SaltwindWaterMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaltwindTraversalTest, "Saltwind.Exploration.ShoreToOceanAndBack",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

class FSaltwindTraverseStep : public IAutomationLatentCommand
{
public:
    explicit FSaltwindTraverseStep(FAutomationTestBase& InTest) : Test(InTest) {}

    bool Update() override
    {
        ASaltwindExplorer* Player = FindPlayer();
        if (FPlatformTime::Seconds() - StartTime > 120)
        {
            Test.AddError(FString::Printf(TEXT("Timed out at the %d traversal stage. Position %s."), Stage,
                                          Player ? *Player->GetActorLocation().ToString() : TEXT("unknown")));
            return true;
        }
        if (!Player)
        {
            return false;
        }
        USaltwindWaterMovement* Movement = Player->GetWaterMovement();
        const FVector Location = Player->GetActorLocation();
        const float Depth = Movement->GetWaterHeight() - Location.Z;
        if (Stage == 0)
        {
            Test.TestTrue(TEXT("Sand steps have recorded variations"), Player->SandSteps.Num() >= 3);
            Test.TestTrue(TEXT("Leaf steps have recorded variations"), Player->LeafSteps.Num() >= 3);
            Test.TestNotNull(TEXT("Underwater post process is available"), Player->Underwater.Get());
            Test.TestNotNull(TEXT("Swimming stroke is bound"), Player->SwimStroke.Get());
            Test.TestNotNull(TEXT("The shoreline has a dedicated swim entry"), Player->WadeToSwim.Get());
            Test.TestNotNull(TEXT("Recorded underwater stroke is bound"), Player->SubmergedStroke.Get());
            Test.TestNotNull(TEXT("Treading loop is bound"), Player->TreadWater.Get());
            Test.TestNotNull(TEXT("Sand footprints have a decal material"), Player->FootprintMaterial.Get());
            Player->StartSprint();
            Test.TestTrue(TEXT("Shift enables a distinct dry-land sprint"),
                          Movement->IsSprinting() && Movement->GetMaxSpeed() > Movement->MaxWalkSpeed + 150.0f);
            Player->StopSprint();
            Test.TestFalse(TEXT("Releasing Shift restores normal movement"), Movement->IsSprinting());
            Test.TestTrue(TEXT("Every natural lighting phase has at least 180 seconds"),
                          USaltwindDayCycle::SecondsPerHour * 2.9f >= 180.0f);
            const AStaticMeshActor* Ocean = nullptr;
            for (TActorIterator<AStaticMeshActor> It(Player->GetWorld()); It; ++It)
            {
                if (It->ActorHasTag(TEXT("SaltwindOceanSurface")))
                {
                    Ocean = *It;
                    break;
                }
            }
            if (Test.TestNotNull(TEXT("Coral Reach has an ocean surface"), Ocean))
            {
                Test.TestTrue(TEXT("The rendered ocean cannot support a walking character"),
                              !Ocean->GetStaticMeshComponent()->IsCollisionEnabled());
            }
            DryHalfHeight = Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            VerifyTerrainCollision(Player);
            Capture(TEXT("Saltwind-Beach"));
            Stage = 1;
        }
        else if (Stage == 1)
        {
            Player->AddMovementInput(FVector(0, -1, 0));
            if (!bSawEntry && IsPlaying(Player, Player->WadeToSwim)
                && Player->GetMesh()->GetAnimInstance()->Montage_GetPosition(Player->SwimMontage) > 0.29f)
            {
                bSawEntry = true;
                Capture(TEXT("Saltwind-WaterEntry"));
                Test.AddInfo(TEXT("Played the authored step and reach before transitioning to the crawl."));
            }
            if (Movement->IsWading() && Depth + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() > 19.0f && !bSawWading)
            {
                bSawWading = true;
                Test.TestTrue(TEXT("Wading stays grounded and slows the character"),
                              Movement->IsMovingOnGround() && Movement->GetMaxSpeed() < Movement->MaxWalkSpeed);
                Capture(TEXT("Saltwind-Wading"));
            }
            if (Movement->IsInOcean() && Movement->GetBottomHeight() < -325)
            {
                Test.TestTrue(TEXT("Entered open water through the shallows"), bSawWading);
                Test.TestTrue(TEXT("The water-entry clip played between wading and swimming"), bSawEntry);
                Test.TestTrue(TEXT("The swimmer floats at the waterline"), Depth > -25.0f && Depth < 79.0f);
                Test.TestTrue(TEXT("Forward swim uses the stroke animation"), IsPlaying(Player, Player->SwimStroke));
                Test.TestTrue(TEXT("Foot contact left marks in the sand"), Player->Footprints.Num() > 0);
                Test.TestTrue(TEXT("Swim animation blends into the land animation graph"),
                              Player->GetMesh()->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
                const USkeletalMeshComponent* Mesh = Player->GetMesh();
                const FVector Hips = Mesh->GetSocketLocation(TEXT("thigh_l")) + Mesh->GetSocketLocation(TEXT("thigh_r"));
                const FVector Knees = Mesh->GetSocketLocation(TEXT("calf_l")) + Mesh->GetSocketLocation(TEXT("calf_r"));
                Test.TestTrue(TEXT("The ground foot rig leaves the crawl kick behind the swimmer"),
                              FVector::DotProduct(Hips - Knees, Player->GetActorForwardVector()) > 30.0);
                SurfaceHandPosition = Player->GetMesh()->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation();
                Test.AddInfo(FString::Printf(TEXT("Walked from the beach into open water at %s."), *Location.ToString()));
                NextCapture = Player->GetWorld()->GetTimeSeconds() + 2.7f;
                Stage = 12;
            }
        }
        else if (Stage == 12)
        {
            Player->AddMovementInput(FVector(0, -1, 0));
            const USkeletalMeshComponent* Mesh = Player->GetMesh();
            const auto DepthAt = [Mesh, Movement](FName Bone)
            {
                const FVector Point = Mesh->GetSocketLocation(Bone);
                return Movement->GetWaterHeightAt(Point) - Point.Z;
            };
            const double Pelvis = DepthAt(TEXT("pelvis"));
            const double Feet = FMath::Min(DepthAt(TEXT("ball_l")), DepthAt(TEXT("ball_r")));
            const double Head = DepthAt(TEXT("head"));
            ++WaterlineSamples;
            if (Pelvis > 3.0 && Feet > -4.0)
            {
                ++SubmergedLowerBodySamples;
            }
            if (FMath::Abs(Head) < 30.0)
            {
                ++HeadNearSurfaceSamples;
            }
            if (!bCapturedCrawl && WaterlineSamples > 15 && Pelvis > 5.0 && Feet > 4.0 && Head < 17.0)
            {
                Capture(TEXT("Saltwind-Swimming"));
                bCapturedCrawl = true;
            }
            if (Player->GetWorld()->GetTimeSeconds() > NextCapture)
            {
                const float LowerBodyShare = float(SubmergedLowerBodySamples) / WaterlineSamples;
                const float HeadShare = float(HeadNearSurfaceSamples) / WaterlineSamples;
                Test.TestTrue(TEXT("The pelvis and kick stay underwater through the surface stroke"), LowerBodyShare >= 0.85f);
                Test.TestTrue(TEXT("The head stays at the waterline while crawling"), HeadShare >= 0.75f);
                Test.AddInfo(FString::Printf(TEXT("Crawl waterline across %d frames: %.0f%% hips and feet submerged, %.0f%% head near surface."),
                    WaterlineSamples, 100.0f * LowerBodyShare, 100.0f * HeadShare));
                if (!bCapturedCrawl)
                {
                    Capture(TEXT("Saltwind-Swimming"));
                }
                Player->StartDive();
                Stage = 2;
            }
        }
        else if (Stage == 2 && Depth > 157 && Location.Z < Movement->GetBottomHeight() + 105.0f)
        {
            Test.TestTrue(TEXT("Seabed contact cannot switch a diver to walking"),
                          Movement->IsInOcean() && !Movement->IsMovingOnGround() && !Movement->IsSprinting());
            Test.TestTrue(TEXT("Diving uses the recorded breaststroke"), IsPlaying(Player, Player->SubmergedStroke));
            const FVector UnderwaterHand = Player->GetMesh()->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation();
            Test.TestTrue(TEXT("The two strokes produce distinct arm poses"),
                          FVector::Dist(UnderwaterHand, SurfaceHandPosition) > 8.0);
            Capture(TEXT("Saltwind-Underwater"));
            Test.AddInfo(FString::Printf(TEXT("Dived to %.0f centimetres below the waves."), Depth));
            Player->StopDive();
            Player->RiseOrJump();
            Stage = 3;
        }
        else if (Stage == 3 && Depth < 40)
        {
            Player->StopRiseOrJump();
            Test.TestTrue(TEXT("Surfaced in swimming mode"), Movement->IsInOcean());
            Stage = 4;
        }
        else if (Stage == 4 && IsPlaying(Player, Player->TreadWater))
        {
            Test.TestTrue(TEXT("The character treads water at the surface"), Movement->IsInOcean() && Depth < 70.0f);
            Stage = 5;
        }
        else if (Stage == 5)
        {
            Player->AddMovementInput(FVector(0, 1, 0));
            if (!Movement->IsInOcean() && Movement->IsMovingOnGround() && Location.Y > -4550)
            {
                Test.TestEqual(TEXT("Standing collision is restored"), Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), DryHalfHeight);
                Test.TestTrue(TEXT("Walking returns to the land animation blueprint"),
                              Player->GetMesh()->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
                Test.AddInfo(FString::Printf(TEXT("Returned to shore at %s."), *Location.ToString()));
                USaltwindDayCycle* Cycle = Player->GetWorld()->GetSubsystem<USaltwindDayCycle>();
                Cycle->AdvanceHours(LightHours[0] - Cycle->GetHour());
                if (AController* Controller = Player->GetController())
                {
                    ShoreView = Controller->GetControlRotation();
                    Controller->SetControlRotation(FRotator(-35.0, -90.0, 0.0));
                }
                Stage = 10;
                NextCapture = Player->GetWorld()->GetTimeSeconds() + 0.6f;
            }
        }
        else if (Stage == 10 && Player->GetWorld()->GetTimeSeconds() > NextCapture)
        {
            Capture(TEXT("Saltwind-Footprints"));
            Stage = 11;
            NextCapture += 0.3f;
        }
        else if (Stage == 11 && Player->GetWorld()->GetTimeSeconds() > NextCapture)
        {
            if (AController* Controller = Player->GetController())
            {
                Controller->SetControlRotation(ShoreView);
            }
            Stage = 6;
            NextCapture += 1.5f;
        }
        else if (Stage == 6 && Player->GetWorld()->GetTimeSeconds() > NextCapture)
        {
            static const TCHAR* Names[] = {
                TEXT("Saltwind-Morning"), TEXT("Saltwind-Noon"), TEXT("Saltwind-Afternoon"),
                TEXT("Saltwind-Evening"), TEXT("Saltwind-Night"), TEXT("Saltwind-Midnight")
            };
            USaltwindDayCycle* Cycle = Player->GetWorld()->GetSubsystem<USaltwindDayCycle>();
            Test.TestTrue(Names[LightIndex], FMath::IsNearlyEqual(Cycle->GetHour(), LightHours[LightIndex], 0.12f));
            Capture(Names[LightIndex]);
            ++LightIndex;
            Stage = 7;
            NextCapture += 0.45f;
        }
        else if (Stage == 7 && Player->GetWorld()->GetTimeSeconds() > NextCapture)
        {
            USaltwindDayCycle* Cycle = Player->GetWorld()->GetSubsystem<USaltwindDayCycle>();
            if (LightIndex < UE_ARRAY_COUNT(LightHours))
            {
                Cycle->AdvanceHours(LightHours[LightIndex] - Cycle->GetHour());
                Stage = 6;
                NextCapture = Player->GetWorld()->GetTimeSeconds() + (LightIndex >= 3 ? 5.5f : 2.25f);
            }
            else
            {
                Cycle->AdvanceHours(13.0f - Cycle->GetHour());
                Player->TeleportTo(FVector(-15150, -13800, 12), FRotator(0, 90, 0));
                if (AController* Controller = Player->GetController())
                {
                    Controller->SetControlRotation(FRotator(8, 86, 0));
                }
                Stage = 8;
                NextCapture = Player->GetWorld()->GetTimeSeconds() + 2.5f;
            }
        }
        else if (Stage == 8 && Player->GetWorld()->GetTimeSeconds() > NextCapture)
        {
            Capture(TEXT("Saltwind-OffshoreIslet"));
            Stage = 9;
            NextCapture += 0.4f;
        }
        return Stage == 9 && Player->GetWorld()->GetTimeSeconds() > NextCapture;
    }

private:
    void VerifyTerrainCollision(const ASaltwindExplorer* Player)
    {
        UStaticMeshComponent* Ground = nullptr;
        TSet<const UStaticMesh*> Rocks;
        int32 CollidingStoneGroups = 0;
        for (TActorIterator<AActor> It(Player->GetWorld()); It; ++It)
        {
            TInlineComponentArray<UStaticMeshComponent*> Components(*It);
            for (UStaticMeshComponent* Component : Components)
            {
                const UStaticMesh* Mesh = Component->GetStaticMesh();
                if (Component->ComponentHasTag(TEXT("SaltwindSandTerrain")))
                {
                    Ground = Component;
                }
                if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Saltwind/Scans/")))
                {
                    continue;
                }
                Rocks.Add(Mesh);
                if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component))
                {
                    if (Instances->GetInstanceCount() > 0 && Instances->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
                    {
                        ++CollidingStoneGroups;
                    }
                }
            }
        }
        Test.TestEqual(TEXT("All six scattered-stone groups block the pawn"), CollidingStoneGroups, 6);
        Test.TestEqual(TEXT("The map uses nine scanned rock meshes"), Rocks.Num(), 9);
        for (const UStaticMesh* Rock : Rocks)
        {
            const UBodySetup* Body = Rock->GetBodySetup();
            Test.TestTrue(Rock->GetName() + TEXT(" uses its visible rock shape for contact"),
                          Body && Body->CollisionTraceFlag == CTF_UseComplexAsSimple);
        }
        if (!Test.TestNotNull(TEXT("The shoreline has a terrain collider"), Ground))
        {
            return;
        }

        // The heights are sampled from the source terrain triangles, in Unreal centimetres.
        const FVector Samples[] = {{-4600,-4200,65.61}, {-4540,-5220,-28.43}, {-2530,-5080,7.51},
            {-6800,200,26.24}, {-4300,1200,1139.52}, {-700,1900,928.12}, {2420,4350,1791.08},
            {-15100,-12900,-732.87}, {12420,7540,-64.73}, {-4600,-6800,-320.23}};
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(SaltwindTerrainAudit), false, Player);
        double LargestError = 0.0;
        for (const FVector& Sample : Samples)
        {
            FHitResult Hit;
            const bool bHit = Ground->LineTraceComponent(Hit, FVector(Sample.X, Sample.Y, 5000),
                                                          FVector(Sample.X, Sample.Y, -3400), Query);
            if (Test.TestTrue(TEXT("Terrain depth probe hit"), bHit))
            {
                const double Error = FMath::Abs(Hit.ImpactPoint.Z - Sample.Z);
                LargestError = FMath::Max(LargestError, Error);
                if (Error > 5.0)
                {
                    Test.AddInfo(FString::Printf(TEXT("At %.0f, %.0f the collider is %.1f cm, expected %.1f cm."),
                        Sample.X, Sample.Y, Hit.ImpactPoint.Z, Sample.Z));
                }
            }
        }
        Test.TestTrue(TEXT("Sand, mud, hills and seabed collision match the visible terrain within 5 cm"), LargestError < 5.0);
        Test.AddInfo(FString::Printf(TEXT("Terrain collider largest height error across %d probes: %.2f cm."),
                                     UE_ARRAY_COUNT(Samples), LargestError));
    }

    static bool IsPlaying(const ASaltwindExplorer* Player, const UAnimSequence* Expected)
    {
        UAnimInstance* Animation = Player->GetMesh()->GetAnimInstance();
        return Animation && Expected && Expected == Player->CurrentSwimAnimation && Player->SwimMontage
            && Animation->Montage_IsPlaying(Player->SwimMontage);
    }

    static ASaltwindExplorer* FindPlayer()
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* World = Context.World();
            if (World && World->IsGameWorld())
            {
                for (TActorIterator<ASaltwindExplorer> Player(World); Player; ++Player)
                {
                    if (Player->IsLocallyControlled())
                    {
                        return *Player;
                    }
                }
            }
        }
        return nullptr;
    }

    static void Capture(const TCHAR* Name)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("SaltwindCapture")))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (FString(Name) + TEXT(".png")),false,false);
        }
    }

    FAutomationTestBase& Test;
    double StartTime = FPlatformTime::Seconds();
    static constexpr float LightHours[] = {7.0f, 12.0f, 15.5f, 17.8f, 22.5f, 2.0f};
    double NextCapture = 0.0;
    FVector SurfaceHandPosition = FVector::ZeroVector;
    FRotator ShoreView = FRotator::ZeroRotator;
    int32 Stage = 0;
    int32 LightIndex = 0;
    float DryHalfHeight = 0.0f;
    bool bSawWading = false;
    bool bSawEntry = false;
    bool bCapturedCrawl = false;
    int32 WaterlineSamples = 0;
    int32 SubmergedLowerBodySamples = 0;
    int32 HeadNearSurfaceSamples = 0;
};

bool FSaltwindTraversalTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSaltwindTraverseStep(*this));
    return true;
}

#endif
