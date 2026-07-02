// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Atlas_CyberDepot : ModuleRules
{
	public Atlas_CyberDepot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// BuildSettingsVersion.V7 하에서는 모듈 루트가 include 경로에 자동으로 추가되지 않아,
		// 코드베이스 전체가 쓰는 "Agent/X.h" 같은 모듈-상대 include 스타일을 위해 명시적으로 추가한다.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// 9단계(Docs/09_Visualization.md) UMG 위젯, Docs/11_MQTT.md JSON 직렬화,
		// 10단계(Docs/10_Benchmark_Replay.md) 게임/GPU 스레드 타이밍 계측용
		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "NavigationSystem", "UMG", "Slate", "SlateCore", "Json", "JsonUtilities", "RHI", "RenderCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
