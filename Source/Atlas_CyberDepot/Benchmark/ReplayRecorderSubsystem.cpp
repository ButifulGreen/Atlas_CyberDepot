// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayRecorderSubsystem.h"
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

void UReplayRecorderSubsystem::StartRecording(const FString& FilePath)
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
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] StartRecording 실패 — World=%s, EventBus=%s"),
			World ? TEXT("있음") : TEXT("없음"), EventBus ? TEXT("있음") : TEXT("없음"));
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	// bAllowRead=true — 사용자 지시로 추가한 재생 토글이 기록 도중인 이 파일을 그대로 열어 리뷰할 수
	// 있어야 한다(기록을 멈추지 않고도 "방금까지"를 재생).
	RecordingFile.Reset(PlatformFile.OpenWrite(*FilePath, false, true));
	if (!RecordingFile)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] StartRecording 실패 — 파일을 열 수 없음(%s)"), *FilePath);
		return;
	}

	CurrentRecordingFilePath = FilePath;
	RecentEntries.Empty();
	bIsRecording = true;

	const UReplaySettings* Settings = GetDefault<UReplaySettings>();

	SnapshotHandle = EventBus->SubscribeSnapshot(FOnStateSnapshot::FDelegate::CreateUObject(this, &UReplayRecorderSubsystem::OnSnapshotReceived));
	World->GetTimerManager().SetTimer(PruneTimerHandle, this, &UReplayRecorderSubsystem::PruneAndRewrite, Settings->PruneIntervalSeconds, true);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Replay] 실시간 기록 시작(블랙박스 보존 %.0f초) — %s"), Settings->RetentionSeconds, *FilePath);
}

void UReplayRecorderSubsystem::StopRecording()
{
	if (!bIsRecording)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PruneTimerHandle);

		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
			{
				EventBus->Unsubscribe(SnapshotHandle);
			}
		}
	}

	RecordingFile.Reset();
	RecentEntries.Empty();
	bIsRecording = false;

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Replay] 기록 종료 — %s"), *CurrentRecordingFilePath);
}

void UReplayRecorderSubsystem::OnSnapshotReceived(const FStateSnapshot& Snapshot)
{
	if (!bIsRecording || !RecordingFile)
	{
		return;
	}

	RecentEntries.Add(Snapshot);

	// 버그 수정(사용자 리포트, 근본 원인) — 2-인자 편의 오버로드는 기본값이 bPrettyPrint=true(여러 줄로
	// 들여쓰기)라서, JSON Lines 형식(한 줄에 객체 하나)을 전제로 한 재생/파싱 쪽이 전부 깨졌다. 명시적으로
	// false를 넘겨 한 줄짜리 압축 JSON으로 직렬화해야 한다.
	FString Line;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Snapshot, Line, 0, 0, 0, nullptr, false))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] 스냅샷 직렬화 실패(ActorID=%s)"), *Snapshot.ActorID.ToString());
		return;
	}
	Line += TEXT("\n");

	FTCHARToUTF8 UTF8Line(*Line);
	if (!RecordingFile->Write(reinterpret_cast<const uint8*>(UTF8Line.Get()), UTF8Line.Length()))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] 스냅샷 기록 실패(ActorID=%s)"), *Snapshot.ActorID.ToString());
	}
}

