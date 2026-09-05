# AI Play entity-command 강화학습 설계

> 기준 코드: 2026-09-01, raw observation schema v4, RL feature v10
> (`802` global features / `80` macro actions / `64` global cells).
>
> 목표: 현재 전투 objective와 army/raid A/B/C 실행기가 대신 고르는
> 유닛·명령·표적을 정책에 돌려주되, 엔진 검증과 ordered gameplay packet
> 경로는 그대로 보존한다.

## 1. 결론

전투 정책은 `모든 유닛 × 모든 명령 × 모든 대상`을 하나의 flat action으로
펼치지 않는다. 한 decision에서 각 전투 유닛이 같은 actor를 공유하며 다음을
조건부로 선택한다.

```text
각 controlled combat entity i
  command_i
    0 KEEP_CURRENT_ORDER
    1 MOVE ------------------- point token 96개 중 하나
    2 ATTACK_MOVE ------------ point token 96개 중 하나
    3 PATROL ----------------- point token 96개 중 하나
    4 ATTACK_UNIT ------------ visible target entity pointer
    5 HOLD_POSITION
    6 STOP
```

외부 action index는 위와 같이 7개로 고정하되, network 안에서는 KEEP을 일반
7-way class 하나로 다루지 않는다. 먼저 명령을 끊을지 결정하는 gate를 두고
ISSUE일 때만 6개 non-KEEP 명령과 조건부 대상을 고른다.

```text
P(macro | global state)                                         (macro due일 때만)
× Π_i P(issue_i | global, own_i, all entities)
      × P(non_keep_command_i | issue_i, global, own_i, all entities)
      × P(point_i | command_i, own_i, global)                    (point 명령만)
      × P(target_i | command_i, own_i, visible target entities)  (공격만)
```

`KEEP_CURRENT_ORDER`가 명시적인 첫 번째 command다. 따라서 정책이 아무것도
바꾸지 않은 유닛에는 패킷이 없고, 기존 엔진 명령과 정책 order latch가 계속
유지된다.

한 번에 유닛 하나만 고르는 pointer 방식은 초기 구현이 조금 작지만 채택하지
않는다. entity cadence가 8 frame이고 병력이 50기라면 전군에 한 번씩 명령을
주기만 해도 400 frame이 걸린다. 반대로 유닛별 shared actor는 action을
Cartesian product로 만들지 않으면서도 전군을 같은 frame에 제어한다. 학습은
아래 10절처럼 먼저 timestep을 균등 가중하고 그 안에서 유닛별 PPO ratio를
평균하는 parameter-sharing MAPPO 근사로 처리하여 거대한 joint probability
곱을 직접 최적화하지 않는다.

기존 80-action 정책은 즉시 삭제하지 않는다. entity mode에서 생산·건설·연구만
느린 macro head로 남기고 전투 objective 계열은 mask한다. 전투 유닛을 실제
명령으로 바꾸는 주체는 entity policy 한 곳뿐이다.

## 2. 현재 코드에서 확인된 경계

현행 경로는 다음과 같다.

```text
AiObservation
  -> EncodeAiObservationForRl                    802개 집계 feature
  -> AiIpcRequestAction                          action 80 + cell 64
  -> DecideTyranoScriptedBotForHighLevelAction  group objective 설정
  -> AiMicroExecutorStep                         유닛/명령/표적 재결정
  -> AiSemanticAction
  -> PlanAiSemanticActionV1
  -> GameplayPublishedAction
  -> ordered Mode1 packet
  -> 엔진 pending command / command-state 실행
```

교체할 부분은 앞쪽의 `집계 feature -> group objective -> strategic micro`다.
다음 뒷부분은 이미 사람 명령과 같은 wire 경로이므로 그대로 재사용한다.

- `AiSemanticAction`
- `PlanAiSemanticActionV1`
- `GameplayPublishedAction`
- `PublishLocallySimulatedMode1GameplayPacket` / `PublishLocalMode1GameplayPacket`
- ordered packet 처리와 실제 unit command state

현재 planner가 지원하는 직접 명령은 다음과 같다.

| 정책 명령 | semantic kind | 실제 wire | 현행 hard gate |
|---|---|---|---|
| `KEEP_CURRENT_ORDER` | 없음 | 패킷 없음 | entity가 존재함 |
| `MOVE(point)` | `move` | subtype `0x02`, cmd `0x04` | `type_flags` bit 4, map bounds |
| `ATTACK_MOVE(point)` | `attack_move` | subtype `0x02`, cmd `0x05`, target 0 | `type_flags` bit 5, map bounds |
| `ATTACK_UNIT(target)` | `attack_unit` | subtype `0x02`, cmd `0x05`, target id | active, visible, non-friendly |
| `PATROL(point)` | `patrol` | subtype `0x02`, cmd `0x09` | `type_flags` bit 9, map bounds |
| `HOLD_POSITION` | `hold_position` | subtype `0x0a`, cmd `0x21` | owned, active |
| `STOP` | `stop` | subtype `0x02`, cmd `0x00` | owned, active |

두 planner 동작은 direct policy 앞에서 더 엄격히 막아야 한다.

1. 공격 capability가 없는 유닛의 `attack_move`/`attack_unit`을 현재 planner는
   오류로 만들지 않고 `move`로 낮춘다. 학습에서는 선택한 행동과 실행된 행동이
   달라지므로 entity prevalidator는 이 선택을 불법으로 처리한다.
2. planner의 표적 검사는 entity 학습에 필요한 실제 공격 legality 전부를
   표현하지 않는다. class bit뿐 아니라 target runtime flags와
   `render_class == 2`의 terrain gate까지 포함한 authoritative pair predicate를
   C++에 한 번 분리하고, mask 생성과 live receiver 검증이 같은 함수를 쓴다.

`kAiMaximumUnitsPerAction == 14`는 semantic planner 한 호출의 selection
상한이다. 실제 wire는 유닛별 패킷이다. 동일한 semantic kind/argument를 최대
14기씩 planner에 넘겨 결과를 모두 만든 뒤, 최종 `GameplayPublishedAction`을
source runtime id 오름차순으로 flatten-sort하여 발행한다. planner 호출 grouping
순서가 packet 순서를 결정하게 두지 않는다. 14-unit batch plan이 실패하면 아직
아무 packet도 발행하지 않은 상태에서 source별로 다시 plan하여 실패 row만
PLANNER_FAILED로 표시한다. 성공한 per-unit plan을 다시 global source-id sort한
뒤 publish한다.

14-unit chunk는 planner API 한도일 뿐 transport packet budget을 줄이지 않는다.
Mode1 reliable ring은 **channel마다** `0x800` slot이므로 한 tick에 한 owner의 2048
entity와 macro/worker packet을 그대로 넣으면 그 channel의 unread slot을 덮을 수
있다. entity mode는 한 owner씩
즉시 publish하는 현재 `run_default_ai_play_bot -> run_default_ai_play_owner` 구조를
바꾼다. 같은 simulation frame에 controlled인 **모든 owner**의 ACT_REPLY와
macro/worker/entity plan을 먼저 side-effect 없이 수집하고, 전체 합을 publish 전에
정확히 한 번 preflight한다.

```text
for every reliable channel c:
    checked_unread[c] + planned_packet_count[c]
        < kMode1ReliableWindowSlots   // 0x800, strict less-than
```

channel은 `GameplayPublishedAction.packed_opcode & 0xff`에서 얻는다. unread count는
그 channel의 producer/consumer sequence를 checked subtraction으로 구한다. v1은
producer<consumer 또는 `u32` wrap을 transport-fatal로 중단하며, 배정될 각 ring
slot이 free인지도 확인한다. invalid channel, window 밖 거리,
occupied target slot은 오류다. 모든 channel 조건이 성공해야 하며 channel별로는
안전한 두 owner의 합이 2048을 넘는다는 이유만으로 global false reject하지 않는다.

check와 packet별 `GetExpectedSequence -> Accept` 사이에 network thread가 끼어드는
TOCTOU도 허용하지 않는다. AI 전용 batch accept API가 reliable mutex 아래에서 모든
channel capacity/slot을 먼저 검증하고 canonical frame order대로 channel별 sequence를
배정한다. input ordinal별 `(channel,sequence)`가 정해지면 allocation/reliable reentry가
금지된 no-fail controller success hook으로 **모든 row**의 staged last-attempt/outcome,
successful ISSUE의 AWAITING order, accepted macro history와 transaction-local RNG next
state를 먼저 설치하고, 전 packet bytes/flags와 producer cursor를 all-or-none
commit한다. 이 전 과정 동안 mutex를
유지하므로 packet consumer는 새 slot을 먼저 볼 수 없다. 그 뒤 lock을 풀고 callback과
transport send를 commit된 순서대로 실행한다.

batch item은 기존 owner 경로의 publish mode도 보존한다. command-line/local owner의
`LOCAL_BROADCAST` item은 성공한 sequence에 대해 기존
`local_broadcast_end=sequence+1`, subtype별 `should_flush_published_packet_range`,
`BroadcastMode1PacketRange(start,end-1)` 순서를 그대로 post-commit 재생한다. 한
connection에서 LOCAL_BROADCAST owner는 최대 하나다. `ai_play_owner_slots`의
`LOCALLY_SIMULATED` item은 기존처럼 range cursor/transport broadcast를 전혀
건드리지 않는다. post-commit transport send 실패는 이미 accepted된 packet을
rollback하지 않고 infrastructure failure로 기록한다.

row별 mask/live/planner/packet-encode 실패는 batch 입력을 만들기 전에 확정하여 그
row만 제외할 수 있다. 그러나 batch에 들어간 packet은 모두 PUBLISHED이거나 batch
전체가 TRANSACTION_ABORTED인 둘 중 하나다. post-commit callback/engine ACK 실패를
ENCODE_FAILED로 바꾸거나 rollback하지 않는다. KEEP/DEDUP/reject/ISSUE의 normal
last-attempt와 state change도 plan 중에는 모두 transaction-local로 stage한다. packet이
0개인 성공 transaction은 같은 success hook 의미로 staged metadata만 commit한다.

batch 검증이 실패하면 normal success hook은 호출하지 않는다. active order, normal
row reject, macro/history, RNG next state는 전혀 commit하지 않고, no-fail abort hook이
모든 entity row와 due macro의 last-attempt/outcome만
`TRANSACTION_ABORTED/TRANSPORT_CAPACITY`로 통일한다. 그 frame의 AI
packet은 하나도 발행하지 않고 transaction
outcome을 전송한 뒤 persistent controller failure와 collector cutoff로 전이한다.
일부 유닛을 executor가 임의로 우선 선택하지
않는다. owner A를 먼저 publish한 뒤 owner B 용량을 검사하는 부분 commit도 금지한다.
이 batch preflight는 기존 ring의 unread packet을 건드리거나 버리지 않는다.
여기서 OUTCOME/abort hook은 ACT_REQ가 있는 decision frame 기준이며, 중간
worker-only frame은 §4.3의 local abort 규칙을 쓴다.
plan 단계는 resources/history/order latch뿐 아니라 RNG도 변경하지 않는다. target
translation이 AI 전용 RNG를 필요로 하면 frame transaction-local copy를 쓰고
preflight 성공 뒤에만 그 next state를 commit하며, gameplay simulation RNG는 AI
planning에 소비하지 않는다.

## 3. 정책이 배우는 것과 코드에 남기는 것

### 3.1 정책이 직접 배우는 판단

- 어느 전투 유닛이 현재 명령을 유지할지
- 어느 유닛을 언제 이동·공격 이동·순찰·정지·홀드할지
- 장거리 진군 위치와 근거리 이동 방향/거리
- 보이는 적 중 누구를 어느 유닛이 집중 공격할지
- 공격을 계속할지, 직접 후퇴시킬지, 어디에서 방어할지
- 분산, 포위, 집중, 전열 교대, 카이팅과 재진입
- 본진 위협에 몇 기를 돌릴지
- 정찰을 어느 유닛으로 어디까지 할지
- 중립 몬스터와 싸울지 무시할지
- macro gate가 열렸을 때 무엇을 생산·건설·연구할지

다음 조건은 mask에 넣지 않는다. 모두 정책이 학습할 내용이다.

- 기지 근처에 적이 있는가
- HP가 낮으니 싸우면 안 되는가
- 목표가 너무 먼가
- 병력이 충분한가
- 현재 전력비가 유리한가
- 후퇴·집중 공격·산개가 효율적인가
- 한 유닛을 몇 기가 때려야 하는가

### 3.2 실행 계층에만 남기는 일

- unit/entity index를 그 snapshot의 `(runtime id, activation generation)`으로 변환
- source의 snapshot `control_epoch`와 현재 소유권·direct-control eligibility,
  생존·완성 여부·capability·fog visibility 재검증
- target runtime flags·class·class-2 terrain gate를 포함한 공격 가능성 재검증
- map bounds와 movement-class별 정적 이동 가능성 검증
- point token을 결정적인 world point로 변환
- 같은 semantic order 중복 억제와 명령 지속
- 엔진 명령 유실/interrupted와 stalled의 결정적 feedback
- source id 정렬, 같은 명령 batching, 14기 chunking
- `AiSemanticAction -> planner -> ordered packet` 발행
- 거부·완료·stalled 상태를 다음 관측으로 돌려주기

