// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// Docs/11_MQTT.md §11 — Eclipse Paho MQTT C 비동기 클라이언트(paho.mqtt.c, MQTTAsync) 서드파티 래퍼.
// 실제 배치 구조(Win64 하위 폴더 없이 평평한 구조):
//   Source/ThirdParty/PahoMQTT/Includes/MQTTAsync.h 등 C 헤더 (Includes/mqtt/에는 미사용 C++ 래퍼 헤더도 함께 있음)
//   Source/ThirdParty/PahoMQTT/Libraries/paho-mqtt3as.lib
//   Source/ThirdParty/PahoMQTT/Binaries/paho-mqtt3as.dll (+ .pdb)
// paho-mqtt3a/3c/3cs, paho-mqttpp3(C++ 래퍼)도 함께 배치돼 있지만 MyMQTTClient.cpp는
// 순수 C 비동기+SSL API(MQTTAsync.h)만 사용하므로 paho-mqtt3as만 링크한다.
public class PahoMQTT : ModuleRules
{
	public PahoMQTT(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Includes"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(ModuleDirectory, "Libraries");
			string BinPath = Path.Combine(ModuleDirectory, "Binaries");
			string DllName = "paho-mqtt3as.dll";
			string DllPath = Path.Combine(BinPath, DllName);

			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "paho-mqtt3as.lib"));

			if (File.Exists(DllPath))
			{
				PublicDelayLoadDLLs.Add(DllName);
				RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", DllName), DllPath);
			}
		}
	}
}
