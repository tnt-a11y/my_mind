using UnrealBuildTool;

public class SaltwindTarget : TargetRules
{
    public SaltwindTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("Saltwind");
    }
}