실행기는 적 중심, 부대 중심, 가까운 base, 저체력 여부를 보고 정책의 target을
바꾸지 않는다. 그런 변경은 안전 검증이 아니라 전략 대행이다.

### 3.3 entity mode에서 끄는 기존 전투 규칙

direct-controlled fighter에는 다음 `AiMicroExecutorStep` 판단을 적용하지 않는다.

- army/raid A/B/C group assignment와 objective
- units-first/buildings-first/neutral-only target picker
- base-defense reflex와 defender 선발
- low-health pullback
- cohesion hold와 wave staging
- 자동 retreat/defend 전이
- scout/roam/explore target 선택
- meat collector 선택
- stuck destination jitter

16-frame `AiMicroHoldUnits`만으로는 충분하지 않다. hold가 끝나면 기존 micro가
다시 명령을 덮기 때문이다. entity별 controller ownership latch를 두고
direct-controlled fighter를 old objective executor에서 완전히 제외한다.

초기 범위에서는 worker의 자동 채집과 근거리 도주만 scripted economy로
남긴다. 이 판단은 v1 전투 actor의 학습 대상이 아니다. worker·transport·고기
pickup·ability·merge·morph·stance를 직접 학습시키는 확장은 14절에 분리한다.

## 4. 두 cadence를 하나의 step stream으로 묶기

### 4.1 entity cadence

- 첫 frame: 즉시 decision
- 이후: 고정 8 frame마다 entity decision
- 적 발견 같은 이벤트가 있어도 cadence를 바꾸지 않음
- 매 tick 모든 alive combat entity에 command head를 평가

고정 cadence는 rollout의 시간 의미와 패킷 예산을 단순하게 한다. 엔진 명령은
그 사이 매 simulation frame 계속 실행된다.

### 4.2 macro cadence

기존 `AiDecisionGate`의 min 8 / max 64 frame과 이벤트 trigger는 유지한다.
다만 별도 socket request를 만들지 않고 매 entity tick의 통합 `act2` 요청에
다음을 싣는다.

```text
macro_due == false -> macro_mask는 no_op 하나만 1
macro_due == true  -> 현재 macro legal mask
```

entity mode v1에서 macro로 남기는 기존 action index는 정확히 다음이다.

```text
0..7    no_op + core 생산/건설/확장
14..27  추가 유닛 생산 + 추가 건물 건설
39..51  개별 연구
```

다음 index는 entity mode에서 0으로 mask한다.

```text
8..13   scout/attack/defend/retreat/hunt objective
28..38  merge/morph/stance/hold/patrol/drop composite
52..79  search/scout/raid A/B/C objective
```

`scout_berry`가 없어도 entity policy가 expansion feature의 방향을 보고 한 전투
유닛을 직접 이동시켜 site를 밝힐 수 있다. builder와 정확한 건설 지점 선택은
v1 macro translator에 남는다.

`AiAutopilotPlan`의 규칙 중 entity fighter를 다시 소유할 수 있는 것은 scout
guard(explore_frontier) 하나뿐이므로, 학습 경기에서는 plan 전체가 아니라
**scout guard만** `AiAutopilotConfig::scout_guard_enabled=false`로 끈다
(2026-09-02 개정). worker floor·pop-nest guard·idle-producer guard는 생산/건설
액션만 내며 유닛 소유권과 충돌하지 않고, v10 macro tower가 이 규칙들이 켜진
분포에서 학습되어 스스로 경제 액션을 뽑지 않기 때문에 전체 OFF는 경제를
시작 일꾼 수준(pop 8)에 고정시켜 모든 경기를 구조적 패배로 만든다(v4~v6
330+게임 실측). autopilot과 macro 번역이 계획한 build의 builder worker는
`AiMicroHoldUnits`로 잠가 같은 frame의 worker-only executor harvest 재할당이
build 명령을 덮어쓰지 않게 한다(발행 순서가 macro stage → worker stage이므로
잠그지 않으면 뒤에 실린 harvest가 이긴다). v1의 scripted worker harvest는
autopilot이 아니라 fighter를 절대 선택하지 않는 별도 worker-only 경로로
유지하고, 현재처럼 매 simulation frame 실행한다.

macro event가 두 entity tick 사이에 발생하면 `macro_due`를 다음 act2 tick까지
latch한다. `macro_due == false`인 요청은 macro action/history/feature-delta
history를 갱신하지도 rollout row를 만들지도 않는다. 이는 현재 Python
`HistoryState`의 last-action/feature-delta 의미를 보존하기 위한 계약이다.

### 4.3 한 frame의 고정 처리 순서

```text
1. controlled owner id 오름차순으로 observation + fog memory 갱신
2. 각 owner의 latched macro gate를 포함한 ACT_REQ/REPLY를 모두 수집·검증
3. 모든 owner의 macro action 번역·plan + economy autopilot plan (builder는
   AiMicroHoldUnits로 잠금)
4. 모든 owner의 scripted worker economy order 계산·plan        (fighter 제외,
   held builder 제외)
5. 모든 owner의 entity fighter order dedupe·plan                (source id 정렬)
6. unread ring + 모든 owner/세 stage packet을 channel별로 batch preflight 1회
7. 성공 시 stage-major `(macro, worker, entity)`, 각 stage 안 owner id 오름차순으로
   publish/commit; worker/entity는 source id 오름차순
8. owner id 오름차순으로 OUTCOME 전송
9. drop/old fighter objective runner는 실행하지 않음
```

macro 내부의 multi-packet plan은 planner가 낸 ordinal을 보존한다. server는 한 frame의
다른 owner ACT_REQ가 먼저 올 수 있으므로 ACT_REPLY 직후 같은 owner OUTCOME이 반드시
이어진다고 가정하지 않고 `(owner,episode,sequence)`로 join한다. 어느 owner의
framing/timeout도 publish 전에 발생하면 모든 owner의 open action을 버리고 packet은
0개다. 같은 입력과 같은 응답이면 multi-owner packet 순서까지 동일해야 한다.

위 표는 entity decision frame의 superset이다. 그 사이 7 frame에는 ACT_REQ/macro/entity
stage를 생략하고 모든 owner의 worker-only plan만 같은 per-channel atomic batch로
처리한다. 이 frame에는 request sequence가 없으므로 OUTCOME을 꾸며 보내지 않는다.
worker-only batch capacity가 실패하면 packet 0, local
`WORKER_TRANSACTION_ABORTED/TRANSPORT_CAPACITY`, persistent controller failure와
collector cutoff로 기록하고 socket을 닫는다. 마지막 entity action은 unsealed tail로
버리며 이전 sealed prefix만 마지막 full ACT_REQ observation으로 bootstrap한다.

## 5. entity 집합과 안정적인 index

### 5.1 own combat entities

`AiObservation.units`에서 다음 조건을 모두 만족한 유닛만 넣는다.

- `controlled && alive && !under_construction`
- mobile type (`type_id < 0x60`)
- `AiMicroRoleOf(unit)`이 `melee` 또는 `ranged`

따라서 harvest capability가 우선인 worker, 비전투 transport, building은 v1
entity actor에서 빠진다. 정렬은 `EntityKey(runtime_id,generation)` 사전순이다.

### 5.2 attack target entities

다음을 target list에 넣고 역시 `EntityKey` 사전순으로 정렬한다.

- 현재 visible, alive인 active hostile owner의 mobile/building
- 현재 visible, alive인 neutral monster
- friendly/ally는 제외

target list 자체는 공격자와 무관하다. 실제 `own i -> target j` legality는
공격자의 class mask로 조건부 계산한다.

### 5.3 ID 규칙

unit id는 정책이 생성할 수 있는 일반 정수가 아니라 opaque `u32` runtime
reference다. 정상 fixed-pool 유닛은 대개 `slot * 0x1d0`이지만, free list가
고갈된 create path는 `0x80000000` 이후 id의 detached record도 만들 수 있으므로
protocol이 offset 공식을 전제로 삼지 않는다.

더 중요한 것은 fixed-pool slot이 재활성화돼도 id가 바뀌지 않는다는 점이다.
따라서 entity module은 canonical activation path에서 slot별 `u32 generation`을
증가시키고 source와 target을 모두 다음 key로 식별한다.

```text
EntityKey = (runtime_unit_id, activation_generation)
```

generation은 engine packet에 넣는 새 unit id가 아니라 AI contract 내부의 stale
reference 방지 token이다. decision tick 관찰만으로 inactive→active를 추측하지
않는다. match registry reset 뒤 initial placed unit은 generation 1로 등록하고,
그 뒤 generation은 "placement가 성공하여 inactive→active가 commit된 순간"마다
증가한다. 실패한 placement나 같은 activation의 중첩 helper call은 증가시키지
않고 `u32` wrap은 contract fatal이다.

현재 코드에는 단일 activation 함수가 없으므로 다음 경계를 모두 작은
idempotent registry API로 감싼다.

- startup/scenario restore placement, fixed-slot remap과
  `default_gameplay_startup_unit_placed`
- production의 manual initialize + active-list insert
- script spawn의 `activate_default_gameplay_script_unit`
- `HandleFreeUnitActivation`
- corpse/revival의 `HandleLifecycleUnitActiveListMove`
- 각 경로의 free/deactivation/remove

API는 pointer/runtime slot의 inactive/active transition을 기억해 한 activation에
한 번만 commit하고 deactivation은 즉시 mark한다. order latch, trace, shadow
label도 이 key를 사용한다. fixed slots는 2048-entry table을 쓸 수 있지만
`runtime_slot_index >= 0x80000000` detached record는 pointer-keyed sparse registry를
별도로 사용하고 그 map의 iteration order는 serialization에 사용하지 않는다.

동시에 active인 두 record가 같은 runtime id를 가지면 ordered command target을
유일하게 표현할 수 없으므로 조용히 tie-break하지 않고 contract-fatal로 진단한다.

`generation`은 물체 identity이고 소유권 identity는 아니다. 실제 코드에서는
`default_player_slot_transfer_owner_state`, script의 `set_script_object_owner`, 일부
unit action이 active record를 deactivate하지 않고 `owner_id`만 바꾼다. 따라서
entity registry는 activation generation과 별도로 다음 direct-control signature를
추적한다.

```text
ControlSignature = (owner_id, direct_eligible, type_id, role,
                    capability_bits_4_5_9, movement_class)
```

signature가 바뀌면 해당 active unit의 `u32 control_epoch`을 증가시키고, 이전
controller owner의 active-order/last-attempt/controller-ownership record를 observation
생성 전에 즉시 제거한다. 새 owner에게 보이는 첫 row는 semantic order `NONE` 또는
raw engine order가 남아 있으면 `EXTERNAL_UNKNOWN`, last attempt는 `NONE`이다.
generation은 증가시키지 않는다. target으로 참조된 같은 EntityKey는 유지하되 새
소유권으로 hostility를 live revalidate하여 friendly가 됐으면 TARGET_LOST다.
모든 direct assignment 경로를 hook하고, every-frame registry audit가 누락된 경로를
같은 규칙으로 잡는다. `control_epoch` wrap은 contract fatal이다.
새 activation은 epoch 1로 시작하며 generation이 증가할 때 control epoch도 1로
reset한다.

정책은 raw key를 action으로 반환하지 않고 request row index를 반환한다. C++은
응답의 `frame/owner/episode/sequence/schema`를 먼저 확인한 뒤 그 snapshot의
index를 key로 역변환하고 snapshot의 `control_epoch`까지 live 값과 다시 검증한다.
따라서 A→B→A 소유권 변경이 한 request 사이에 끝나도 A의 오래된 reply가 같은
runtime id/generation에 적용되지 않는다. control epoch은 wire/trace의 opaque stale
token일 뿐 network feature나 engine packet unit id로 쓰지 않는다.

wire row 수는 variable-length다. Python rollout batch에서만 그 minibatch의
최대 길이로 동적 padding한다. nominal fixed pool은 `0x800`(2048) slot이므로 v1
wire safety limit도 own/target 각각 2048로 둔다. 이는 모든 runtime id가 pool
offset이라는 주장이 아니다. detached spawn 등으로 한도를 넘으면 일부를
거리·위협도로 조용히 자르지 않고 해당 episode를 명시적인 unsupported-contract
오류로 중단한다. FFA에서 큰 `U×E` 연산이 병목이면 정확한 row와 mask를
유지한 chunked attention으로 해결한다.

## 6. 관측 계약

기존 802 global features는 macro tower와 centralized critic 입력으로 그대로
유지한다. per-unit 정보는 802 뒤에 flatten하지 않고 별도 ragged entity
contract로 보낸다.

다만 802 안의 `army_objective/attack_tactic/pulling_back/scout`와 raid A/B/C처럼
old executor가 만든 요약 slot은 entity mode에서 모두 canonical zero/none으로
쓴다. entity order를 다시 army/raid group label로 집계해 넣지 않는다. 그 label이
살아 있으면 정책이 direct unit state 대신 이전 전략 규칙을 우회적으로 따라가게
된다. 병력 수·위치·HP·fog/map/economy처럼 observation 자체에서 계산되는 global
feature와 macro history는 유지한다.

