# AI Play 봇 MVP 사양: 티라노 / Python

## 1. 고정 범위

첫 번째 AI Play 봇은 아래 한 가지 조건만 지원한다.

- 실행 파일: `ranker_rebuild.exe`
- 실행 방식: 혼자하기에 해당하는 로컬 1대1 경기
- 맵: `Maps/Rank Maps/(4) Python Jurassic v0.1.trk`
- 맵 크기: 128×128 tiles
- 봇 진영: 티라노(내부 tribe id 2)
- 상대: 기존 규칙 기반 Computer AI, 티라노
- 사람 슬롯: local owner 0
- 봇 제어 슬롯: `Computer(AI)`로 선택한 owner(기본 1)
- 나머지 owner 2~7: disabled
- 초기 목표: 자원 운용, 생산, 이동, 공격을 수행해 한 경기를 끝까지 진행

첫 MVP에서는 P2P, 여러 맵, 다른 진영, 사람과 비슷한 마우스 조작, 강화학습을 지원하지 않는다. 먼저 구조화된 게임 상태와 정상 gameplay packet을 이용하는 `ScriptedBot`을 완성한다.

## 2. 기준 리플레이

- 파일: `RankerOCPV_Win/Replays/Debug_replays/gameplay_1.ply`
- 크기: 115,890 bytes
- SHA-256: `0B3A2721E1B9F5CE88457AEE68F8BCBBAA962523EFDC9A8398E98FC2470C815F`
- gameplay frame 범위: 77~8,607
- ordered gameplay packet: 1,612개
- trailing packet bytes: 0
- packet 발행 owner: 모두 local owner 0

리플레이 헤더에는 과거 저장 환경의 절대 경로가 들어 있지만, 현재 작업공간에서는 다음 파일을 정식 기준 맵으로 사용한다.

`RankerOCPV_Win/Maps/Rank Maps/(4) Python Jurassic v0.1.trk`

동일 이름의 `Favorite`, `Old Maps/Rank Maps`, `Rank Maps` 사본 세 개는 현재 모두 같은 내용이다.

- 맵 파일 크기: 102,893 bytes
- 맵 SHA-256: `C2D4E81FF81FB189BADE261ED20B7EA881ABC9D6B8D67ADF01244C693E637453`

학습 및 평가 metadata에는 절대 경로 대신 맵의 정규화된 상대 경로와 SHA-256을 기록한다.

## 3. 리플레이에서 확인한 세션 정보

리플레이의 Link startup metadata는 다음과 같다.

| 항목 | 값 |
|---|---|
| local owner | 0 |
| session mode | 1 |
| owner 0 상태 | human/local (`0`) |
| owner 1 상태 | Computer (`1`) |
| owner 0 tribe | Tyrano (`2`) |
| owner 1 tribe | Tyrano (`2`) |
| owner 0 map slot | 2 |
| owner 1 map slot | 0 |
| owner 2~7 상태 | disabled (`0x14`) |

맵의 `P_PLAYE` 레코드에는 네 개의 기본 시작 위치와 진영 기본값이 들어 있지만, 실제 리플레이 세션의 owner/tribe 배치는 위 startup metadata가 덮어쓴다. AI 환경도 맵 레코드만 읽어서 플레이어 구성을 추측하지 않고 이 세션 초기화 경로를 사용해야 한다.

## 4. 관찰된 명령 범위

전체 33개 Debug replay의 공통 명령 범위와 AI 노출 정책은 [AI Play 봇 명령 카탈로그](AI_PLAY_BOT_COMMAND_CATALOG.md)에 정리한다. 이 절은 그중 첫 MVP와 같은 티라노/Python 기준 리플레이에서 실제로 사용된 범위다.

`gameplay_1.ply`에서 확인한 packet 분포는 다음과 같다.

