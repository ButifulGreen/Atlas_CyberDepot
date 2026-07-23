// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/TrainingDataRecorderSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Benchmark/ReplaySettings.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "TimerManager.h"
#include "HAL/PlatformProcess.h"

void UTrainingDataRecorderSubsystem::StartRecording(const FString& FilePath)
{
	if (bIsRecording)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;
	if (!World || !EventBus)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[TrainingData] StartRecording 실패 — World=%s, EventBus=%s"),
			World ? TEXT("있음") : TEXT("없음"), EventBus ? TEXT("있음") : TEXT("없음"));
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	RecordingFile.Reset(PlatformFile.OpenWrite(*FilePath, false, false));
	if (!RecordingFile)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[TrainingData] StartRecording 실패 — 파일을 열 수 없음(%s)"), *FilePath);
		return;
	}

	CurrentRecordingFilePath = FilePath;
	LastEntryTimestamps.Empty();
	bIsRecording = true;

	TrainingLogHandle = EventBus->SubscribeTrainingLogEntry(FOnTrainingLogEntry::FDelegate::CreateUObject(this, &UTrainingDataRecorderSubsystem::OnTrainingLogReceived));
	World->GetTimerManager().SetTimer(FlushTimerHandle, this, &UTrainingDataRecorderSubsystem::FlushToDisk, GetDefault<UReplaySettings>()->TrainingDataFlushIntervalSeconds, true);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[TrainingData] 실시간 기록 시작 — %s"), *FilePath);
}

void UTrainingDataRecorderSubsystem::StopRecording()
{
	if (!bIsRecording)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlushTimerHandle);

		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
			{
				EventBus->Unsubscribe(TrainingLogHandle);
			}
		}
	}

	FlushToDisk();
	RecordingFile.Reset();
	bIsRecording = false;

	UE_LOG(LogFactoryDispatch, Log, TEXT("[TrainingData] 기록 종료 — %s"), *CurrentRecordingFilePath);
}

void UTrainingDataRecorderSubsystem::OnTrainingLogReceived(const FTrainingLogEntry& Entry)
{
	if (!bIsRecording || !RecordingFile)
	{
		return;
	}

	FTrainingLogEntry Mutable = Entry;
	if (const FDateTime* LastSeen = LastEntryTimestamps.Find(Entry.ActorID))
	{
		Mutable.ElapsedSinceLastEntrySeconds = static_cast<float>((Entry.Timestamp - *LastSeen).GetTotalSeconds());
	}
	LastEntryTimestamps.Add(Entry.ActorID, Entry.Timestamp);

	// 버그 수정(사용자 리포트, 근본 원인) — 2-인자 편의 오버로드는 기본값이 bPrettyPrint=true(여러 줄로
	// 들여쓰기)라서, JSON Lines 형식(한 줄에 객체 하나)을 전제로 한 재생/파싱 쪽이 전부 깨졌다. 명시적으로
	// false를 넘겨 한 줄짜리 압축 JSON으로 직렬화해야 한다.
	FString Line;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Mutable, Line, 0, 0, 0, nullptr, false))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[TrainingData] 항목 직렬화 실패(ActorID=%s)"), *Entry.ActorID.ToString());
		return;
	}
	Line += TEXT("\n");

	FTCHARToUTF8 UTF8Line(*Line);
	if (!RecordingFile->Write(reinterpret_cast<const uint8*>(UTF8Line.Get()), UTF8Line.Length()))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[TrainingData] 항목 기록 실패(ActorID=%s)"), *Entry.ActorID.ToString());
	}
}

void UTrainingDataRecorderSubsystem::FlushToDisk()
{
	if (RecordingFile)
	{
		RecordingFile->Flush();
	}
}

void UTrainingDataRecorderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 사용자 지시 — 리플레이 기록기와 동일하게 수동 시작에 기대지 않고 월드 시작과 동시에 자동으로
	// 기록을 시작한다(깜빡하고 안 켜는 실수 방지). 버그 수정(ReplayRecorderSubsystem과 동일, 실기 재현) —
	// "Run Under One Process"로 여러 플레이어를 켜면 서버/클라이언트 월드가 같은 프로세스에서 돌아
	// 프로세스 ID만으로는 안 겹쳐, PIE 인스턴스 ID까지 더한다.
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("TrainingData") /
		FString::Printf(TEXT("%s_%u_%d.jsonl"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),
			FPlatformProcess::GetCurrentProcessId(), InWorld.GetOutermost()->GetPIEInstanceID());
	StartRecording(FilePath);
}

void UTrainingDataRecorderSubsystem::Deinitialize()
{
	if (bIsRecording)
	{
		StopRecording();
	}

	Super::Deinitialize();
}