같은 visible state라도 macro action을 지금 낼 수 있는지에 따라 transition이
달라지므로 `macro_due`, gate elapsed, forced-deadline remaining을 별도 scalar로
actor/critic에 함께 준다. 이 세 값은 802 feature index를 바꾸지 않고 act2
contract field로 추가한다.

raw `AiObservedUnit`에는 이미 다음이 있다.

- id/type/owner, 위치, HP/secondary, type flags
- 이동 속도, sight, ground/air attack range, target class mask
- attack/defense, base recovery ticks
- direction/animation/level/experience
- controlled-only command state, destination/path target/current target
- command-entry lockout, command/attack recovery counter, effect timer
- cargo/item/equipment

unit row에는 `UnitMovementDefinition::movement_class`를 observation schema
v5로 append한다. 그러나 이것만으로 point mask를 계산할 수는 없다. 현재
`AiObservedMapTile.passable`은 class 0 투영만 담고 class 2/4가 쓰는
`alternate_flags` 규칙이 없기 때문이다. entity contract builder는 live
`UnitMovementMap`을 명시적으로 입력받아 visibility/dynamic occupancy를 제외한
engine static-entry predicate와 8-neighbor 규칙을 공유한다. Python이 observation
tile만으로 reachability를 재구성하지 않는다.

현재 semantic order와 그 경과 시간은 raw engine state만으로 안정적으로
복원하지 않는다. 새 `AiEntityActiveOrder`와 `AiEntityLastAttempt`가 분리 관리하고
encoder 입력으로 주입한다.

### 6.1 own entity categorical fields

| field | 값 |
|---|---|
| `type_id` | `0..0xa9`, embedding |
| `movement_class` | raw `u32`; 0..4 embedding, 그 밖은 UNK+OOB |
| `distance_check_mode` | controlled raw `u32`; 0/1 embedding, 그 밖은 UNK |
| `render_class` | public target class, embedding |
| `role` | melee/ranged |
| `command_base_state` | `(raw & kUnitCommandStateMask)`; `0..137` 외는 UNK |
| `command_state_high_flags` | `raw_command_state & ~kUnitCommandStateMask` |
| `unit_command_flags` | 별도 raw `AiObservedUnit.command_flags` |
| `movement_state` | raw movement state, embedding |
| `semantic_order` | 아래 8-way wire intent category |
| `order_status` | none/awaiting_apply/active/completed/target_lost/stalled/interrupted |
| `presence_bits` | destination/path/engine-target/semantic-point와 OOB flags |
| `last_attempt_command/result/reject` | active order와 분리한 직전 시도 feedback |
| `active_target_row` | 현재 target row gather index 또는 `-1`; embedding 금지 |
| `engine_order_match` | 0 N/A(no semantic record), 1 MATCH, 2 CLEARED, 3 DIFFERENT |

numeric unit id는 network feature로 넣지 않는다. episode 안의 identity와 응답
mapping에만 사용한다.

raw `AiSemanticActionKind`를 `u8`로 cast하지 않는다. wire category는 다음처럼
고정 변환한다.

```text
semantic_order: 0 NONE, 1 EXTERNAL_UNKNOWN, 2 MOVE, 3 ATTACK_MOVE,
                4 PATROL, 5 ATTACK_UNIT, 6 HOLD, 7 STOP
order_status:   0 NONE, 1 AWAITING_APPLY, 2 ACTIVE, 3 COMPLETED,
                4 TARGET_LOST, 5 STALLED, 6 INTERRUPTED
last_attempt_command: external command index 0..6, 255 NONE
last_attempt_result:  OUTCOME result 0..8, 255 NONE
```

내부 semantic enum과 external command 간 switch는 한 함수에 두고 unknown kind는
`EXTERNAL_UNKNOWN`으로만 encode한다.

role도 raw `AiMicroRole`을 cast하지 않는다. own row는 `0 MELEE, 1 RANGED`, target
row는 `0 MELEE, 1 RANGED, 2 NONCOMBAT`으로 고정하고 worker/transport/building/
other는 target에서 모두 NONCOMBAT로 합친다.

`render_class >= 32`, 알 수 없는 command/movement state는 embedding 범위를
벗어나지 않도록 각각 UNK category와 out-of-range bit로 encode한다. missing
좌표는 continuous 값 0과 `presence_bits`를 함께 사용한다.

presence는 heuristic validity가 아니라 wire availability다. controlled own row의
raw destination/path 좌표는 stale 값도 engine state의 일부이므로 bit0/bit1을 항상
1로 보내고 command/movement state가 의미를 구분하게 한다. bit2는 fog-visible
`target_id`가 실제 target row로 resolve될 때만 1, bit3은 active-order kind가
MOVE/ATTACK_MOVE/PATROL이고 semantic point record가 있을 때만 1이다. 나머지
bit4..7은 아래 OOB 정의를 따른다.

### 6.2 own entity continuous fields (`D_own = 33`)

| index | 값 |
|---:|---|
| 0..1 | `x/map_width_px`, `y/map_height_px` |
| 2 | `health/max_health` |
| 3 | `secondary/max_secondary` |
| 4 | held meat/recovery reserve인 `action_mode` 정규화 |
| 5 | `movement_step_limit / movement_period` 정규화 |
| 6 | sight / map diagonal |
| 7..8 | ground/air attack range / map diagonal |
| 9..10 | effective attack/defense 정규화 |
| 11 | base attack recovery ticks 정규화 |
| 12 | command-entry lockout 정규화 |
| 13 | 현재 command/attack recovery counter 정규화 |
| 14 | effect timer 정규화 |
| 15..16 | direction `sin/cos` |
| 17..18 | destination `dx/dy` / map diagonal |
| 19..20 | path target `dx/dy` / map diagonal |
| 21..22 | current engine target `dx/dy` / map diagonal, 없으면 0 |
| 23 | current engine target distance / map diagonal, 없으면 0 |
| 24..25 | issued semantic point `dx/dy` / map diagonal, 없으면 0 |
| 26 | issued semantic point distance / map diagonal, 없으면 0 |
| 27 | issued semantic order age / 256, clamp |
| 28 | last packet issue age / 64, clamp |
| 29 | interrupted idle candidate frames / 4, clamp |
| 30 | progress-required age since last 8px movement / 48, clamp |
| 31 | level 정규화 |
| 32 | experience 정규화 |

정규화 상수는 코드 한 곳에 두고 checkpoint metadata에 기록한다. sentinel은
별도 categorical/boolean field가 담당하며 좌표 `0`과 missing을 섞지 않는다.

### 6.3 target entity fields

target은 public 정보만 사용한다. 상대의 private command, destination, lockout,
equipment는 raw observation 단계에서 이미 redaction된다.

categorical:

- `type_id`, `owner_id`, `render_class`
- mobile/building/neutral
- melee/ranged/noncombat 역할

continuous (`D_target = 14`):

- x/y, HP ratio, secondary ratio
- 이동 속도, sight
- ground/air range, public attack/defense
- direction sin/cos
- own start로부터 dx/dy

actor는 own/target 좌표로 pair-relative dx/dy/distance를 내부에서 계산한다.
`U == 0`과 `E == 0`은 정상 request다. 빈 pool은 zero pooled context를 쓰고,
`E == 0`이면 ATTACK_UNIT을 mask한 뒤 pointer softmax 자체를 호출하지 않아
all-`-inf` NaN을 만들지 않는다.

## 7. point target 96개

전역 8x8만으로는 일반 128x128-tile map에서 한 cell이 약 512px라서 사거리
50..430px의 카이팅·산개를 배우기 어렵다. point vocabulary를 다음처럼
고정한다.

```text
token 0..63   : global 8x8 cell (row-major)
token 64..95  : local offset
                radius = {64, 128, 256, 512}px 4개
                direction = {E, SE, S, SW, W, NW, N, NE} 8개
                token = 64 + radius_index*8 + direction_index
```

global token은 cell의 기하학적 중심을 그대로 쓰지 않는다. 해당 유닛의 static
reachable component 안에서 cell 중심에 가장 가까운 진입 가능 tile을 고르고,
동률이면 tile index가 작은 점의 world center를 쓴다. cell 안에 그런 tile이
없으면 그 유닛의 point mask에서 0이다.

정확한 geometry는 `point_geometry_version == 1`로 고정한다.

- tile map 폭/높이가 8의 배수가 아니면 cell `c`의 half-open 범위는
  `[floor(c*N/8), floor((c+1)*N/8))`이다.
- 후보 tile center와 cell의 유리수 기하 중심 사이 squared Euclidean distance가
  최소인 tile을 고르고, 동률은 row-major `tile_y*Nx + tile_x`가 작을수록 먼저다.
- world point는 `(tile_x*32+16, tile_y*32+16)`이다.
- local 좌표에서 y+는 south다. cardinal component는 radius 그대로이고 diagonal
  component는 radius별 `{45,91,181,362}`px를 양 축에 사용한다. 즉 diagonal도
  대략 같은 Euclidean radius를 갖는다.
- point bit `t`는 `word=t>>5`, `bit=t&31`의 least-significant-bit-first다.

이 mask는 기존 `AiRlStepEncoding::target_mask`를 재사용하지 않는다. 기존 mask는
explored/remembered/start cell만 여는 전략용 mask다. 사람의 MOVE와 마찬가지로
direct entity는 안개 속 미탐색 지점으로 이동할 수 있으며, 공개 map terrain과
정적 reachability만 사용한다.

local token은 명령 시점 유닛 위치에 offset을 더한 절대점이다. map 밖이거나
static reachable component 밖이면 0이다. KEEP으로 유지한 local 명령은 처음
resolve한 절대점을 계속 쓴다. 정책이 다음 tick에 같은 local token을 다시
ISSUE하면 새 현재 위치 기준 절대점이므로 새로운 이동 명령이 될 수 있다.

point용 static entry는 live command check를 그대로 부르지 않고 다음과 같이
고정한다.

- legacy entry layer가 있으면 `legacy_movement_class_can_enter_cell`과 동일한
  class 0..4/`alternate_flags`/terrain/brush 규칙을 쓰되
  `allow_command_shortcut=false`다. 단, `visibility_flags & 0x20000000`은 runtime
  building footprint이므로 static copy에서 clear한 뒤 판정한다.
- legacy layer가 없으면 `IsPassableTerrainCell`만 사용하고
  static `kMapCellBlockedTerrain`은 거부하며, 동적
  `kMapCellReservedByUnit`만 무시한다.
- source tile이 static-invalid여도 active unit의 현재 tile 하나는 virtual flood
  seed로 허용하되, 그 다음부터는 static-entry를 통과한 tile로만 확장한다.
- neighbor 순서는 engine pathfinder와 같은 N,E,S,W,NW,NE,SE,SW 8개다. diagonal은
  destination tile만 검사하고 cardinal corner-clear를 추가하지 않는다.

반면 ATTACK_UNIT의 class-2 pair gate는 static helper가 아니라 위 8절에 적은 live
`CheckUnitCanEnterTerrainCell`을 target의 32px-aligned 좌표에 그대로 호출한다.
다음 전략·동적 조건은 point mask에 넣지 않는다.

- 현재 유닛/건물의 동적 점유
- 목적지까지의 거리
- 적 존재 여부
- 해당 이동이 전술적으로 안전한지
- HP나 전력비

동적 점유와 실제 path 실패는 엔진과 다음 `order_status`가 알려준다.

wire에서는 유닛별 96-bit point mask를 `[u32,u32,u32]`로 압축한다. token resolve와
inverse-quantization은 한 C++ helper 및 Python golden fixture로 고정한다.

## 8. 조건부 hard mask

command index는 고정한다.

```text
0 KEEP_CURRENT_ORDER
1 MOVE
2 ATTACK_MOVE
3 PATROL
4 ATTACK_UNIT
5 HOLD_POSITION
6 STOP
```

유닛별 command mask는 다음만 검사한다.

| command | legal 조건 |
|---|---|
| KEEP | alive direct-owned entity면 항상 1 |
| MOVE | `type_flags` bit 4 + point mask에 1이 하나 이상 |
| ATTACK_MOVE | bit 5 + point mask에 1이 하나 이상 |
| PATROL | bit 9 + point mask에 1이 하나 이상 |
| ATTACK_UNIT | bit 5 + pairwise legal target이 하나 이상 |
| HOLD | owned/alive |
| STOP | owned/alive |

`i -> j` attack pointer는 C++의 authoritative pair predicate가 다음을 모두
확인했을 때만 1이다.

```text
source/target EntityKey generation이 현재 snapshot과 같음
source가 active/owned/alive이고 attack capability bit 5가 있음
source.distance_check_mode != 1
target이 active/alive/visible/non-friendly이고 runtime flags에
    kUnitActionTargetTransient | kUnitActionTargetInactive |
    kUnitActionTargetClassBlocked가 없음
render_class < 32이면 attackable_class_mask bit가 켜져 있음
render_class >= 32이면 엔진의 permissive class 규칙을 그대로 적용
render_class == 2이면
    profile.render_class2_terrain_gate != 0 또는
    CheckUnitCanEnterTerrainCell(source, target.x&~31, target.y&~31)를 통과
```