| Subtype | 개수 | 관찰된 command | MVP에서의 의미 |
|---|---:|---|---|
| `0x01` | 78 | `0x00`, `0x20`, `0x21`, `0x24` | 생산/primary action 및 취소 계열 |
| `0x02` | 1,413 | `0x03`, `0x04`, `0x05`, `0x06`, `0x07`, `0x23` | 유닛 이동·전투·작업 명령 |
| `0x08` | 58 | `0x1f` | 생산 건물 rally vector |
| `0x0b` | 56 | `0x0d` | 유닛 status-mask 변경 |
| `0x0c` | 3 | `0x14`, `0x16`, `0x19` | 생산/배치 선택 계열 |
| `0x0f` | 1 | `0x01` | catch-up 상태; AI 행동 아님 |
| `0x13` | 2 | `0x00` | 경기 종료/비활성 처리 |
| `0x1d` | 1 | `0x00` | 종료 vote 완료 |

Subtype `0x02`의 세부 관찰은 다음과 같다.

| command | 일반 | queued | 우선 해석 |
|---:|---:|---:|---|
| `0x03` | 4 | 0 | target/spawn 계열; 의미 추가 확인 필요 |
| `0x04` | 802 | 0 | target 또는 point 명령 |
| `0x05` | 543 | 0 | 조건부 target/attack-move 계열 |
| `0x06` | 11 | 0 | tile 정렬 작업/배치 계열 |
| `0x07` | 31 | 10 | 채집 또는 point 작업; queued 사용 확인 |
| `0x23` | 10 | 0 | value-transfer 계열; 의미 추가 확인 필요 |

Packet 수는 사람의 클릭 수와 같지 않다. 여러 유닛에 같은 명령을 내리면 유닛별 packet이 생기고, 일부 명령은 보조 packet을 함께 발행한다. 향후 모방학습 dataset은 인접 packet을 그대로 독립 action으로 취급하지 않고 하나의 semantic action으로 묶어야 한다.

## 5. Observation v1

첫 `AiObservation`은 렌더링 상태와 분리된 읽기 전용 스냅샷으로 만든다.

### 세션

- schema version
- simulation frame
- map relative path와 SHA-256
- 맵 크기
- local owner와 tribe
- 활성 owner와 관계 mask
- 경기 종료 여부와 종료 사유

### 자신의 상태

- primary/secondary/aux 자원
- 사용/예약/최대 인구
- 사용할 수 있는 생산 order와 비용
- 현재 생산/건설 queue
- 시작 위치

### 유닛

각 유닛에는 pointer 대신 안정된 runtime slot/id를 사용한다.

- id와 runtime slot
- type id와 owner id
- 위치, destination과 path target
- 체력과 최대 체력
- 현재/이전 command state
- target unit id
- 생산/건설/이동/작업 상태
- action capability와 production capability
- active, visible, under-construction 상태

자신의 유닛은 모두 제공하고 적 유닛은 봇이 제어하는 owner의 현재 시야에 보이는 정보만 제공한다. P2P flight recorder의 full-state 정보나 숨겨진 적 상태를 observation에 복사하지 않는다.

### 맵

- 정적 지형/이동 가능 여부
- 봇 제어 owner의 explored/visible mask
- 현재 보이는 자원 타일의 남은 채집량과 중립 object
- 좌표는 world pixel과 tile을 구분해 명시

베리는 별도 unit이 아니라 map cell의 `kMapCellHarvestAmountMask`에 잔량이
저장된다. `AiObservation`은 현재 visible인 cell에 대해서만 이 값을
`resource_amount`로 제공한다. 탐사했지만 지금 보이지 않는 cell은 실제 잔량을
새로 읽어 정보가 새지 않도록 `0`을 제공한다.

## 6. Semantic Action v1

첫 Action API는 아래 범위로 제한한다.

