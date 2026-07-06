# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 상태

Atlas_CyberDepot는 **Unreal Engine 5.8** C++ 프로젝트입니다. `README.md`에 따르면 현재 **기획단계**에 있으며, `Source/` 모듈에는 엔진이 생성한 기본 보일러플레이트 코드만 들어있고 `Content/`에는 아직 에셋이 없습니다. 게임 고유 코드, 게임플레이 프레임워크, 테스트 스위트는 아직 존재하지 않습니다.

아키텍처 설계(시그니처 레벨 v5)는 완료되어 `Docs/` 폴더에 섹션별로 분리되어 있습니다. 자세한 내용과 권장 구현 순서는 아래 "설계 문서(Docs/) 안내"를 참고하세요.

## 빌드 / 실행 명령어

npm, make 같은 커스텀 빌드 스크립트는 없으며, 모든 작업은 Unreal의 표준 툴체인을 통해 이루어집니다.

- **IDE 프로젝트 파일 생성**: `Atlas_CyberDepot.uproject`를 우클릭 후 "Generate Visual Studio project files" 선택 (또는 `UnrealBuildTool.exe -projectfiles -project="Atlas_CyberDepot.uproject" -game -engine` 실행).
- **빌드 (에디터)**: `Atlas_CyberDepot.sln`을 Visual Studio에서 열고 `Win64` 플랫폼의 `Development Editor` 구성을 빌드, 또는 `UnrealBuildTool`을 `Atlas_CyberDepotEditor` 타겟으로 직접 호출.
- **에디터 실행**: `Atlas_CyberDepot.uproject`를 더블클릭, 또는 빌드된 에디터 실행파일에 `-project="Atlas_CyberDepot.uproject"` 옵션을 주어 실행.
- **멀티플레이어 로컬 테스트**: 이 프로젝트는 현장 2인 + 키오스크 1인, 총 3인 협동 구조가 핵심이므로(`Docs/02_Multiplayer_RPC.md`), 에디터의 Play 설정에서 **Number of Players: 2~3**, **Net Mode: Play As Listen Server**로 켜서 테스트할 것. 순수 싱글로 로직을 다 만든 뒤 마지막에 리플리케이션을 얹으면 재작업 비용이 크므로, 3단계(Navigation) 이후부터는 가능한 한 리슨 서버 환경에서 확인한다.
- 현재 저장소에 커밋된 자동화 테스트(Automation Spec/Functional Test 등)가 없으므로, "테스트 실행" 명령은 아직 존재하지 않습니다. 자동화 테스트가 추가되면 이 섹션에 실행 명령을 함께 기록할 것.

## 모듈 / 타겟 구조

- `Source/Atlas_CyberDepot/` — 단일 기본 게임 모듈 (`Atlas_CyberDepot.Build.cs`, `.cpp`, `.h`). Public 의존성: `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`. Private 의존성은 아직 선언되어 있지 않습니다.
- `Source/Atlas_CyberDepot.Target.cs` — `Game` 타겟 (`Win64`, BuildSettingsVersion V7, IncludeOrderVersion Unreal5.8).
- `Source/Atlas_CyberDepotEditor.Target.cs` — `Editor` 타겟, 동일한 빌드 설정 사용.
- `Atlas_CyberDepot.uproject`에서 `ModelingToolsEditorMode` 플러그인이 활성화되어 있습니다 (Editor 전용).
- **신규 모듈 추가 힌트**: 설계상 다음 시스템을 구현할 때는 해당 모듈을 `Build.cs`의 `PrivateDependencyModuleNames`에 추가해야 한다.
  - AI 컨트롤러/Navigation(`Docs/04_Agent_AI.md`, `Docs/08_Navigation.md`) → `AIModule`, `NavigationSystem`
  - UI 위젯(`Docs/09_Visualization.md`) → `UMG`, `Slate`, `SlateCore`
  - 이 목록에 없는 새 외부 의존성이 필요해지면, 추가와 동시에 이 섹션을 업데이트할 것.

## 엔진/프로젝트 설정 참고사항 (`Config/DefaultEngine.ini`)