사거리 안인지, 가까운지, 저체력인지, building인지 mobile인지는 legality가
아니다. 정책이 pointer score로 선택한다.

class mask만 Python에서 파생하면 `ValidateUnitActionTarget`의 source
`distance_check_mode == 1` early reject, class-2 terrain gate와 transient target
flags를 잃는다. 따라서 C++이 snapshot의 authoritative
`U×ceil(E/32)` pair bitset을
만들어 보낸다. Python은 이를 그대로 masking하고 PPO update에도 rollout 당시
bitset을 저장한다. update 때 live mask를 다시 계산하면 안 된다. C++ receiver는
선택된 pair에 같은 predicate를 live state로 한 번 더 적용한다.

## 9. 명령의 시간 의미와 direct order latch

새 상태는 old group objective와 분리한다. 기존 active intent와 이번 tick의 실패를
한 `status`에 덮어쓰면 "기존 공격 유지 + 새 MOVE 거부"를 표현할 수 없으므로
두 record를 나눈다.

```cpp
struct AiEntityKey {
    u32 runtime_id;
    u32 activation_generation;
};

struct AiEntityActiveOrder {
    AiEntityKey source;
    u32 controller_owner;
    u32 control_epoch;
    AiSemanticActionKind kind;
    AiEntityKey target;              // ATTACK_UNIT만, 없으면 invalid key
    i32 target_x;
    i32 target_y;
    u32 issued_frame;                // packet publish 성공 frame
    u32 applied_frame;               // matching engine pending-command ACK frame
    u32 ordered_packet_channel;
    u32 ordered_packet_sequence;
    u32 delivery_seen_frame;
    u32 last_issue_frame;
    u32 idle_candidate_frames;
    i32 last_progress_x;
    i32 last_progress_y;
    u32 last_progress_frame;
    AiEntityOrderStatus status;       // awaiting/active/completed/lost/stalled/interrupted
};

struct AiEntityLastAttempt {
    u32 controller_owner;
    u32 control_epoch;
    u32 request_sequence;
    AiEntityCommand requested_command; // external 0..6, semantic enum과 별도
    u32 attempt_frame;
    AiEntityAttemptResult result;     // kept/deduped/published/rejected/failed
    AiEntityRejectCode reject_code;   // mask/live/planner/publish를 모두 표현
};
```

`issued_frame`은 engine이 명령을 이미 수행했다는 뜻이 아니라 ordered packet
accept가 성공했다는 뜻이다. publish 직후 status는 AWAITING_APPLY다. 기존
`UnitCommandContext::on_command_acknowledged` callback을 compose하여 promoted
`active_command_payload`가 기대한 kind/target/point와 일치하고 아래 packet origin도
정확히 일치할 때만 ACTIVE와 `applied_frame`을 설정한다. 다른/오래된 payload ACK는
현재 record를 활성화하지 않는다. AWAITING_APPLY 동안 idle/progress/completion
timer는 전부 정지한다.

matching ACK frame에는 `idle_candidate_frames=0`, `last_progress=(unit.x,unit.y)`,
`last_progress_frame=applied_frame=current_frame`으로 seed한다. issued frame이나
zero-initialized frame을 stall 기준으로 재사용하지 않는다. ACK frame에는 ACTIVE
command rule을 더 실행하지 않고 다음 simulation frame부터 센다.

같은 payload의 오래된 ACK도 content 비교만으로는 구분할 수 없다. ordered packet
receiver는 unit pending command를 생성/교체할 때 AI-only sidecar에
`(source EntityKey, reliable channel, consumed sequence)`를 기록한다. deferred command가
있으면 각 deferred slot에도 origin을 함께 mirror하고 pop 때 pending sidecar로
옮긴다. pending promotion 직전 origin을 active sidecar로 승계한 뒤 ACK callback이
이를 읽는다. ACTIVE 전이의 필수 조건은 다음 둘 모두다.

```text
delivery_seen_origin == (record.ordered_packet_channel,
                         record.ordered_packet_sequence)
acknowledged_active_origin == delivery_seen_origin
```

즉 reliable read sequence가 단순히 해당 번호를 지났다는 사실만으로 ACK하지 않는다.
새 X를 publish했지만 delivery 전에 예전 X가 promote돼도 origin mismatch라서 새
record는 AWAITING_APPLY를 유지한다. 이 sidecar는 AI controller metadata일 뿐 engine
command payload나 P2P simulation/checksum state를 바꾸지 않으며 activation/control
epoch reset 때 함께 폐기한다.

origin은 payload와 lockstep인 provenance라서 receiver 경로만 hook해서는 안 된다.
pending/deferred/active payload의 모든 write·overwrite·copy·queue shift·pop·remove·clear
경로는 같은 index의 sidecar에도 똑같이 적용해야 한다. ordered packet consume에서
유래하지 않은 script/lifecycle/unit-action/cancel/초기화 write는 origin을 추측하거나
이전 값을 보존하지 않고 `INVALID`로 쓴다. 특히 현재 코드의 direct mutation 경계인
`ranker_gameplay_script.cpp`, `ranker_unit_commands.cpp`, `ranker_unit_action.cpp`,
`ranker_unit_lifecycle.cpp`, `ranker_winmain.cpp`를 전부 audit하여 작은 payload+origin
helper로 감싸고, debug build는 payload empty와 origin INVALID 및 queue shift slot의
대응을 assert한다. INVALID/missing origin ACK는 content가 같아도 절대 ACTIVE가 아니다.
여기서 payload empty는 selector-validity, 현재 typed contract에서는 정확히
`payload.state == 0`만 뜻한다. original parity상 promotion 뒤 pending의
`x/y/value` residual은 의도적으로 남으므로 struct 전체 zero를 assert하거나
sidecar 때문에 residual을 clear하지 않는다.

ACK와 ACTIVE 추적은 동일한 canonical payload-normalization helper를 사용한다.
target command는 selector+target EntityKey, point command는 selector+absolute point,
HOLD/STOP은 selector를 비교한다. ACTIVE 뒤에도 매 frame 현재
`active_command_payload`와 이 tuple을 비교하며 결과를 `engine_order_match`로 다음
관측에 보낸다. stale `unit.target` pointer만으로 active 여부를 판단하지 않는다.
STOP의 normalized selector 0은 유효한 payload다. `active_origin`이 expected origin과
같으면 state 0이어도 MATCH이고, origin이 INVALID인 empty state 0일 때만 CLEARED다.

matching ACK를 영원히 기다리지 않는다. 해당 channel의 reliable consumer가 checked
monotonic sequence로 expected sequence를 처리했고 exact delivery origin과 matching
`pending_command`가 있으면 lockout 동안 기다린다. exact origin 없이 consumer만
지났거나, origin/payload가 다른 것으로 바뀌거나, matching payload도 ACK도 없는
상태가 8 frame 지속되면 INTERRUPTED다. 어떤 경우든 `issued_frame + 256`까지 matching ACK가
없으면 apply-timeout INTERRUPTED로 전이한다. 이 상태에서는 같은 ISSUE를 다시
낼 수 있다. delivery/ACK/timeout frame은 every-frame tracker가 갱신하고 order
status/age로 정책에 보인다.
source generation/deactivation이면 record를 폐기한다. target generation mismatch는
old target key를 보존한 채 TARGET_LOST로 전이해 다음 observation에 원인을 남긴다.
source control epoch/owner/direct-control signature mismatch도 record를 즉시 폐기하며,
이전 owner의 last attempt를 새 owner에게 노출하지 않는다.

정확한 전이는 다음과 같다.

### KEEP

- 패킷 0개
- engine command를 건드리지 않음
- issued active order와 age를 유지
- completed/target_lost/stalled/interrupted 상태도 자동 변경하지 않음

### 새로운 ISSUE

- strict mask/receiver/live planner/packet encode가 모두 승인된 row만 batch에 포함
- frame batch commit hook에서 해당 source의 assigned origin이 확정될 때만
  AWAITING_APPLY record 교체와
  `issued_frame` reset
- planner/encode 실패 row는 기존 active record를 유지하고 last-attempt feedback을
  stage하며, frame transaction 성공 hook에서만 교체
- 포함된 source끼리의 publish 성공은 all-or-none이다. 성공 batch의 각 row outcome은
  PUBLISHED이고, capacity/internal batch 실패는 모든 owner의 planned row를
  TRANSACTION_ABORTED로 닫는다.

### 같은 ISSUE

`(semantic kind, target EntityKey 또는 resolved absolute point)`가
AWAITING_APPLY/ACTIVE record와 같으면 패킷을 내지 않고 age도 reset하지 않는다.
terminal record도
현재 상태가 이미 같은 명령을 만족하면 억제한다. 즉 completed STOP 상태에서
여전히 idle이거나 completed MOVE/ATTACK_MOVE 목적점의 32px 안에 있으면 같은
ISSUE를 매 8 frame 재발행하지 않는다. 상태가 다시 달라졌을 때만 새 ISSUE다.
반대로 status가 TARGET_LOST/STALLED/INTERRUPTED이면 같은 ISSUE도 새 정책 선택으로
간주해 다시 발행할 수 있다.

### 완료

- MOVE/ATTACK_MOVE: 목적점까지 squared Euclidean distance가 `<= 32^2`이고
  masked base command state가 idle(0/1)이면 COMPLETED
- ATTACK_UNIT: target generation 변경·사망·비가시·비적대·class 불가이면
  항상 TARGET_LOST
- HOLD: STOP이나 다른 명령으로 바뀔 때까지 active
- PATROL: raw base state가 patrol family `0x35..0x3a`에 있으면 active. 요청 point가
  현재 위치와 같아 engine이 즉시 idle/pop한 경우는 COMPLETED
- STOP: 엔진이 idle이면 COMPLETED

완료된 같은 명령을 executor가 자동으로 다른 표적에 연결하지 않는다. 다음
행동은 정책이 고른다.

### interrupted와 stalled

v1 executor는 명령을 자동 재발행하지 않는다. every-simulation-frame state
tracker가 다음을 관측해 정책이 다음 8-frame tick에 같은 명령을 다시 낼지 직접
고르게 한다.

- outer state gate는 다음 순서다. source generation/control/ownership invalid면 purge,
  AWAITING/ACTIVE ATTACK_UNIT의 target이 invalid면 TARGET_LOST, 그 다음 status를 분기한다.
- `AWAITING_APPLY`이면 exact-origin ACK와 §9의 delivery 8-frame/absolute 256-frame
  timeout만 처리하고 그 frame tracker를 끝낸다. pending X가 lockout을 기다리는 동안
  기존 active Y가 DIFFERENT로 보이는 것은 정상이다. AWAITING에서는
  completion/canonical mismatch/idle/stall/progress 판정을 절대 실행하지 않는다.
  matching ACK로 ACTIVE가 된 frame도 ACTIVE 규칙은 다음 simulation frame부터 적용한다.
- `ACTIVE`일 때만 아래 completion/payload/MATCH/stall 규칙을 실행한다.
  COMPLETED/TARGET_LOST/STALLED/INTERRUPTED는 KEEP만으로 자동 재개하지 않는다.
- idle은 `(raw_command_state & kUnitCommandStateMask) in {0,1}`이다.
- ACTIVE 내부 전이 우선순위는 COMPLETED → canonical payload state → command별 MATCH
  규칙 → STALLED 순서로 고정한다.
- `DIFFERENT`(다른 nonzero payload)는 모든 command를 즉시 INTERRUPTED로 만든다.
  MOVE/ATTACK_MOVE뿐 아니라 HOLD/STOP도 외부 override 뒤 같은 ISSUE를 다시 낼 수 있다.
- `CLEARED`는 stale `unit.target`과 range를 보지 않는다. STOP은 앞 단계에서 idle
  COMPLETED가 되고, MOVE/ATTACK_MOVE/PATROL/ATTACK_UNIT/HOLD는 idle 4 frame 뒤
  INTERRUPTED다. MOVE/ATTACK_MOVE 목적점과 PATROL 요청점 만족은 앞 단계의
  COMPLETED가 우선한다.
- `MATCH`인 MOVE는 목적점 밖에서 idle이 4 frame 연속이면 INTERRUPTED다.
- `MATCH`인 ATTACK_MOVE는 목적점 밖, idle 4 frame, attack recovery 0이고 현재 engine
  target에 대한 `ValidateUnitActionTarget(...).valid && .in_range`가 false일 때만
  INTERRUPTED다. 이 contact 예외를 CLEARED/DIFFERENT에 적용하지 않는다.
- `MATCH`인 ATTACK_UNIT은 semantic target이 아직 valid하지만 engine target key가
  다르고 idle 4 frame, attack recovery 0이면 target range와 무관하게 INTERRUPTED다.