```text
NoOp
Move(unit_ids, point)
AttackMove(unit_ids, point)
AttackUnit(unit_ids, target_id)
Harvest(unit_ids, resource_or_point)
ProduceUnit(producer_id, unit_type)
Research(producer_id, research_order)
Build(builder_id, building_type, point)
SetRally(producer_ids, target_or_point)
CancelProduction(producer_id, latest)
```

Action Validator는 다음을 검사한다.

- 명령 대상이 봇 제어 owner 소유인지
- 유닛이 active이고 명령 가능한 상태인지
- target이 현재 관찰에서 허용되는지
- point `Harvest` 대상이 현재 보이며 이동 가능하고 잔여 자원이 있는지
- 생산 capability, 비용, 인구, queue 제한을 충족하는지
- 건설 위치가 맵 내부이고 원본 placement 검사를 통과하는지
- 한 decision frame의 action 수 제한을 넘지 않는지

통과한 행동만 기존 `GameplayPublishedAction`과 ordered gameplay packet으로 변환한다. `AiObservation`이나 policy가 유닛/자원 runtime을 직접 수정할 수 있는 경로는 만들지 않는다.

`gameplay_1.ply`와 reconstructed handler/catalog를 대조해 subtype `0x01`/`0x0c`, build command `0x06`, rally command `0x1f`, latest cancel의 packet 의미를 확정했다. 특정 queue index 취소와 아직 해석하지 않은 명령은 공개 Action API에 넣지 않는다.

## 7. 첫 ScriptedBot의 행동

첫 봇은 강한 전략보다 인터페이스 검증을 목적으로 한다.

1. 시작 유닛과 자원을 확인한다.
2. 일꾼을 가장 가까운 유효 자원에 배치한다.
3. 정해진 티라노 빌드 순서의 첫 생산/건설 조건을 기다린다.
4. 생산 건물 또는 생산 가능한 유닛을 만든다.
5. 전투 유닛을 일정 수까지 모은다.
6. 탐색할 시작 위치 후보로 attack-move한다.
7. 적이 관찰되면 보이는 적 유닛/건물을 공격한다.
8. 승패 또는 timeout까지 반복한다.

첫 티라노 빌드 순서는 `gameplay_1.ply`의 생산·건설·연구 시점을 정렬해
추출했다. 프레임 시간을 그대로 재생하지 않고, 앞 단계의 구조물 완성이나 유닛
수량을 만족하면 다음 단계로 넘어가는 결정론적 목표 목록으로 구현한다. 따라서
자원 수급이나 건설 시간이 달라도 같은 순서를 유지하며, replay의 모든 세부
조작을 그대로 복제하지는 않는다.

## 8. 구현 순서

1. [완료] `AiObservation` 자료형과 순수 snapshot builder 작성
2. [완료] 같은 runtime 상태에서 observation hash가 같은지 회귀 테스트 추가
3. [완료] 기본 이동·공격·채집 action validator와 packet adapter 작성
4. [완료] 티라노 생산·연구·건설·랠리·latest 취소 의미 확정 및 packet planner 작성
5. [완료] live production/placement validator를 planner callback에 연결
6. [완료] 첫 `ScriptedBot` controller 작성 및 기존 packet 발행 경로 연결
7. [완료] visible 자원 타일 관찰, point `Harvest` 검증과 기본 채집 정책 연결
8. [완료] `gameplay_1.ply` 기반 티라노 건설·생산·연구 순서와 실패 backoff 연결
9. Python 맵에서 한 경기 자동 완주
10. 봇의 processed packet이 들어간 `.ply` 저장 및 재생
11. 같은 seed/action sequence의 최종 상태와 결과 재현 확인

## 9. MVP 완료 조건

- 봇이 `Computer(AI)` 티라노 슬롯을 사람 입력 없이 제어한다.
- 모든 상태 변경은 기존 gameplay packet 및 시뮬레이션 경로를 사용한다.
- 불가능한 action은 상태를 바꾸지 않고 사유 코드와 함께 거절된다.
- 같은 초기 상태와 action sequence가 같은 observation hash를 만든다.
- Python 맵에서 자원, 생산, 이동, 공격 단계를 모두 한 번 이상 수행한다.
- 경기 종료까지 crash, hang 또는 P2P flight mismatch가 없다.
- 저장된 봇 `.ply`를 policy 없이 재생할 수 있다.