void UReplayRecorderSubsystem::PruneAndRewrite()
{
	if (!bIsRecording || RecentEntries.Num() == 0)
	{
		return;
	}

	// 블랙박스 — 실제 벽시계 기준 보존 기간(에이전트 활동이 뜸해도 오래된 기록은 그대로 만료된다). 매 정리
	// 시점마다 새로 읽어, 세션 도중 프로젝트 세팅에서 값을 바꿔도 재컴파일/재시작 없이 바로 반영된다.
	const FDateTime Cutoff = FDateTime::UtcNow() - FTimespan::FromSeconds(GetDefault<UReplaySettings>()->RetentionSeconds);
	const int32 FirstKeptIndex = RecentEntries.IndexOfByPredicate([Cutoff](const FStateSnapshot& Entry)
	{
		return Entry.Timestamp >= Cutoff;
	});

	if (FirstKeptIndex == INDEX_NONE)
	{
		RecentEntries.Empty();
	}
	else if (FirstKeptIndex > 0)
	{
		RecentEntries.RemoveAt(0, FirstKeptIndex);
	}

	// 남은 항목만으로 파일을 통째로 다시 써서 디스크 상의 오래된 기록도 실제로 제거한다.
	TArray<FString> Lines;
	Lines.Reserve(RecentEntries.Num());
	for (const FStateSnapshot& Entry : RecentEntries)
	{
		FString Line;
		if (FJsonObjectConverter::UStructToJsonObjectString(Entry, Line, 0, 0, 0, nullptr, false))
		{
			Lines.Add(MoveTemp(Line));
		}
	}

	// 버그 수정(사용자 리포트) — RecordingFile.Reset(OpenWrite(...))는 새 핸들을 먼저 연 뒤에야 기존 핸들을
	// 닫기 때문에, 기존 핸들이 아직 열려있는 채로 같은 파일을 또 열려다 매번 공유 위반으로 실패했다
	// (bAllowRead=true는 읽기 공유만 허용하고 쓰기 핸들 중복은 막지 않는다). 기존 핸들을 먼저 명시적으로
	// 닫아야 한다.
	RecordingFile.Reset();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	RecordingFile.Reset(PlatformFile.OpenWrite(*CurrentRecordingFilePath, false, true));
	if (!RecordingFile)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] 블랙박스 정리 후 파일 재오픈 실패(%s)"), *CurrentRecordingFilePath);
		return;
	}

	const FString Content = Lines.Num() > 0 ? (FString::Join(Lines, TEXT("\n")) + TEXT("\n")) : FString();
	FTCHARToUTF8 UTF8Content(*Content);
	if (!RecordingFile->Write(reinterpret_cast<const uint8*>(UTF8Content.Get()), UTF8Content.Length()))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Replay] 블랙박스 정리 재기록 실패(%s)"), *CurrentRecordingFilePath);
		return;
	}
	RecordingFile->Flush();
}

void UReplayRecorderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 사용자 지시 — 수동으로 언제 시작할지 맡기면 깜빡하고 기록을 안 켜는 실수가 생길 수 있어, 월드 시작과
	// 동시에 자동으로 기록을 시작한다. 다른 액터의 BeginPlay 결과물에 의존하지 않아(구독만 걸어두면 되고,
	// 실제 데이터는 이후 에이전트들이 발행하기 시작할 때 자연스럽게 들어옴) 지연 없이 즉시 실행해도 안전하다.
	// 버그 수정(사용자 리포트, 실기 테스트) — 초 단위 타임스탬프만 쓰면 리슨 서버+클라이언트를 같은 초에
	// 켰을 때 파일명이 겹쳐 나중에 여는 쪽이 공유 위반으로 실패했다. 프로세스 ID를 덧붙였지만, "Run Under
	// One Process"(PIE 기본값)로 여러 플레이어를 켜면 서버/클라이언트 월드가 같은 프로세스에서 돌아
	// 프로세스 ID까지 똑같아 여전히 겹쳤다(실기 재현). PIE 인스턴스 ID(같은 프로세스 안에서도 PIE
	// 세션마다 다름, 패키징 빌드에선 항상 -1이라 그 경우엔 프로세스 ID가 대신 구분해준다)까지 더해야
	// 두 경우 모두 확실히 겹치지 않는다.
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("Replays") /
		FString::Printf(TEXT("%s_%u_%d.jsonl"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),
			FPlatformProcess::GetCurrentProcessId(), InWorld.GetOutermost()->GetPIEInstanceID());
	StartRecording(FilePath);
}

void UReplayRecorderSubsystem::Deinitialize()
{
	if (bIsRecording)
	{
		StopRecording();
	}

	Super::Deinitialize();
}