- `MATCH`인 PATROL은 patrol family `0x35..0x3a`를 유지한다. 이 family 밖에서 idle 4
  frame이면 현재 위치가 요청 point일 때 COMPLETED, 아니면 INTERRUPTED다.
- `MATCH`인 HOLD는 유지하고 STOP은 위 completion 규칙으로 닫는다.

STALLED도 translation progress가 필요한 MOVE, 위 조건의 ATTACK_MOVE, valid하지만
실제 reach 밖인 ATTACK_UNIT, patrol translation leg `0x37/0x38`에만 적용한다. patrol
combat state `0x39/0x3a`에서는 freeze한다. 매 simulation frame
현재 위치와 `last_progress_x/y`의 squared Euclidean distance가 `>= 8^2`이면
progress point/frame을 갱신한다. combat contact/recovery 중, patrol combat
`0x39/0x3a`, HOLD/STOP이면 timer를 진행하지 않는다. progress가 필요한 상태로 48
frame이 지나면 STALLED다. INTERRUPTED/STALLED에서는 자동 packet이 없고 다음
정책 ISSUE만 order를 재개한다.

"timer를 진행하지 않는다"는 단순 early-return이 아니다. progress가 필요 없거나
freeze 조건인 매 frame에는 `last_progress=(current x,y)`와
`last_progress_frame=current_frame`을 전진시켜 frozen 시간이 나중의 48-frame 차이에
섞이지 않게 한다. progress-required frame에서만 8px 이동 여부와 48-frame elapsed를
계산한다. command별 idle predicate가 한 frame이라도 false면
`idle_candidate_frames=0`으로 reset하여 반드시 연속 4 frame만 인정한다.

unit 사망·deactivation 시 record를 제거한다. 같은 runtime id가 다시
활성화되더라도 generation이 다르므로 이전 source order나 target latch를 절대
상속하지 않는다. deactivation 없는 owner/direct-control signature 변경은 같은
효과를 control epoch으로 보장한다.

## 10. 모델과 PPO 손실

### 10.1 네트워크

초기 모델은 다음 정도면 충분하다.

```text
global 802
  -> 기존 9-channel 8x8 CNN + scalar tower -> global embedding

own categorical embeddings + own continuous 33
  -> shared own encoder

target categorical embeddings + target continuous 14
  -> shared target encoder

own/target pooled context + global embedding
  -> per-own contextual embedding
       -> KEEP/ISSUE Bernoulli gate
       -> ISSUE command head [6]
       -> command-conditioned point head [96]
       -> ATTACK_UNIT pointer:
            Q(own) dot K(target)
            + MLP(relative dx, dy, distance, pair features)

global + macro gate state + masked pool(own) + masked pool(target)
  -> entity centralized team value V_entity(s)

separate macro global/history tower
  -> macro policy (due일 때만) + V_macro(s) (모든 entity/final state에서 평가 가능)
```

v1은 DeepSets/1~2 layer attention으로 시작한다. Transformer는 entity 수가 큰
FFA 확장 전까지 필수가 아니다. type id와 raw command state는 float로
정규화하지 않고 embedding으로 처리한다.

target pointer의 relative MLP를 생략하면 문서의 pair-relative 좌표가 실제
score에 들어간다는 보장이 없다. target entropy는 legal target 수에 따라
커지므로 head별 coefficient와 `H/log(max(n_legal,2))` 진단값을 별도로 둔다.

기존 macro policy tower의 호환되는 weight만 warm-load할 수 있다. objective
action mask와 autopilot이 달라졌으므로 완전한 동일 분포 checkpoint로 취급하지
않으며, 기존 value head는 새 critic으로 load하지 않는다. macro tower/optimizer는
entity tower와 분리하여 빈도가 훨씬 높은 entity gradient가 macro 표현을
덮지 않게 한다. 새 entity head는 shadow BC 또는 calibrated KEEP gate bias로
초기화하고, BC 뒤 random critic은 actor를 freeze한 value-only warmup을 거친다.

### 10.2 action log-prob

유닛 i의 log-prob는 다음이다. 7개 외부 action index와 network 내부 gate의
mapping은 `issue=0 -> KEEP(0)`, `issue=1 -> command(1..6)`으로 고정한다.

```text
logp_i = log P(issue_i)
       + I(issue_i) * [
           log P(non_keep_command_i | issue_i)
           + I(point command) * log P(point_i | command_i)
           + I(ATTACK_UNIT)   * log P(target_i | command_i)
         ]
```

전체 유닛 log-prob를 더해 하나의 PPO ratio로 만들지 않는다. 병력이 늘수록
ratio variance가 폭발하기 때문이다. 각 entity를 parameter-sharing agent로
보고 같은 team advantage를 broadcast한다.

병력이 많은 timestep이 같은 team advantage를 `U_t`번 복제하지 않도록 전체
ragged row를 한 번에 평균하지 않고 timestep을 먼저 균등 가중한다.

```text
ratio_ti = exp(new_logp_ti - old_logp_ti)
L_entity = mean_t [
    (1 / U_trainable_t) * sum_i
      min(ratio_ti * A_team_t,
          clip(ratio_ti, 1-eps, 1+eps) * A_team_t)
]
```

`U_trainable_t == 0`인 actor term은 건너뛴다. action 시점에 살아 있던 entity는
다음 tick 전에 죽어도 그 action row를 유지한다. `A_team_t`는 batch의 timestep
sample들 사이에서 한 번 정규화한 뒤 entity에 broadcast하고, centralized value
loss는 timestep당 딱 한 번 계산한다. 이는 exact joint PPO가 아니라 small-KL parameter-sharing
MAPPO 근사다. 한 유닛의 kill이 같은 tick 다른 유닛에도 credit되는 한계가
측정되면 action-conditioned Q/counterfactual baseline이나 combat-event
attribution auxiliary head를 추가한다.

macro actor loss는 latched due event끼리의 별도 SMDP rollout에서만 계산하고,
macro critic은 아래처럼 모든 entity/final state에서도 학습한다. entropy도 ISSUE gate, non-KEEP command, point, target별로 나눈다.
gate entropy coefficient는 작게 두고 BC-KL을 anneal한다. 일반 7-way entropy로
KEEP을 1/7 쪽으로 밀지 않는다.

### 10.3 시간 할인

action row `t`에는 과거가 아니라 그 action이 적용된 미래 구간을 저장한다.
다음 request가 오면 `transition_dt[t] = frame[t+1] - frame[t]`를 사후 채우고,
terminal에서는 `final_frame - frame[t]`를 반드시 채운다. 기본은 8이지만
terminal 직전과 향후 cadence 변경을 안전하게 처리한다.

```text
gamma_dt  = gamma_8  ^ (transition_dt / 8)
lambda_dt = lambda_8 ^ (transition_dt / 8)
δ_t       = reward_t + gamma_dt * V(s_next) * nonterminal - V(s_t)
GAE_t     = δ_t + gamma_dt * lambda_dt * nonterminal * GAE_next
```

현재 Python의 장기 horizon 값 `gamma_8 = 0.9998`을 시작점으로 사용한다.
`terminated`(승/패/무)는 bootstrap 0, time-limit `truncated`는 TERMINAL의 full
final observation value로 bootstrap한다. IPC/protocol 실패는 환경 truncation이
아니며 마지막 unsealed action만 버리고, 이전에 완전히 seal된 prefix는
`collector_cutoff`와 마지막 유효 observation value로 닫는다. evaluation 결과는
infrastructure-invalid로 표시한다.

macro due frame을 `tau_k`, 그 사이 sealed entity transition 시작 frame을 `f_n`이라
하면 macro SMDP target은 다음과 같다.

```text
R_k      = sum_n gamma_8^((f_n-tau_k)/8) * r_macro_n
Gamma_k  = gamma_8^((tau_next-tau_k)/8)
Lambda_k = lambda_8^((tau_next-tau_k)/8)
delta_k  = R_k + Gamma_k * V_macro(s_next) * nonterminal - V_macro(s_tau_k)
GAE_k    = delta_k + Gamma_k * Lambda_k * nonterminal * GAE_next
```

`tau_next`는 다음 due frame 또는 final/cutoff frame이다. macro actor advantage는
due row에만 존재한다. `V_macro`는 current macro latch와 gate state를 조건으로
모든 entity/final state에서도 평가·학습하여 due 사이 truncated/cutoff state를
올바르게 bootstrap한다. entity storage chunk 경계만으로 pending macro transition을
임의 종료하지 않는다. 중간 frame의 critic target은 그 frame부터 `tau_next`까지의
residual discounted macro reward와 `V_macro(s_next)`로 같은 식을 적용한다.

### 10.4 reward

현재 `ranker_ppo.py` reward는 zero-sum이 아닐 수 있는 economy growth와
army-centroid approach shaping까지 포함한다. 이를 모든 fighter에 그대로
broadcast하지 않는다. act2 trace는 reward 완성값보다 raw war/economy/terminal
material을 authoritative하게 기록하고 learner에서 head별로 한 번만 계산한다.
`cumulative_losses`는 reward 계산 전용 privileged material이며 actor나 critic
input에 concat하지 않는다.

episode 시작 시 owner별 `episode_hostile_owner_mask`를 한 번 고정한다. 값은 그
시점의 active competitor owner mask에서 자기 자신, relation mask의 friendly/ally,
neutral/system owner를 뺀 것이다. 이후 script alliance나 slot ownership transfer가
일어나도 이 reward opponent set은 바꾸지 않는다. 그렇지 않으면 누적 hostile loss
합이 관계 변경 순간 감소해 음의 가짜 reward가 생긴다. 아래 hostile loss는 매
snapshot에서 이 frozen mask owner들의 runtime `u64` loss counter를 합한 값이며,
네 counter 모두 episode 동안 단조 증가해야 한다.
현재 `g_runtime.ai_play_unit_value_lost/building_value_lost` 자체는 `u64`지만
`run_default_ai_play_owner`의 reward sample과 `AiRlOwnerTrace::prev_losses`가 이를
`u32`로 좁힌다. act2 경로는 둘을 `std::array<u64,4>`로 분리해 32-bit cast 없이
끝까지 유지한다.

```text
delta_war = 5.0 * (
    delta(hostile unit+building value lost)
  - delta(own unit+building value lost)) / 1000.0

delta_econ = 2.0 * max(0,
    tanh(army_value_next/8000) - tanh(army_value_prev/8000))

entity reward = delta_war + terminal_payoff
macro reward  = delta_war + delta_econ + terminal_payoff
approach_weight = 0.0
```

`army_value` type/cost table은 현재 `_ARMY_VALUE_TERMS`를 reward metadata id로
freeze한다. TERMINAL의 `terminal_outcome`은 기존 enum과 맞춰
`ONGOING=0, WIN=1, LOSS=2, DRAW=3`이다. `terminated` transition 마지막 한 번에만
`terminal_payoff={0,+6,-6,0}`을 더하고, time-limit `truncated`에는 0을 더한다.
early-finish/material multiplier는 v1에서 쓰지 않는다. discount 자체가 빠른 승리를
선호한다. `entity reward`는 paired self-play에서 정확히 zero-sum이다. macro의
own-only positive `delta_econ`은 bounded auxiliary라서 macro reward 전체는
zero-sum이 아니며 이 사실을 component별 log에 명시한다. outcome은 fog
observation의 "보이는 적 없음"으로 추정하지 않고 authoritative session result를
owner perspective로 변환한다.

macro reward는 due-to-due 구간을 frame discount로 누적한다. entity combat
curriculum에서 approach shaping이 꼭 필요하다는 실험 근거가 생긴 경우에만
별도 실험으로 켜고 최종에는 0으로 anneal한다. C++ legacy potential reward의
`gamma=0.997`과 learner `gamma_8`을 이중 적용하지 않는다.

명령 자체에 "공격은 좋다", "후퇴는 나쁘다" 같은 보상을 추가하지 않는다.
그런 shaping은 되찾은 결정권을 다시 규칙으로 고정한다.

아래 항목은 진단으로 기록하되 초기 reward에는 넣지 않는다.

- changed order 수 / published packet 수
- 같은 명령 억제 수
- invalid/rejected/stalled 수
- KEEP 비율과 command switch 빈도
- target completion과 target-lost 수

명령 진동이 실제로 학습을 방해한다고 측정된 뒤에만 아주 작은 packet/change
cost를 ablation으로 검토한다.

## 11. versioned IPC 계약

기존 newline-JSON `act`는 legacy checkpoint용으로 그대로 유지한다. entity mode는
새 `act2` socket/message kind를 쓰며 길이로 버전을 추정하지 않는다. shadow
단계에서는 아래 논리 JSON을 쓸 수 있지만 고속 rollout은 같은 배열을 binary로
보낸다. JSON과 binary를 한 connection에서 섞지 않고 HELLO에서 mode를 고정한다.

요청 개념형은 다음과 같다.

