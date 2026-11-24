#include "SaltwindDayCycle.h"
#include "SaltwindExplorer.h"
#include "SaltwindWaterMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
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
            Test.TestNotNull(TEXT("Treading loop is bound"), Player->TreadWater.Get());
            Capture(TEXT("Saltwind-Beach"));
            Stage = 1;
        }
        else if (Stage == 1)
        {
            Player->AddMovementInput(FVector(0, -1, 0));
            if (Movement->IsInOcean() && Movement->GetBottomHeight() < -325)
            {
                Test.AddInfo(FString::Printf(TEXT("Walked from the beach into open water at %s."), *Location.ToString()));
                Player->StartDive();
                Stage = 2;
            }
        }
        else if (Stage == 2 && Depth > 157)
        {
            Capture(TEXT("Saltwind-Underwater"));
            Test.AddInfo(FString::Printf(TEXT("Dived to %.0f centimetres below the waves."), Depth));
            Player->StopDive();
            Player->RiseOrJump();
            Stage = 3;
        }
        else if (Stage == 3 && Depth < 35)
        {
            Player->StopRiseOrJump();
            Test.TestTrue(TEXT("Surfaced in swimming mode"), Movement->IsInOcean());
            Stage = 4;
        }
        else if (Stage == 4)
        {
            Player->AddMovementInput(FVector(0, 1, 0));
            if (!Movement->IsInOcean() && Movement->IsMovingOnGround() && Location.Y > -4550)
            {
                Test.AddInfo(FString::Printf(TEXT("Returned to shore at %s."), *Location.ToString()));
                USaltwindDayCycle* Cycle = Player->GetWorld()->GetSubsystem<USaltwindDayCycle>();
                Cycle->AdvanceHours(22.0f - Cycle->GetHour());
                Test.TestTrue(TEXT("The day cycle advances into night"), FMath::IsNearlyEqual(Cycle->GetHour(),22.0f,0.05f));
                Stage = 5;
                WaitForNight = Player->GetWorld()->GetTimeSeconds() + 2.5f;
            }
        }
        else if (Stage == 5 && Player->GetWorld()->GetTimeSeconds() > WaitForNight)
        {
            Capture(TEXT("Saltwind-Night"));
            Stage = 6;
            WaitForNight += 0.3f;
        }
        return Stage == 6 && Player->GetWorld()->GetTimeSeconds() > WaitForNight;
    }

private:
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
    double WaitForNight = 0.0;
    int32 Stage = 0;
};

bool FSaltwindTraversalTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FSaltwindTraverseStep(*this));
    return true;
}

#endif