이 조건을 통과한 뒤에만 여러 사람 리플레이 수집, 모방학습, 강화학습을 시작한다.

## 10. 현재 구현 상태 (2026-08-25)

첫 인터페이스 기반 구현을 완료했다.

| 항목 | 상태 | 구현 파일 |
|---|---|---|
| `AiObservation` schema v1 | 완료 | `include/ranker_ai_observation.h` |
| 결정론적 snapshot builder와 hash | 완료 | `src/ranker_ai_observation.cpp` |
| `NoOp`, `Move`, `AttackMove`, `AttackUnit`, `Harvest` | 완료 | `include/ranker_ai_actions.h`, `src/ranker_ai_actions.cpp` |
| `ProduceUnit`, `Research`, `Build`, `SetRally` | live 검증과 packet 발행 연결 완료 | `include/ranker_ai_live_validator.h`, `src/ranker_ai_live_validator.cpp` |
| `CancelProduction(latest)` | 완료 | `gameplay_1.ply`의 latest 형식만 공개 |
| action 검증과 subtype `0x02` packet 계획 | 완료 | `src/ranker_ai_actions.cpp` |
| 시야/결정론/action packet 회귀 테스트 | 완료 | `tests/ai_play_interface_regression.cpp` |
| 실제 게임 loop의 AI controller | 첫 `Computer(AI)` 슬롯 버전 완료 | `include/ranker_ai_scripted_bot.h`, `src/ranker_ai_scripted_bot.cpp`, `src/ranker_winmain.cpp` |
| visible 자원 탐지와 `Harvest` 경제 루프 | 첫 버전 완료 | `src/ranker_ai_observation.cpp`, `src/ranker_ai_scripted_bot.cpp` |
| replay 기반 티라노 빌드 순서 | 첫 버전 완료 | `src/ranker_ai_scripted_bot.cpp`, `tests/ai_play_interface_regression.cpp` |

관측 경계는 다음과 같이 강제한다.

- 봇 제어 owner의 유닛은 항상 관측한다.
- 다른 owner의 유닛은 caller가 현재 시야에 있다고 명시한 경우에만 포함한다.
- 시야 callback이 없으면 다른 owner의 유닛을 모두 숨기는 fail-closed 방식이다.
- 보이는 적도 위치, 체력, type 같은 공개 정보만 제공한다.
- 적의 destination, path target, command state, queue와 현재 target은 0으로 지운다.
- 자신의 유닛이 시야 밖 적을 target으로 잡고 있어도 해당 target id는 노출하지 않는다.
- 맵 cell의 동적 점유 bit는 숨기고 정적 terrain과 caller가 투영한 explored/visible mask만 제공한다.
- 자원량은 현재 visible인 map cell에만 제공하고 숨겨진 cell은 `0`으로 가린다.

Action Planner는 runtime을 직접 변경하지 않고 기존 `GameplayPublishedAction` 목록만 만든다. 공격 명령은 사람의 explicit-attack 경로와 맞춰 action `0x05`가 가능한 유닛은 `0x05`, 공격 능력은 없지만 이동 가능한 혼합 선택 유닛은 `0x04`를 사용한다. queued action은 replay에서 확인한 것과 같이 command의 `0x80000000` bit를 사용한다.

티라노 생산 의미와 근거는 [`gameplay_1.ply` 티라노 AI Action 매핑](AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md)에 정리했다. live adapter는 기존 `CheckUnitProductionRequirements`, `CheckProductionOrderAvailability`, `CheckPreviewProductionPlacementFootprintGateCells`를 호출한다. 통과한 action만 기존 ordered gameplay packet 경로로 발행한다.