```json
{
  "t": "act2",
  "protocol": 2,
  "observation_schema": 5,
  "global_feature_version": 10,
  "entity_feature_version": 1,
  "entity_action_version": 1,
  "semantic_action_version": 2,
  "point_geometry_version": 1,
  "owner": 1,
  "episode": 37,
  "frame": 1232,
  "sequence": 154,
  "reply_to_sequence": 153,
  "entity_policy_version": 21,
  "macro_policy_version": 8,
  "since_previous_request": 8,
  "macro_due": 0,
  "macro_gate": {"elapsed": 8, "deadline_remaining": 56},
  "feat": ["802 floats"],
  "cumulative_losses_u64": [120, 0, 350, 80],
  "macro_mask": ["80 bits"],
  "own_id": [944, 1412],
  "own_generation": [7, 3],
  "own_control_epoch": [2, 1],
  "own_cat": [["categorical fields"], ["..."]],
  "own_feat": [["33 floats"], ["..."]],
  "command_mask_words": [127, 127],
  "point_mask_words": [[4294967295,4294967295,4294967295], ["..."]],
  "target_id": [1888, 2360],
  "target_generation": [4, 9],
  "target_cat": [["categorical fields"], ["..."]],
  "target_feat": [["14 floats"], ["..."]],
  "pair_mask_words": [[3], [2]]
}
```

응답은 own row와 같은 길이의 dense array다. target을 쓰지 않는 command의
`point/target`은 반드시 `-1`이다.

```json
{
  "protocol": 2,
  "observation_schema": 5,
  "global_feature_version": 10,
  "entity_feature_version": 1,
  "entity_action_version": 1,
  "semantic_action_version": 2,
  "point_geometry_version": 1,
  "owner": 1,
  "episode": 37,
  "frame": 1232,
  "sequence": 154,
  "reply_to_sequence": 154,
  "entity_policy_version": 21,
  "macro_policy_version": 8,
  "macro": 0,
  "macro_target": -1,
  "command": [0, 4],
  "point": [-1, -1],
  "target": [-1, 0]
}
```

응답 배열은 request own row와 같은 positional 순서다. row permutation을
허용하지 않는다. target을 쓰지 않는 command의 `point/target`은 반드시 `-1`이고,
JSON object key 순서는 의미가 없다.

### 11.1 binary frame

native C struct를 그대로 보내지 않는다. 모든 integer는 명시가 없는 한 unsigned
little-endian, 실수는 IEEE-754 little-endian `f32`, 배열 사이 implicit padding은
없다. 모든 frame은 아래 96-byte header 뒤 정확히 `payload_bytes`가 온다.

| offset | type | field / 고정값 |
|---:|---|---|
| 0 | `u8[4]` | ASCII `RAI2` |
| 4 | `u16` | `header_bytes=96` |
| 6 | `u16` | `protocol=2` |
| 8 | `u16` | kind: HELLO=1, ACK=2, ACT_REQ=3, ACT_REPLY=4, OUTCOME=5, TERMINAL=6, ERROR=7 |
| 10 | `u16` | flags: bit0 macro_due, bit1 terminated, bit2 truncated |
| 12 | `u32` | payload bytes, 최대 `16*1024*1024` |
| 16 | `u8[8]` | ASCII contract id `ENTCMD01` |
| 24..35 | `u16[6]` | observation=5, global=10, entity-feature=1, entity-action=1, semantic=2, point-geometry=1 |
| 36 | `u32` | owner |
| 40 | `u32` | episode id |
| 44 | `u32` | simulation frame |
| 48 | `u32` | sequence |
| 52 | `u32` | reply-to sequence, 해당 없으면 0 |
| 56 | `u32` | own row count |
| 60 | `u32` | target row count |
| 64 | `u32` | global count=802 |
| 68 | `u32` | macro action count=80 |
| 72 | `u32` | external command count=7 |
| 76 | `u32` | point count=96 |
| 80 | `u32` | payload CRC32 (IEEE reflected polynomial `0xedb88320`, init/xor-out `0xffffffff`) |
| 84 | `u32` | message owner의 pinned entity policy version; connection-level frame은 0 |
| 88 | `u32` | message owner의 pinned macro policy version; connection-level frame은 0 |
| 92 | `u32` | 0 reserved; nonzero면 reject |

ACT_REQ transaction sequence만 `(connection, owner, episode)`별 1부터 증가하고
wrap하지 않는다. REPLY/OUTCOME은 새 번호를 만들지 않고 request 번호를 echo한다.
새 episode/connection은 HELLO로 reset한다. HELLO payload prefix는
`(u32 max_payload_bytes, u32 reply_timeout_ms, u32 run_mode,
u32 controlled_owner_mask)`이고, mask의 owner id 오름차순으로 다음 record를
붙인다.

```text
u32 owner
u32 frozen_hostile_owner_mask      // C++ episode-start reward opponent set
u32 requested_entity_version       // 0xffffffff = configured version
u32 requested_macro_version        // 0xffffffff = configured version
u8  requested_checkpoint_sha256[32]// all-zero = configured fingerprint
```

각 record는 정확히 48 bytes다. ACK는 server가 고른 actual version/fingerprint를
같은 record layout으로 돌려주되 owner와 frozen hostile mask는 byte-for-byte
echo해야 하며 server가 reward opponent를 바꿀 수 없다.
이 table 덕분에 `-AIVS` challenger와 frozen champion이 서로 다른 checkpoint를
한 connection에서 쓸 수 있다. run mode는 evaluation=0/training=1, v1 inference
timeout 기본은 5000ms다. TCP
fragmented/coalesced read를 가정하고 header와 payload를 각각 exact-read한다.
길이는 allocation 전에 64-bit checked arithmetic으로 검증한다.

`send_all`/blocking socket 기본 동작에는 의존하지 않는다. HELLO/ACK가 timeout을
협상하기 전에는 config의 `handshake_timeout_ms`, 협상 뒤에는
`reply_timeout_ms`를 **모든 frame의 exact-read와 exact-write**에 적용한다. deadline은
monotonic clock의 frame header 시작 시각부터 header+payload 전체에 하나이며 partial
progress마다 다시 5000ms로 늘리지 않는다. C++/Python 모두 nonblocking+poll 또는
remaining-time을 재설정하는 bounded socket I/O로 `WOULD_BLOCK/EINTR`, peer close와
deadline을 구분한다. 약 1.1MB인 최대 ACT_REQ/TERMINAL도 peer가 accept만 하고 읽지
않으면 bounded timeout으로 끝나야 한다.

HELLO header의 policy fields는 0이고 owner table이 version을 요청한다. v1은
HELLO_ACK가 owner별로 고른 behavior-policy version/fingerprint를 episode 전체에
pin한다. 모든 ACT_REQ/REPLY/OUTCOME/TERMINAL은 해당 owner의 두 version을 정확히
echo하고 mid-episode weight
swap을 금지한다. 따라서 ACT_REQ가 이전 transition을 seal하면서 다음 action을
요청해도 version 귀속이 모호하지 않다. collector-cutoff는 같은 simulation
episode를 resume하는 segment 경계가 아니라 controller를 끝내는 infrastructure
abort다. 새 weight/HELLO는 terminated/truncated match 또는 abort된 match를 완전히
끝내고 새 gameplay episode id를 시작할 때만 허용한다. learner barrier는 active
ACT_REQ 밖에서 기다리므로 정상 PPO update가 controller failure를 만들지 않는다.

kind별 header invariant는 다음과 같다.

| kind | `sequence` / `reply_to` | owner/count/flags | payload |
|---|---|---|---|
| HELLO | `0 / 0` | episode>0, frame=0, owner=`0xffffffff`, U=E=0, fixed counts(global/macro/command/point=`802/80/7/96`), flags=0, policy fields=0 | prefix + requested owner table |
| ACK(HELLO) | `0 / 0` | HELLO episode/frame/count/flags echo, policy fields=0 | prefix + actual owner table |
| ACT_REQ | 새 `q` / 이전 q 또는 0 | 한 owner, current U/E, macro_due만 허용 | full request body |
| ACT_REPLY | `q / q` | request owner/frame/U/E/macro_due echo | dense action body |
| OUTCOME | `q / q` | request owner/frame/U/E/policy/macro_due echo | publish outcome body |
| TERMINAL | 마지막 q 또는 0 / 같은 값 | 한 owner, final U/E, final macro_due + (terminated XOR truncated) | outcome + full final body |
| ACK(TERMINAL) | TERMINAL echo | TERMINAL owner/frame/U/E/fixed-counts/policy/flags echo | empty |
| ERROR | offending q 또는 0 / 같은 값 | 가능한 header context, flags=0 | `u16 code,u16 n,u8 utf8[n]`, `n<=1024` |

TERMINAL은 controlled owner마다 owner perspective observation/loss/outcome으로 한
frame씩 보낸다. `-AIVS`처럼 둘이면 owner id 오름차순으로 보내고 각각 ACK를 받은
뒤에만 connection을 닫는다. server는 HELLO의 owner mask에 있는 TERMINAL을 모두
받아야 episode를 완료한다. TERMINAL도 bit0에 final `macro_due`를 보존해 truncated
critic state가 Markov하도록 한다. 모든 미정의 flag와 reserved 값은 0이고
terminated와 truncated를 동시에 세울 수 없다.

따라서 negotiated `reply_timeout_ms`는 ACT_REPLY read, ACT_REQ/OUTCOME/TERMINAL write,
server의 ACT_REPLY/ACK write와 각 TERMINAL ACK read에 모두 적용된다. TERMINAL write나
ACK가 timeout이면 이미 만든 local compact trace/final chunk를 flush하고 episode를
infrastructure-invalid로 표시한 뒤 socket을 닫는다. 종료 경로가 server 정지 때문에
무한 대기하지 않는다.

ACT_REQ payload는 다음 SoA 배열을 이 순서로 tight-pack한다.

```text
f32 global[802]
f32 macro_gate[2]                 // elapsed/64, deadline_remaining/64
u32 macro_mask[3]                 // unused high bits는 0
u64 cumulative_losses[4]          // own unit/building, hostile unit/building
u32 own_id[U], own_generation[U], own_control_epoch[U]
u16 own_type_id[U]
u32 own_movement_class[U]
u32 own_distance_check_mode[U]
u8  own_role[U]
u32 own_render_class[U]
u32 own_command_base[U], own_command_state_high_flags[U]
u32 own_unit_command_flags[U], own_movement_state[U]
u8  own_semantic_order[U], own_order_status[U], own_presence_bits[U]
u8  own_engine_order_match[U]
u8  own_last_attempt_command[U], own_last_attempt_result[U]
u16 own_last_reject_code[U]
i32 own_active_target_row[U]
u32 own_attackable_class_mask[U]
f32 own_feature[U][33]             // row-major
u32 command_mask[U]                // low 7 bits
u32 point_mask[U][3]
u32 target_id[E], target_generation[E]
u16 target_type_id[E]
u8  target_owner[E], target_role[E]
u32 target_render_class[E], target_kind_bits[E]
f32 target_feature[E][14]          // row-major
u32 attack_pair_mask[U][ceil(E/32)]// row-major; E=0이면 0 words
```

따라서 ACT_REQ full body 크기는 overflow-checked 식
`3260 + 207*U + 76*E + 4*U*ceil(E/32)` bytes이고 TERMINAL은 앞의 outcome `u32`
때문에 여기에 4 bytes를 더한다. `own_control_epoch`은 stale reply 검증용 opaque
token이라 encoder 입력에 넣지 않는다. `own_distance_check_mode`와
`own_engine_order_match`는 위 categorical vocabulary로 encode한다.
JSON 예시의 `since_previous_request`는 진단용 중복값이다. binary에서는 owner별 이전
header frame과 현재 frame의 checked difference로 derive하며 payload에 다시 넣지 않는다.

loss counter는 runtime의 `u64`를 좁히지 않는다. hostile 두 값은 HELLO에서
echo된 frozen hostile owner mask의 합이고, 네 값 모두 owner perspective에서 단조
증가해야 하며 감소나 wrap은 contract error다. `own_presence_bits`의 bit0..7은 destination/path/engine-target/semantic-point/
render-class-OOB/command-base-OOB/movement-state-OOB/movement-class-OOB다. `target_kind_bits`의
bit0..3은 mobile/building/neutral/render-class-OOB이며 나머지는 0이어야 한다.
raw render class는 보존하되 network embedding은 OOB bit와 UNK category를 쓴다.
모든 bitset은 LSB-first다: macro/action/point/target/entity index `k`는
`word=k>>5, bit=k&31`이다. 마지막 word의 count 밖 high bits는 0이어야 하고,
pair row는 target index, trainable mask는 own index를 이 방식으로 매핑한다.