- 기본 맵/템플릿은 엔진의 `OpenWorld` 템플릿이며(`GameDefaultMap=/Engine/Maps/Templates/OpenWorld`), 원래 템플릿 이름이었던 `TP_Blank`에서의 리다이렉트가 등록되어 있습니다.
- 렌더링은 **Lumen + virtual shadow maps + Substrate + 하드웨어 레이트레이싱** 구성입니다: `r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1`, `r.Shadow.Virtual.Enable=1`, `r.RayTracing=True`, `r.Substrate=True`, 정적 라이팅은 비활성화(`r.AllowStaticLighting=False`).
- Windows 타겟 RHI는 **DX12**이며, 셰이더 포맷은 SM6로 상향되어 있습니다.
- 하드웨어 타겟팅은 Desktop / Maximum 그래픽 성능으로 설정되어 있습니다.

게임플레이 코드가 아직 존재하지 않으므로, 위의 모듈/타겟 구조 외에 문서화할 더 큰 아키텍처는 없습니다. 실제 시스템(게임플레이 프레임워크 클래스, 플러그인, 서브시스템 등)이 추가되면 이 파일을 업데이트하세요.

## 설계 문서(Docs/) 안내

시스템 상세 스펙은 이 파일에 직접 쓰지 않고 `Docs/` 폴더에 섹션별로 분리되어 있습니다(아래 "프로젝트 절대 규칙 2번" 참고). 작업을 요청할 때는 관련된 `Docs/` 파일명을 명시적으로 지정할 것 — 전체 인덱스와 파일별 권장 구현 단계는 `Docs/README.md`에 표로 정리되어 있습니다.

권장 구현 순서(의존성 기준, 문서 섹션 순서와는 다름):
1. Core — `Docs/01_EventBus_DataPipeline.md` (이벤트 버스 뼈대)
2. Agent 베이스 — `Docs/04_Agent_AI.md` 중 `AFactoryAgentBase`/`AFactoryAIController`
3. Navigation — `Docs/08_Navigation.md`
4. 인프라 — `Docs/06_Infrastructure.md`
5. 로봇 개별 동작 — `Docs/04_Agent_AI.md` 중 Atlas/TransportRobot 실제 로직
6. 배정/디스패치 — `Docs/07_TaskAssignment.md`, `Docs/03_InventoryOrder.md`
7. 정비/고장 — `Docs/05_Repair.md`
8. 멀티플레이어 — `Docs/02_Multiplayer_RPC.md`
9. 시각화/외부 통신 — `Docs/09_Visualization.md`, `Docs/11_MQTT.md`, `Docs/12_RaspberryPi.md`, `Docs/13_CSVSchema.md`
10. 벤치마크/리플레이 — `Docs/10_Benchmark_Replay.md`

`Docs/14_OpenIssues.md`는 어느 단계에서든 착수 전에 함께 확인할 것(해당 시스템에 걸린 미해결/미채택 사항이 있는지).

## 프로젝트 절대 규칙

### 1. 출력 최소화 및 플러프(Fluff) 제거
- **대화형 미사여구 생략**: "안녕하세요", "코드를 수정해 드리겠습니다", "도움이 되셨길 바랍니다" 등의 인사말과 서론/결론은 일절 생략하고 핵심 본문만 출력할 것.
- **전체 코드 재출력 금지**: 기존 소스 코드 수정 시 파일 전체를 다시 출력하지 말 것. 변경되거나 추가된 부분만 최소한의 앞뒤 컨텍스트(`// ...`)와 함께 코드 블록으로 제시할 것. **단, 신규 파일을 최초로 생성하는 경우는 예외이며 전체 내용을 출력한다.**
- **설명 최소화**: 코드를 변경한 이유에 대한 장황한 설명은 생략할 것. 주석이나 설명이 꼭 필요한 경우에만 한 줄 평으로 간결하게 작성할 것.

### 2. 컨텍스트 격리 및 탐색 제한
- **무지성 전체 검색 금지**: 특정 버그나 기능을 구현할 때, 유저가 명시하지 않은 프로젝트 내 다른 폴더나 파일을 마음대로 `grep`하거나 탐색하지 말 것.
- **Lazy Loading 활용**: 기획 스펙이나 시스템 상세 아키텍처는 이 파일에 직접 작성하지 말고, `Docs/` 폴더 내 개별 마크다운 파일로 분리한 뒤 필요할 때만 유저에게 해당 파일의 탐색 승인을 요청할 것. 어떤 파일이 어떤 시스템을 다루는지는 `Docs/README.md` 인덱스를 우선 참고할 것.