### 실행 및 현재 정책

별도의 실행 옵션은 필요하지 않다.

1. `ranker_rebuild.exe`를 평소처럼 실행한다.
2. 게임 방의 빈 슬롯 선택 목록에서 `Computer(AI)`를 고른다.
3. 첫 MVP는 티라노 전용이므로 해당 슬롯의 진영이 자동으로 `Tyrano`로 고정된다.
4. 게임을 시작하면 선택한 Computer owner를 새 `ScriptedBot`이 제어한다.

- 기존 `Computer` 항목은 원래 게임의 Owner AI를 그대로 사용한다.
- `Computer(AI)`는 한 명의 사람과 Computer가 플레이하는 로컬 세션에서 작동한다.
- replay 재생에서는 새 controller를 실행하지 않는다.
- 여러 사람이 접속한 P2P 세션에서는 아직 새 controller를 사용하지 않고 기존 Computer AI로 처리한다.
- 개발 호환성을 위한 `--ai-play` 옵션은 남아 있지만 일반 사용에는 필요하지 않다.
- 초기 Nest가 있으면 rally point를 정한다.
- 현재 시야의 베리 타일 중 가장 가까운 곳에 Dinos를 최대 4기까지 순차 배치한다.
- 채집·운반 중인 Dinos는 새 공격 명령 대상에서 제외하고 자원 복귀 loop를 유지한다.
- 일꾼 비용 100을 확보하면 생산 중인 수까지 계산해 Dinos를 6기까지 생산한다.
- Dinos 6기 이후에는 `gameplay_1.ply`에서 추출한 Nest·EggNest·LandNest·두 번째
  TyranoNest 건설, Masos·Dilophos 생산, 채집·공격·이동 연구 순서를 진행한다.
- 건설은 완성된 구조물 수, 유닛 생산은 현재 유닛과 queue 수를 함께 세어 중복
  명령을 막는다. 자원이나 선행 조건이 부족하면 그 목표를 유지한다.
- live validator가 생산 불가나 배치 실패를 반환하면 목표별 재시도 프레임까지
  기다리며, 그동안 보이는 적 공격이나 맵 탐색은 계속한다.
- 보이는 적이 있으면 공격하고, 없으면 결정론적인 맵 지점으로 attack-move한다.
- Nest가 없고 Dinos가 남아 있으면 주변 tile을 순서대로 검사해 Nest 건설을 시도한다.
- 기존 Owner AI는 `Computer(AI)` owner에 대해서만 중지되므로 두 AI가 같은 유닛에 중복 명령하지 않는다.

visible 자원 탐색, `Harvest`, EggNest 이후의 첫 replay 기반 빌드 순서까지
구현했다. 현재 순서는 `gameplay_1.ply`의 핵심 구조물·연구와 초기 전투 병력
milestone을 재현하는 규칙 기반 버전이며, 사람의 40기 Masos 생산이나 모든 전투
클릭을 그대로 복제하는 것은 아니다. 아직 경기 timeout/자동 재시작과 실제 GUI
한 경기 완주 검증은 하지 않았다. 따라서 다음 단계는 Python 맵 한 경기 자동
완주와 봇 명령이 저장된 replay 재생 검증이다.

집중 회귀 테스트 실행 방법은 다음과 같다.

```powershell
cmake -S ranker_reconstructed_code -B build_ai_play_interface -G Ninja `
  -DRANKER_BUILD_AI_PLAY_INTERFACE_REGRESSION_TEST=ON `
  -DRANKER_BUILD_MODE1_PARTIAL_ROUND_RECEIVE_TEST=ON
cmake --build build_ai_play_interface --target `
  ai_play_interface_regression mode1_partial_round_receive_regression
ctest --test-dir build_ai_play_interface --output-on-failure `
  -R '^(ai_play_interface_regression|mode1_partial_round_receive_regression)$'
```