ACT_REPLY payload는 `i32 macro, i32 macro_target, u8 command[U],
i32 point[U], i32 target[U]` 순서다. schema/count는 request header와 같고 policy
version은 실제 sample에 사용한 값을 쓴다.
C++이 publish를 끝낸 직후 같은 sequence의 OUTCOME을 일방향 전송한다. payload는
`u16 macro_result, u16 macro_reject_code, u8 macro_trainable, u8 reserved[3],
u16 entity_result[U], u16 entity_reject_code[U],
u32 trainable_mask[ceil(U/32)]`다. result enum은
`KEPT=0, DEDUPED=1, PUBLISHED=2, REJECTED_MASK=3, REJECTED_STALE=4,
PLANNER_FAILED=5, ENCODE_FAILED=6, NOT_DUE=7, TRANSACTION_ABORTED=8`로 고정한다. entity row와 due인
macro는 result 0..2일 때만 trainable=1이고 macro가 due가 아니면 NOT_DUE/0이다.
entity trainable bit도 result 0..2와 정확히 일치해야 하며 불일치하면 protocol
error다. reject code는 `NONE=0, OUT_OF_RANGE=1, MASKED=2, STALE_SOURCE=3,
STALE_TARGET=4, OWNERSHIP=5, INACTIVE=6, VISIBILITY=7, HOSTILITY=8,
CAPABILITY=9, CLASS=10, TERRAIN=11, POINT=12, PLANNER=13, ENCODE=14,
INTERNAL=15, TRANSPORT_CAPACITY=16`다. ENCODE는 atomic batch 전 packet encode
실패에만 쓴다. 이는 engine
수행 완료 ack가 아니라 validation/precommit outcome이다.
그 시점에는 미래 interval/reward를 모르므로 둘 다 넣지 않는다.

ring preflight가 실패한 frame transaction은 모든 controlled owner의 entity row와
due macro를
`TRANSACTION_ABORTED/TRANSPORT_CAPACITY`, trainable=0으로 보낸다. non-due macro만
`NOT_DUE/NONE`, trainable=0을 유지한다. packet을 하나도 내지 않았으므로 KEEP이나
부분 성공으로 간주하지 않으며, 이어지는 controller cutoff에서 이 open action은
PPO transition으로 seal하지 않는다.

다음 ACT_REQ는 header의 `reply-to=직전 sequence`, 현재 frame, 현재
`cumulative_losses`와 global/entity observation을 함께 보내 직전 transition을
seal한다. learner는 두 snapshot의 loss counter/global feature 차이로 §10.4의
reward를 만들고 frame 차이로 `transition_dt`를 만든다. episode가 먼저 끝나면
TERMINAL header의 `reply-to=마지막 sequence`와 final frame을 사용한다. TERMINAL
payload는 `u32 terminal_outcome` 뒤에 ACT_REQ와 동일한 full observation body를
담으며 reply/action은 요구하지 않는다. true terminal과 time-limit 모두 final
entity observation을 보내므로 truncated critic bootstrap도 가능하다. terminated면
outcome은 WIN/LOSS/DRAW 중 하나이고 truncated면 반드시 ONGOING이다.

C++/Python golden-byte fixture가 이 표의 normative test다.

### 11.2 오류와 on-policy 계약

다음 framing 오류는 응답 전체를 거부하고 socket을 닫는다.

- magic/contract/schema/version/owner/episode/frame/sequence 불일치
- payload length/CRC/count/array shape 불일치 또는 timeout

HELLO/ACT_REQ write 또는 ACT_REPLY read처럼 publish 전 framing/I/O 실패인 경우에는
어떤 macro/entity packet도 publish하지 않아야 한다. episode에
persistent `controller_failed` latch를 세워 이후 fighter는 KEEP, macro는 no-op만
수행하며 old strategic micro나 현재 random fallback으로 절대 전환하지 않는다.
training collector는 실패한 request/action과 아직 seal되지 않은 transition만
버리고, 마지막으로 수신 완료된 full observation에서 이전 sealed prefix를
bootstrap한다. 이미 seal된 chunk를 소급 무효화하지 않는다. evaluation 결과는
episode 전체를 infrastructure-invalid로 표시하고, 게임 자체는 진단을 남긴 채
scripted worker economy만 계속할 수 있다.

OUTCOME exact-write나 LOCAL_BROADCAST post-commit send처럼 publish 뒤 I/O가 실패하면
이미 발행한 gameplay packet을 rollback하지 않는다. C++ local trace에는 source별 실제 publish 결과를 남기되 server가
seal할 수 없는 그 open action부터 rollout tail을 버리고 같은 persistent cutoff로
간다. 이전 sealed prefix만 마지막 full observation으로 bootstrap한다.

반면 enum/index/mask 위반이나 snapshot 뒤 live target 소실은 해당 row만
reject하고 다른 row는 계속 처리한다. 기존 active order는 유지하고 OUTCOME의
그 row `trainable=0`과 reject code를 돌려준다. macro 오류도 macro row만
trainable=false다. 이 row별 분리는 atomic batch **전** mask/live/planner/encode
단계까지만 허용된다. batch에 포함된 row는 전부 PUBLISHED 또는 전부
TRANSACTION_ABORTED다. learner는 sampled action과 실제 행동이 다른 row를 actor
loss에서 제외하며, exact request rows/masks와 outcome을 함께 보존한다.

## 12. episode와 checkpoint 계약

현재 game trace는 `feat + mask + one action + one target`만 저장하며 target
mask조차 episode에 없다. 기존 JSONL로 entity action을 소급 학습할 수 없다.

full request tensor는 C++ JSONL에 매 8 frame 복제하지 않는다. 수백 entity를
그대로 반복하면 한 경기 파일이 수백 MB 이상으로 커진다. 저장 책임을 다음처럼
나눈다.

- 게임의 `ai_entity_trace.jsonl`: 모든 schema/version, owner/episode/sequence/frame,
  owner별 frozen hostile mask와 behavior-policy versions/checkpoint fingerprint,
  outgoing `transition_dt`, `terminated/truncated`,
  sampled macro와 non-KEEP entity edit만 sparse하게 기록하고,
  source `(id,generation,control_epoch)`/target `(id,generation)`, per-unit publish
  outcome/trainable 여부, KEEP 수,
  raw war/economy/terminal material을 보존한다.
- Python의 optional full rollout `.npz`: global feature, macro mask,
  own id+generation+control-epoch와 target id+generation·categorical·continuous row,
  command/point/pair mask,
  sampled command/point/target, old log-prob/value, outcome/trainable mask,
  reward material, transition_dt, terminal flags와 step offset을 저장한다.
- shadow BC dataset: 같은 concat+offset 형식을 사용하고 teacher label을 함께
  저장한다.

온라인 PPO도 full game을 여러 worker의 RAM에 계속 쌓지 않는다. entity storage
chunk는 정확히 `256 sealed transitions + 다음 bootstrap observation`이다. 아직
next observation/reward가 없는 action은 chunk에 넣지 않는다. macro due-to-due
transition은 storage chunk 경계를 넘어 별도 event stream으로 carry한다. packed
chunk에 checkpoint fingerprint와 episode-pinned entity/macro behavior-policy
version을 붙여 임시 파일에 stream한 뒤 atomic rename한다. 이 streaming은
저장/수집이지 즉시 update가 아니다.

TERMINAL/truncated/collector-cutoff가 256 전에 오면 1..255개의 final partial chunk도
반드시 flush한다. true terminal tail은 bootstrap 0, time-limit tail은 TERMINAL full
observation value, collector-cutoff tail은 마지막 수신 완료 full observation value를
쓴다. pending macro event도 같은 final/cutoff frame까지 reward를 seal해 함께
flush한다.

v1은 episode 안에서 weight를 바꾸지 않는다. learner는 같은 immutable policy
version의 synchronous episode/collector batch만 PPO update하며 허용 policy lag는
0이다. update와 느린-worker barrier는 모든 active ACT_REQ가 끝난 episode 경계에서
실행한다. 따라서 한 macro return 안에서 entity version이 바뀌지 않고 IPC reply
timeout과도 경쟁하지 않는다. collector failure 시 open action은 버리고 마지막
full observation으로 sealed partial segment를 bootstrap한다. true terminal이면
bootstrap은 0이다.

crash audit나 offline PPO/BC가 필요한 run에서만 compressed full chunk를 켠다.
compact game trace와 episode/sequence를 맞추면 reward와 실제 수용된 action을 다시
결합할 수 있다. PPO update는 저장된 snapshot mask를 쓰며 live mask를 재계산하지
않는다.

checkpoint metadata는 최소 다음을 검사하고 mismatch면 hard error로 종료한다.

```text
global_feature_version/count = 10 / 802
observation_schema_version   = 5
entity_feature_version       = 1
entity_action_version        = 1
semantic_action_version      = 2
macro_action_count           = 80
own_continuous_count         = 33
target_continuous_count      = 14
command_count                = 7
point_count                  = 96
point_geometry_version       = 1
wire entity hard limit       = 2048
categorical vocabulary ids/counts
KEEP/ISSUE gate + reward specification ids
reward parameters: war_scale=5, loss_weight=1, econ_scale=2, econ_cap=8000,
                   terminal_scale=6, approach_weight=0, army-value-table id,
                   frozen-hostile-owner-set rule id
gamma_8 / lambda_8
architecture id
normalization id
contract id / checkpoint fingerprint
entity/macro behavior-policy versions
```

## 13. 학습 시작 방법

entity head를 완전 random으로 바로 self-play에 넣으면 전투 유닛마다 7-way
command가 독립적으로 흔들릴 가능성이 크다. 기존 executor는 controller로
남기지 않고 teacher로 한 번만 활용한다.

### 13.1 shadow teacher 수집

1. 기존 micro가 경기를 실제로 제어하되 teacher query도 학생과 같은 8-frame
   pre-action cadence에서 수행한다.
2. dedupe 전 teacher의 desired order를 entity observation/mask와 함께 기록한다.
   매-frame 실행 뒤 changed packet만 관찰하면 tick 사이 변경을 다음 KEEP으로
   잘못 label하므로 사용하지 않는다.
3. 지원되는 desired order를 unit별 `(issue, command, point/target)`로 unbatch한다.
4. world point는 같은 resolver로 96 token을 전부 역평가해 nearest token을 고르고
   오차가 metadata의 초기 `teacher_point_max_error_px=64`를 넘으면 제외한다.
5. `pickup_move`처럼 v1에 없는 kind, 현재 mask 밖 label, stale pointer는
   STOP/KEEP으로 바꾸지 않고 명시적인 제외 사유를 기록한다.
6. 명령이 바뀌지 않은 fighter는 KEEP label이다. KEEP을 subsample하면 각 row에
   inclusion probability를 저장하고 inverse-probability BC weight 또는 동등한
   prior-logit 보정을 적용하여 실제 90%+일 수 있는 KEEP prior를 왜곡하지 않는다.

이는 기존 hardcoded micro를 최종 정책으로 유지한다는 뜻이 아니다. actor가
최소한 이동·공격·명령 유지의 시간 의미를 익히는 초기화 데이터다.
offline BC 뒤에는 learner가 만든 state에서 old executor를 실행하지 않는 shadow
oracle로만 query하는 DAgger pass를 짧게 수행해 covariate shift를 줄인다.

### 13.2 단계별 훈련

```text
1. macro checkpoint의 호환 파라미터 load
2. shadow dataset으로 entity actor BC
3. actor freeze + 새 entity critic value-only warmup
4. macro tower/head freeze, old fighter micro/reflex OFF, 소규모 mirrored combat와
   fixed opponent에서 entity head만 PPO
5. command별 entropy/invalid/stall/전투 결과가 안정되면 full match로 확장하고
   별도 macro actor/critic unfreeze
6. frozen snapshot pool self-play
7. old micro 대비 승률 + reflex OFF A/B 평가
```

평가의 주 지표는 승률·소멸 frame·war score다. 명령 수나 공격 빈도를 강함의
대리 지표로 쓰지 않는다.

## 14. v1 이후 확장

전투 외 tactical 기능은 별도 conditional head로 append하고 기존 command index를
재번호화하지 않는다. 단, 일꾼·건물 경제 전체를 직접 제어하는 변경은 macro 계약과
wire 의미까지 달라지므로 ENTCMD01에 append하지 않는다. 별도
[ENTCMD02 직접 경제·전투 제어 계획](AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md)이 이
항목의 worker/economy 초안을 대체한다. ENTCMD02에는 `RETURN_CARGO`가 없으며,
`HARVEST` 한 번 뒤 엔진이 채집과 반납을 자동 반복한다.

- meat pickup: `PICKUP_MOVE` + visible map-effect pointer
- transport: BOARD/UNLOAD + friendly unit/carrier pointer
- merge: recipe-compatible friendly pointer/set
- morph/stance: unit mechanics command
- ability/item: ability id + friendly/enemy/point conditional target
- queued command: queue flag와 queue capacity 관측을 추가한 뒤 활성화

merge/morph/stance/drop을 v1 macro mask에서 뺀 이유는 capability가 없어서가
아니다. 현재 translator가 대상 유닛을 다시 결정하므로 direct control 원칙과
충돌하며, 올바른 entity target head 없이 남기면 정책 결정권이 반쪽이 되기
때문이다.

## 15. 구현 파일 경계

### 신규

- `include/ranker_ai_entity_control.h`
  - schema/constants, EntityKey generation, entity rows, masks, decision/reply,
    active-order/last-attempt/outcome와 pending/deferred/active packet-origin sidecar
- `src/ranker_ai_entity_control.cpp`
  - stable encoding, live movement-map static reachability, authoritative attack
    pair predicate, control-signature epoch, canonical engine-payload match, point
    resolve, validation, KEEP/dedupe/completion/interrupted/stalled, per-unit commit과
    canonical flatten-sort