### 3. 언리얼 C++ 컴파일 에러 방지 규칙 (재작업 토큰 차단)
- **최신 표준 준수**: UE 5.8 표준에 맞게 `UPROPERTY`로 관리되는 포인터는 생 포인터 대신 반드시 `TObjectPtr<T>`를 사용할 것.
- **헤더 최소화**: `#include`는 전방 선언(Forward Declaration)을 적극 활용하여 헤더 파일 간의 꼬임을 방지하고 인클루드 카운트를 줄일 것.
- **네트워크 기본 탑재**: 변수나 함수 추가 시, 멀티플레이어 리플리케이션(Replication)이 필요한지 유저에게 먼저 의도를 짧게 묻거나 계획 단계에서 확정 지을 것.
- **컴파일 검증 의무화**: 하나의 클래스/서브시스템 단위 작업이 끝나면, 가능한 경우 실제 빌드(컴파일)를 시도해 에러 여부를 확인하고 결과를 보고할 것. 코드만 보고 "컴파일될 것"이라 단정하지 말 것.

### 4. 설계 스펙 이탈 처리 규칙
- `Docs/` 스펙과 다르게 구현해야 할 필요가 생기면, 임의로 스펙을 무시하고 진행하지 말 것. 어떤 부분이, 왜 스펙과 달라야 하는지 먼저 짧게 보고하고 유저의 승인을 받은 뒤 진행할 것. 승인 후에는 해당 `Docs/` 파일도 함께 갱신할 것(설계 문서와 실제 구현이 몰래 어긋나는 것을 방지).

### 5. 밸런싱 변수 노출 컨벤션
- `Docs/14_OpenIssues.md`에 "구현 후 튜닝 필요"로 명시된 값(예: `RestDecayIntervalSeconds`, `FullyRestedThresholdRatio`, `MaintenanceThreshold` 등)을 비롯해 플레이테스트로 조정될 가능성이 있는 수치는 하드코딩하지 말고 `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Balance|...")`로 노출할 것. 재컴파일 없이 에디터/블루프린트에서 값을 조정할 수 있어야 한다.

## Git 및 GitHub 제어 규칙 (Git/GitHub Control Rules)

### 1. 임의의 변경 및 제출(Push) 절대 금지
- **자율 커밋 금지**: 코드를 수정하더라도 유저의 명시적인 지시나 최종 승인 없이 `git commit`을 단독으로 수행하지 말 것.
- **원격 저장소 Push 금지**: 원격 저장소(GitHub)로의 `git push` 명령어는 어떠한 경우에도 자율적으로 실행하지 말 것. 반드시 유저가 직접 터미널에 입력하거나 명확히 승인했을 때만 진행할 것.

### 2. 브랜치(Branch) 관리 규칙
- **임의 브랜치 조작 금지**: 유저의 허락 없이 브랜치를 새로 생성(`git branch`, `git checkout -b`)하거나 다른 브랜치로 전환(`git switch`)하지 말 것. 모든 작업은 유저가 현재 열어둔 브랜치 안에서만 수행할 것.
- **파괴적 명령어 제한**: 저장소 히스토리를 날려버릴 수 있는 `git reset --hard`, `git clean -fd`, `git push --force` 등의 파괴적인 명령어는 절대로 스스로 실행하지 말고, 필요하다고 판단 시 유저에게 먼저 제안만 할 것.

### 3. 커밋 메시지 컨벤션 강제 (지시받았을 때만 수행)
- 유저가 커밋을 지시하는 경우, 영문 혹은 한글로 변경 사항을 명확히 요약하되 아래의 기능별 접두사(Conventional Commits) 규칙을 엄격히 준수하여 커밋 메시지를 작성할 것:
  - `feat:` 새로운 언리얼 C++ 기능/클래스 추가
  - `fix:` 버그 수정 또는 컴파일 에러 해결
  - `refactor:` 코드 구조 개선 (기능 변화 없음)
  - `docs:` CLAUDE.md나 주석 등 문서 수정
  - `chore:` 기능 추가/버그 수정이 아닌 잡다한 유지보수성 변경 (에셋 뼈대 생성, 폴더 구조 정리, 설정 파일 정리 등)