- `tools/ai/ranker_entity_contract.py`
  - `act2` parse, bit-mask expansion, padded batch
- `tools/ai/ranker_entity_ppo.py`
  - shared entity actor, pointer heads, centralized critic, PPO/BC

### 수정

- `include/src/ranker_ai_observation.*`
  - schema v5 controlled `movement_class`/`distance_check_mode` append,
    command base/high-flags 분리 입력
- `include/src/ranker_ai_ipc.*`
  - HELLO/act2/reply/outcome/terminal, deadline와 persistent failure latch;
    bounded exact-read/write, legacy `act` 보존
- `include/src/ranker_reliable_packets.*`, `include/src/ranker_gameplay_packets.*`
  - per-channel capacity/slot을 한 mutex 구간에서 검증하고 sequence/packet cursor를
    all-or-none commit하며 LOCAL_BROADCAST/LOCALLY_SIMULATED의 기존 range/flush 차이를
    보존하는 AI batch publish API
- `src/ranker_gameplay_packets.cpp`, `include/src/ranker_unit_commands.*`,
  `src/ranker_unit_action.cpp`, `src/ranker_unit_lifecycle.cpp`,
  `src/ranker_gameplay_script.cpp`와 session restore 경계
  - pending/deferred/active payload의 모든 write/copy/shift/pop/clear에 packet-origin
    sidecar를 lockstep 이동하거나 INVALID 처리
- `src/ranker_winmain.cpp`
  - owner loop을 collect-all/preflight-once/publish-after-preflight frame transaction으로
    변경,
    macro/entity cadence, controller ownership, fixed publish order, trace
- `include/src/ranker_ai_micro_executor.*`
  - generic order equality/batching 개념을 entity module로 분리하거나 공유
  - direct-owned fighter를 objective executor에서 제외
- `include/src/ranker_ai_rl_features.*`
  - 기존 80 enum/index 유지, entity-mode macro mask 함수 추가
- `include/src/ranker_ai_rl_reward.*`
  - raw reward material, outgoing transition_dt, final-frame 및
    terminated/truncated trace 계약
- `tools/ai/ranker_ipc_server.py`, `ranker_ppo.py`, `ranker_rl_env.py`,
  `ranker_selfplay.py`
  - act2 routing, rollout merge, checkpoint contract, self-play
- `CMakeLists.txt`
  - main executable와 focused AI regression에 신규 C++ source 추가

## 16. 구현 순서와 완료 조건

### Phase A — pure contract / shadow mode

1. activation generation hook과 observation v5 movement class
2. live movement-map을 받는 entity encoder와 deterministic row order
3. command/point/authoritative attack-pair mask
4. act2 serializer/parser/outcome와 version hard-fail
5. 실제 제어 없이 동일 cadence desired-order shadow dataset 수집

완료 조건:

- active unit vector 순서를 바꿔도 entity bytes가 동일
- 같은 slot/id 재활성화가 새 generation이고 stale source/target이 거부됨
- active owner/direct-control signature 변경은 generation을 유지한 채 control epoch을
  바꾸고 이전 order/attempt 및 왕복 소유권 변경 전 reply를 거부함
- hidden enemy/private state가 target row에 없음
- C++/Python mask round-trip 동일
- class 0/1/2/3/4 point mask와 source distance-check/class-2 attack terrain gate가
  engine과 동일
- 2048 protocol limit 위 입력이 조용한 truncation 없이 hard failure로 진단됨

### Phase B — direct order latch

1. strict framing + bounded I/O + row별 receiver/prevalidator
2. KEEP/같은 order 억제/changed order 발행
3. pending/deferred/active payload 전 mutation 경로의 exact packet-origin sidecar
4. active order와 last-attempt/outcome, completion/target lost/stalled feedback
5. canonical plan/flatten-sort, pre-batch row reject와 atomic batch commit
6. old fighter executor 완전 격리

완료 조건:

- KEEP은 0 packet이고 engine order가 유지됨
- 최초 ISSUE만 packet, active 또는 이미 만족한 같은 ISSUE는 0 packet
- publish 뒤 matching engine ACK까지 AWAITING_APPLY이고 completion/stall timer 0
- AWAITING 중 old active payload가 달라도 changed ISSUE를 INTERRUPTED로 오판하지 않음
- mismatched/missing ACK는 delivery 후 8-frame 조건 또는 absolute 256-frame timeout에
  INTERRUPTED가 되어 same ISSUE가 다시 publish 가능
- content가 같은 old ACK도 `(channel,sequence)` origin이 다르면 ACTIVE 전이 금지
- ACTIVE canonical payload clear/override와 patrol immediate-pop/path failure가
  terminal feedback으로 닫혀 영구 dedupe되지 않음
- AIVS owner별 plan을 전부 모으기 전에는 publish 0이고, 어느 channel의 batch
  preflight 실패도 모든 owner packet/outcome을 원자적으로 abort
- target/point 변경은 즉시 packet
- invalid/stale row는 그 row만 packet 0, 기존 order 유지, trainable=0 outcome
- active-list 입력 순서와 JSON object key 순서를 바꿔도 request bytes와 published
  packet bytes가 동일; positional reply row 순서는 바꾸지 않음

### Phase C — 학습 경로

1. Python KEEP/ISSUE entity model + relative-bias pointer + 별도 centralized critic
2. timestep-nested per-entity log-prob/PPO와 due-to-due macro update
3. packed streaming rollout, outcome, transition_dt, checkpoint metadata
4. weighted shadow BC + value warmup + shadow-oracle DAgger
5. mirrored combat/fixed opponent rollout, 이어서 full match와 self-play

완료 조건:

- 모든 sampled command/point/target이 mask 안에 있음
- 저장한 log-prob를 update 시 동일하게 재계산
- entity 수/padding 변화가 valid row 출력에 영향 없음
- U=0/E=0, terminal partial dt, truncated bootstrap이 finite
- rejected row가 actor loss에서 빠지고 다른 row는 유지됨
- schema/count가 다른 checkpoint를 hard reject

### Phase D — 게임 검증

관련 테스트만 실행한다.

```text
ai_play_interface_regression
ranker_entity_ppo.py --selftest
ranker_rebuild build
고정 seed + 고정 act2 응답 sequence 2회 결정성 비교
entity-mode 단일 self-play replay
```

`mode1_partial_round_receive_regression`은 packet publish/transport 구현 자체를
바꾼 경우에만 추가한다. unrelated gameplay suite는 실행하지 않는다.

## 17. 회귀 테스트 명세

`tests/ai_play_interface_regression.cpp`의 기존 observation/semantic-action
fixture를 재사용하여 다음을 고정한다.

1. own/target EntityKey 사전순, duplicate-active-id fatal과 active-list 순서 독립성
2. startup/scenario-restore/production/script/free/lifecycle 각 activation path의
   generation 1회 증가, detached sparse registry, source/target same slot+same id
   재활성화와 stale reject
3. player-slot transfer/script/action owner 변경과 direct-control signature 변경의
   control-epoch 증가, old state purge, A→B→A stale reply reject
4. dead/foreign/hidden/under-construction entity 제외
5. visible enemy private command/lockout redaction
6. high-flag command state base mask와 unknown bucket
7. MOVE bit4, ATTACK bit5, PATROL bit9, HOLD/STOP mask
8. source `distance_check_mode==1`, class bit, target runtime
   transient/inactive/class-blocked flags,
   class-2 terrain gate,
   `render_class>=32` engine-equivalent pair mask
9. 전략 조건을 mask하지 않는 negative test
10. movement class 0/1/2/3/4, 8-way neighbor, non-divisible 8x8 경계,
   tie-break, local diagonal, dynamic occupancy 무시, point bit order
11. KEEP, first issue, active/satisfied same issue, changed issue, completion,
    pending-command ACK까지 AWAITING_APPLY timer freeze, mismatched/absent ACK의
    8/256-frame escape, 새 X delivery 전에 도착한 old-X ACK의 origin-sequence reject,
    old active Y + new pending X + lockout에서 DIFFERENT/completion/stall 금지,
    non-packet overwrite와 queue cancel/shift 뒤 same-payload ACK의 INVALID reject,
    late ACK frame의 idle/progress baseline reset, freeze 구간 제외, nonconsecutive idle
    reset, exact interrupted/stalled scope와 same-ISSUE resume
12. MATCH/CLEARED/DIFFERENT canonical payload × stale in-range `unit.target`, patrol
    point==current immediate pop과 patrol path failure가 우선순위대로
    COMPLETED/INTERRUPTED/STALLED로 닫힘
13. target 사망/비가시/stale row는 row별 reject, 기존 order 유지
14. id-sort + 14-unit chunk + non-contiguous group flatten packet order
15. 일부 row planner/packet-encode 실패는 기존 record 유지+row별 outcome이고,
    성공 hook에서 staged last-attempt가 commit됨; batch 포함 packet은 all-PUBLISHED
    또는 all-TRANSACTION_ABORTED이고 abort hook은 order/history/RNG를 유지한 채 모든
    row outcome만 transaction-aborted로 override
16. macro due latch와 production/build/research-only mask/history
17. direct-owned fighter가 old reflex/objective/autopilot scout order를 받지 않고
    802의 executor-derived objective/raid summary가 canonical zero/none임
18. one-owner와 two-owner combined max changed-order burst, unread ring
    channel별 `0x7ff/0x800` 경계, macro+worker 포함 collect-all/preflight-once,
    각 channel은 safe지만 aggregate>0x800인 성공 case, 어느 한 channel overflow 시
    모든 owner zero-publish transaction abort와 concurrent accept TOCTOU 방지;
    -AI LOCAL_BROADCAST range/flush parity와 -AIVS LOCALLY_SIMULATED no-broadcast
19. binary golden bytes, fragmented/coalesced frames, bad length/CRC/version,
    accept-but-no-read/slow-partial-read의 HELLO·ACT_REQ·OUTCOME·TERMINAL whole-frame
    read/write deadline, timeout 뒤 persistent failure가 random policy로 복귀하지 않음
20. ACT_REQ→OUTCOME→next ACT_REQ/TERMINAL reward/dt closure와 final observation
21. two-owner TERMINAL fan-out/ACK와 owner-perspective loss/outcome, relation 변경에도
    frozen hostile mask와 u64 cumulative tuple이 감소하지 않음
22. one connection의 owner별 challenger/champion version/fingerprint pin
23. legacy `act` fixture가 그대로 통과

Python selftest는 다음을 포함한다.

1. ragged-to-padded batch와 padding invariance
2. U=0/E=0과 inactive pointer branch가 NaN 없이 동작
3. KEEP/ISSUE·command/point/target conditional mask
4. authoritative pair bitset과 relative-bias pointer legality
5. per-entity log-prob 저장/재계산; update 때 live mask 재계산 금지
6. timestep-nested entity mean과 timestep당 1회 value loss
7. outgoing/terminal transition_dt와 gamma/lambda SMDP GAE,
   final macro_due를 포함한 terminated/truncated bootstrap
8. macro loss/history가 latched `macro_due` event에만 적용됨
9. pre-batch row-reject outcome/trainable mask와 transaction-wide batch-abort join
10. KEEP subsampling importance-weight가 원래 prior loss를 재현
11. one PPO update finite/shape check, 256 chunk와 final partial-tail bootstrap/flush
12. episode-pinned behavior version, 256 sealed chunk, stale version hard reject
13. checkpoint schema/geometry/reward mismatch hard failure

## 18. P2P와 replay 경계

현재 AI-play pump는 `network_player_count > 1`이면 return한다. 이 설계도 우선
로컬 self-play/평가 전용이다. 모델을 실제 live P2P에서 누가 실행하고 packet을
어떻게 broadcast할지는 controller authority와 결정성 검증이 필요한 별도 작업이다.

다만 entity 결과가 기존 ordered gameplay packet으로만 게임 상태를 바꾸므로,
로컬 학습 경기의 `.ply`는 모델 없이 동일 명령을 재생할 수 있어야 한다. direct
controller가 엔진 상태를 packet 밖에서 바꾸면 안 된다.

원본 `ranker.exe`나 P2P flight recorder 형식은 이 작업에서 변경하지 않는다.

## 19. 첫 구현 slice

한 번에 전체 학습기를 갈아엎지 않고 다음 slice를 먼저 완성한다.

```text
combat entities only
7 commands (KEEP 포함)
96 point tokens + visible target pointer
8-frame entity cadence
기존 production/build/research macro
scripted worker harvest only
old fighter objective/reflex OFF
changed order만 existing planner/packet 경로로 발행
```

이 slice가 끝나면 정책은 실제로 다음을 스스로 학습한다.

```text
누가 / 언제 / 어떤 명령을 / 어느 위치나 어느 적에게 내릴지
```

실행기가 남기는 것은 그 선택이 현재 게임 규칙상 가능한지 확인하고, 같은 명령을
불필요하게 다시 보내지 않으며, 결정적인 packet으로 바꾸는 일뿐이다.
