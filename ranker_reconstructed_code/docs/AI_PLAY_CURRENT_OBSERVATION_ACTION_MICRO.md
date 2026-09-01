# AI Play 현재 관측·액션·마이크로 실행기

> 기준: 2026-09-01 코드. Raw observation schema v4, RL feature v10, 802 features, 80 actions, 64 target cells.

이 문서는 현재 AI가 **무엇을 보고**, 정책이 **무엇을 선택하며**, 선택한 액션이 **어떤 실제 유닛 명령으로 풀리는지**를 한 흐름으로 설명한다. 과거 버전의 설계 과정은 기존 문서에 남기고, 여기서는 현행 동작만 다룬다.

## 1. 전체 흐름

```text
게임 상태 (매 simulation frame)
  ↓
AiObservation v4 기본 snapshot 생성
  - 자기 상태 + 현재 보이는 상대
  ↓ 매 frame owner별 fog-honest 기억을 후첨
8-frame gate check 시 이전 micro 상태를 후첨하고
802개 feature + legal_mask[80] + target_mask[64] 생성
  ↓ decision gate 평가; due일 때 decision context를 feature에 패치
정책이 high-level action 1개와 필요하면 8×8 target cell 1개 선택
  ↓
Tyrano high-level translator
  ├─ 생산/건설/연구/합체/변신/stance
  │    → 즉시 AiSemanticAction → live planner → packet
  ├─ raid 편성/drop attack
  │    → 그룹 상태 변경 또는 별도 composite runner 시작
  └─ 전투/정찰 objective 변경
       → AiMicroExecutorStep (매 frame)
       → 상시 채집을 포함한 move/attack/harvest/pickup
       → live planner → packet
```

핵심 경계는 간단하다.

- 정책은 전략적 의도와 선택적 지역만 고른다.
- 번역기와 micro executor가 유닛, 표적, 건설 위치, 이동점을 결정론적으로 고른다.
- `legal_mask`는 학습 보조 장치이고, 실제 게임 상태를 보는 planner/validator가 최종 권위다.
- 활성화된 비-replay·비-live-P2P AI-play 경로에서 정책은 이벤트가 있을 때만 호출되지만 micro executor는 매 frame 실행된다.

## 2. 관측: raw snapshot과 정책 입력은 다르다

### 2.1 Raw `AiObservation` v4

Raw observation은 정책 tensor보다 훨씬 자세한 내부 snapshot이다.

| 범주 | 현재 담기는 내용 |
|---|---|
| 세션·시간 | frame, map 경로/hash 필드, 맵 크기, local owner/faction, active/relation mask, 종료 상태. Live builder의 `map_sha256`는 현재 빈 값 |
| 경제 | primary/secondary/auxiliary 자원, supply, 예약 수요, hard population limit |
| 연구 | 대표 연구 3종과 production order `0..63` 전체 완료 레벨 |
| 시작점 | 자기 실제 시작점과 익명 map start 후보 최대 8개. 상대가 어느 후보인지 직접 주지 않음 |
| 타일 | terrain flags, berry 잔량 기억, passable, explored, visible, buildable, placement class |
| 유닛 공통 | id/type/owner, 위치, HP, 건설 상태, 시야·이동·공격 사거리, target class mask, 공격/방어력, 방향·애니메이션·레벨·경험 |
| 자기 유닛 전용 | command/movement state, 목적지/path target, 현재 target, cargo, 생산·deferred queue, lockout/effect timer, item/equipment |
| 보이는 map effect | 고기 drop(effect id `1..4`)의 id, 위치, 양, claim 여부 |
| fog memory | pump가 기본 snapshot 뒤 매 frame 후첨하는 적 건물 tile, 적 mobile last-seen tile count, 마지막 적군 관측 시간·중심·규모 |
| micro 요약 | RL gate check frame에 인코딩 직전 후첨하는 army/raid A/B/C 인원·objective·tactic, scout 계열 id, attack target/age/contact latch, army 중심 |
| 실행 피드백 | gate check frame에 후첨하는 최근 거부된 건물 type과 경과 frame |

시야 규칙은 다음과 같다.

- 자기 유닛은 항상 관측한다. 상대·중립 유닛은 현재 보일 때만 넣는다.
- 상대의 type-public 정보와 화면에 보이는 상태는 제공하지만, 명령 큐·목적지·target·장비·연구가 적용된 private 수치는 숨긴다.
- 자기 전투 수치는 연구·장비를 반영한 effective 값이고, 상대는 공개 base definition 값이다.
- 지형, 익명 시작 후보, berry 초기 위치·양은 공개 맵 정보다. 안개 속 berry 감소량은 마지막으로 본 값만 유지한다.
- Live AI-play owner 경로의 적 건물·적군 memory는 보이는 tile을 매 frame 다시 쓰고, 안개 속에는 마지막 목격을 남긴다. 따라서 이동한 적의 오래된 흔적이 남는 것은 의도된 정보 모델이다.
- 타일의 `passable`/`buildable`은 정적 지형 판정이다. 유닛과 건물 footprint 같은 동적 점유는 live validator가 다시 확인한다.

Micro 요약은 같은 frame의 executor 실행 전, 즉 **직전 frame까지 누적된 executor 상태**다. 기본 `BuildAiObservationV1`만 호출한 snapshot에는 fog memory와 이 pump 전용 요약이 채워지지 않는다.

인구 필드 이름은 특히 주의한다.

```text
population_used     = 건물이 제공하는 실제 supply
population_reserved = 현재 생산 queue까지 포함한 수요
사용 가능 인구       = max(supply - population_reserved, 0)
supply               = population_limit == 0
                       ? population_used
                       : min(population_used, population_limit)
```

### 2.2 정책 입력 `AiRlStepEncoding` v10

온라인 Python 정책에는 raw snapshot을 보내지 않는다. 다음 세 배열만 보낸다.

```json
{
  "t": "act",
  "owner": 1,
  "frame": 1234,
  "feat":  [802개의 float],
  "mask":  [80개의 0/1],
  "tmask": [64개의 0/1]
}
```

대부분의 feature는 `[0,1]`로 clamp된 집계값이다. 8×8 채널은 channel-major이고, 각 채널 안에서는 `cell = y*8+x` 순서다.

| 인덱스 | 의미 |
|---:|---|
| `0..12` | frame, primary/secondary 자원(secondary는 현재 항상 0), supply/limit/free/reserved, faction one-hot, 종료 여부 |
| `13..30` | 기본 유닛·건물 수/건설 중 수, 채집·idle worker, 자기 army/전체 수 |
| `31..45` | 현재 보이는 적, 대표 연구 3종, 보이는 중립 몬스터, unit `0x22` 수/건설 중 수, nest `0x86/0x87` 수 |
| `46..79` | 추가 연구 10종, 완료 roster 11종, 추가 건물 4종, stance/morph/transport/army/queue 집계 |
| `80..85` | 교전 비율, 국지 HP 전력비, main objective, 저체력 철수 비율 |
| `86..469` | 8×8 6채널: own building, own army, visible enemy mobile, visible+remembered enemy building, berry, explored |
| `470..486` | 자기 시작 cell과 army↔적/기억 건물/미탐색 start/home 방향·거리 |
| `487..508` | 적 조합·tribe·HP·사거리와 자기 army HP/spread/role/행동 상태 |
| `509..530` | 생산·건설 pipeline과 scout 상태 |
| `531..544` | main search/tactic, 확장 후보, 건설 예약·진행·차단, explorer/roamer/build reject |
| `545..567` | 적 mobile type slot 16개와 양측 공격력·대공·전력비 |
| `568..631` | 8×8 적 mobile last-seen memory |
| `632..635` | 마지막 적군 관측 age·방향·규모 |
| `636..699` | 8×8 passable 비율 |
| `700..763` | 8×8 buildable 비율 |
| `764..771` | raid A 크기·생존·objective/tactic/search와 main army 크기 1칸(`[770]`) |
| `772..787` | 지난 결정 간격, decision trigger 12개, worker/pop/fighter autopilot 발동 수 |
| `788..794` | raid B 크기·생존·objective 요약 |
| `795..801` | raid C 동일 |

`[772..787]`은 `EncodeAiObservationForRl()`만 호출하면 0이다. 실제 decision이 열린 뒤 pump가 다음 문맥으로 덮어쓴다.

```text
[772]       지난 policy decision 이후 frame / 64
[773..784]  max interval, production open, completion, enemy seen/lost,
             contact, base threat, own loss, objective done, build reject,
             imitation owner packet, first decision
[785..787]  worker / population nest / fighter autopilot 발동 수 / 8
```

Raw per-unit 목적지, weapon recovery base ticks, command lockout, 장비, 고기 상태 등은 현재 802개 입력에 그대로 들어가지 않는다. 정책은 그 일부를 집계·grid·mask 형태로만 본다.

## 3. 결정 시점과 mask

기본 decision gate는 8 frame마다 상태를 확인하고, 이벤트가 있으면 정책을 호출한다. 아무 이벤트가 없어도 64 frame이 지나면 강제로 호출한다. 첫 결정은 즉시 열린다.

주요 이벤트는 생산 가능 상태의 개방, 유닛/건물/연구 완료, 적 발견·소실, 교전 상태 변화, 본진 위협, 손실, 목표 완료, raid 소멸, scout 역할 해제, 건설 거부다. `no_op`은 “현재 objective 유지”이며, decision이 없는 frame에도 기존 objective와 micro는 계속 진행된다.

정책 action을 objective로 번역한 직후 gate의 objective snapshot을 다시 찍는다. 정책 자신의 변경이 다음 check에서 `objective_done`으로 잘못 재발화하는 것을 막기 위해서다. 방어 action mask는 visible combat mobile을 1200px에서 보지만, `base_threat` trigger/reflex는 기본 800px·micro 전투 role·완성 건물 HP 감소를 사용하므로 두 조건은 완전히 같지 않다.

`legal_mask[80]`의 공통 원칙은 다음과 같다.

| 액션 계열 | 열리는 조건의 요약 |
|---|---|
| `no_op` | 항상 열림. 정책/IPC가 잘못된 액션을 반환했을 때의 실질 fallback |
| 생산 | 완성 producer, queue 여유, 예약 건설비를 뺀 자원, 유닛별 인구 비용, 선행 건물 |
| 건설 | 사용 가능한 worker, 자원·선행조건, explored/비점유/유효 footprint 부지. 같은 type의 거부 후 64 frame backoff |
| 연구 | 지정 researcher가 완성·idle, level cap 미만, 현재 level 비용 충족 |
| 합체·변신·stance·drop | 정확한 roster/research/capability 및 현재 상태. Drop은 carrier `0x29`와 army가 필요 |
| 공격 | 해당 fighting group이 존재하고 visible hostile 또는 기억된 적 건물 위치가 있음 |
| 방어 | 해당 group이 존재하고 자기 건물 1200px 안에 visible combat mobile이 있음. 자기 건물이 하나도 없을 때만 시작점을 anchor로 사용 |
| 후퇴·hold·patrol | 해당 group이 존재함 |
| 사냥 | 도달 가능한 visible neutral이 있음. main army는 중심 1200px 안으로 추가 제한, raid는 거리 제한 없음 |
| search/explore | 미탐색 start 후보 또는 도달 가능한 frontier가 남아 있음 |
| raid detach | 해당 raid 슬롯이 비었고 main army가 6기 이상 |
| raid merge/retreat | 해당 raid 슬롯에 살아 있는 멤버가 있음. 공격·방어·사냥·search는 각 표적/위협/탐색 조건도 충족해야 함 |

생산 queue의 live cap은 4다. 확장은 `scout_berry`가 dark site를 밝힌 뒤에만 `expand_base_nest`가 열리는 2단계 chain이며, 두 액션은 site의 explored 상태에 따라 서로 배타적으로 열린다.

공간 액션은 별도 `target_mask[64]`를 쓴다. 맵을 8×8로 나누고 explored tile, 기억된 적 건물, 익명 start 후보가 속한 cell을 연다. 전부 0이면 8개 공간 액션도 닫힌다. 범위 밖·masked cell 또는 비공간 액션에 붙은 cell은 실행 직전 `-1`로 낮추며, `-1`이면 translator가 위치를 정한다. `attack_*_base`의 cell은 선호 공격 구역이지만 defend는 visible base-threat 중심 → cell → 가까운 base 순이라 정상 위협 상황에서는 cell이 흔히 사용되지 않는다.

Attack-commit lock도 mask 단계에서 적용된다. fighting group이 공격 목표와 target/march point를 가지고 아직 무기 접촉을 하지 않았으며 목표 나이가 2400 frame 미만이면, main은 attack 재지정·defend·hunt·search·hold/patrol을, raid는 해당 슬롯의 attack·defend·hunt·search·`merge_raid*`를 막는다. 유닛 합체 액션은 막지 않는다. `retreat`는 항상 탈출구로 남고 생산·연구·다른 그룹은 막지 않는다.

## 4. 전체 high-level action 80개

`ⓒ`는 8×8 target cell을 받을 수 있는 액션이다. C++ enum과 Python `ACTION_NAMES`의 순서는 동일하다.

| 범위 | index → action |
|---:|---|
| `0..13` | `0 no_op` · `1 produce_worker` · `2 produce_masos` · `3 produce_dilophos` · `4 build_population_nest` · `5 build_egg_nest` · `6 build_land_nest` · `7 expand_base_nest` · `8 scout_map` · `9 attack_nearest_enemy` · `10 attack_enemy_base ⓒ` · `11 defend_base ⓒ` · `12 retreat` · `13 hunt_neutral_monster` |
| `14..27` | `14 produce_unit_x22` · `15 build_nest_x86` · `16 build_nest_x87` · `17 produce_unit_x25` · `18 produce_unit_x27` · `19 produce_unit_x28` · `20 produce_unit_x2e` · `21 produce_unit_x2c` · `22 produce_unit_x29` · `23 produce_unit_x2a` · `24 build_nest_x83` · `25 build_nest_x88` · `26 build_nest_x89` · `27 build_nest_x8a` |
| `28..38` | `28 merge_twin_velocis` · `29 merge_twin_rhampos` · `30 merge_twin_pteras` · `31 merge_mutant` · `32 morph_enter_army` · `33 morph_exit_army` · `34 stance_on_army` · `35 stance_off_army` · `36 hold_army` · `37 patrol_defense` · `38 drop_attack` |
| `39..55` | `39 research_harvest` · `40 research_ground_attack` · `41 research_ground_defense` · `42 research_movement` · `43 research_air_attack` · `44 research_air_defense` · `45 research_mutant_merge` · `46 research_morph` · `47 research_haste` · `48 research_exp_down` · `49 research_melee_reinforce` · `50 research_triceps_speed` · `51 research_air_reinforce` · `52 search_enemy_base` · `53 scout_berry` · `54 explore_frontier` · `55 roam_scout` |
| `56..63` | `56 detach_raid` · `57 merge_raid` · `58 raid_attack_units` · `59 raid_attack_base ⓒ` · `60 raid_defend_base ⓒ` · `61 raid_retreat` · `62 raid_hunt_neutral` · `63 raid_search` |
| `64..71` | `64 detach_raid_b` · `65 merge_raid_b` · `66 raid_b_attack_units` · `67 raid_b_attack_base ⓒ` · `68 raid_b_defend_base ⓒ` · `69 raid_b_retreat` · `70 raid_b_hunt_neutral` · `71 raid_b_search` |
| `72..79` | `72 detach_raid_c` · `73 merge_raid_c` · `74 raid_c_attack_units` · `75 raid_c_attack_base ⓒ` · `76 raid_c_defend_base ⓒ` · `77 raid_c_retreat` · `78 raid_c_hunt_neutral` · `79 raid_c_search` |

### 4.1 액션이 실행기로 들어가는 방식

| 액션 | translator 결과 | 이후 동작 |
|---|---|---|
| `no_op` | 아무것도 바꾸지 않음 | 기존 group objective와 micro 계속 |
| 생산·건설·연구 | producer/builder와 type/order를 고른 즉시 semantic action | planner가 자원·인구·queue·선행조건·부지를 재검증 |
| 합체·변신·stance | 조건을 만족하는 unit을 결정론적으로 선택한 semantic action | 현재 translator가 최대 14기만 선택하며 합체는 정해진 arity 유지 |
| main `attack_*` | army에 `attack(units_first/buildings_first)` | 매 frame 표적과 march point 재계산 |
| main `defend/retreat/hunt/search` | army objective 변경 | 매 frame defend/이동/사냥/탐색 명령 생성 |
| `hold_army` | 현재 army 중심, 반경 128의 `defend` | 실제 엔진 hold 명령은 사용하지 않음 |
| `patrol_defense` | 현재 army 중심, 반경 320의 `defend` | 실제 엔진 patrol 명령은 사용하지 않음 |
| `scout_map` | scout 1기를 분리해 `scout` | 일반 맵 탐색이 아니라 알려진 적 기지 앞 조기경보 초소 |
| `scout_berry` | berry scout 1기를 다음 확장지에 배정 | 타일이 explored가 되면 원 기본 그룹으로 자동 복귀 |
| `explore_frontier` | explorer 1기 배정 | 미탐색 start 후보 우선, 이후 도달 가능한 frontier |
| `roam_scout` | roamer 1기 배정 | 현재 시야 밖의 도달 가능한 tile을 결정론적 난수로 순회 |
| raid detach/merge | main 일부를 raid A/B/C로 옮기거나 되돌림 | 각 raid가 독립 objective를 가짐 |
| raid 전투 액션 | 해당 raid에 main과 같은 attack/defend/retreat/hunt/search | main과 독립적으로 계속 실행 |
| `drop_attack` | 최대 4명 승선으로 시작 | 별도 runner가 `board → travel → unload → assault` 진행 |

생산/건설/연구는 현재 Tyrano catalog에 고정되어 있다.

- `0x80` base: worker `0x20`, unit `0x2c`, 연구 `0x14/0x2a/0x38/0x2b`
- `0x84` egg nest: `0x21/0x22/0x24/0x25/0x27/0x28/0x2e`
- `0x87`: `0x29/0x2a`
- `0x85`, `0x86`, `0x89`, `0x8a`: 각 action 이름에 대응하는 지상/공중/강화 연구

건설은 공통 `FindAiBuildSite`를 사용한다. 정적 유효성·탐색 여부·점유·국소 통로를 확인하고, 가능한 경우 footprint 주위 1-tile ring이 완전히 열린 후보를 우선한다. 가장 가까운 worker가 짓고, live 거부 시 일반 건물은 5회, 확장 base는 24회까지 같은 decision에서 spiral 재시도한다.

## 5. Micro executor의 상태 모델

### 5.1 그룹 9개

| 그룹 | 기본 멤버와 역할 |
|---|---|
| `economy` | worker. 상시 채집이 기본 |
| `army` | 신규 비-worker mobile의 기본 그룹. 그중 melee/ranged가 전투 수행 |
| `scout` | 알려진 적 기지 앞 조기경보 0~1기 |
| `berry_scout` | 다음 확장지 조명 0~1기 |
| `explorer` | start 후보와 frontier 탐색 0~1기 |
| `roamer` | 현재 시야 밖 순회 0~1기 |
| `raid`, `raid_b`, `raid_c` | main에서 분리된 독립 전투 그룹 3개 |

최대 전투 바디는 `army + raid + raid_b + raid_c` 네 개다. raid detach는 main 전투원의 빠른 순, 같은 속도면 ranged 우선, 이후 낮은 unit id 순으로 30%를 고른다. 최소 3기, 최대 14기, main의 절반 이하이며 새 raid는 현재 중심에서 반경 128 hold로 시작한다. 새로 생산된 전투원은 raid를 자동 보충하지 않고 항상 army에 들어간다.

역할은 공개 능력으로 유도한다.

```text
harvest capability → worker
transport capacity + no attack capability → transport
attack 불가 mobile → other
raw attack range 1..64 → melee
raw attack range 0 또는 64 초과 → ranged
type_id >= mobile limit → building
```

### 5.2 Objective 8개와 공격 tactic

| Objective | 의미 |
|---|---|
| `harvest` | idle worker를 berry에 분산 배정 |
| `attack` | tactic을 유지하며 현재 표적과 march point를 계속 재계산 |
| `defend` | post/건물 주위 bubble 안에서 싸우고 leash로 복귀 |
| `retreat` | 가까운 base로 교전 없이 이동, 도착하면 defend로 전환 |
| `scout` | 지정 지점으로 이동하고 적 전투원을 회피 |
| `search` | 미탐색 start 후보로 plain move, 자동 교전하지 않음 |
| `explore` | 도달 가능한 미탐색 후보/frontier를 1기가 탐색 |
| `roam` | 시야 밖의 도달 가능한 지점을 1기가 계속 순회 |

공격 tactic은 구체 unit id가 아니라 지속 의도다.

- `units_first`: 적 mobile 우선, 없으면 건물.
- `buildings_first`: 적 건물 우선, 없으면 mobile. 공간 액션의 cell은 선호 공격 구역이 된다.
- `neutral_only`: 전략 표적은 중립 몬스터다. 단, 사냥 중 사거리 안에 공격 가능한 적군이 있으면 그 적과 교전할 수 있다.

표적이 죽거나 시야에서 사라져도 executor가 같은 tactic으로 다음 표적을 고르므로 정책 의도가 한 번의 kill로 끝나지 않는다.

## 6. 매 frame micro 처리 순서

1. 살아 있는 자기 mobile, 완성 건물/base, visible hostile, visible neutral을 모아 id 순으로 정렬한다.
2. 죽은 unit record를 제거하고 신규 unit을 `economy` 또는 `army`에 등록한다.
3. berry 예약, 그룹 중심, 좌표별 median cohesion anchor, weapon target-class 합집합, attack wave별 anchor를 계산한다.
4. 본진 위협 reflex를 갱신한다.
5. 자원 소진/재등장, scout 완료, retreat 도착, attack target 소진, search/explore/roam 도착에 따른 자동 전이를 처리한다.
6. fighting group별 현재 표적·기억 건물 march point·중립 도달성을 다시 계산한다.
7. reflex가 덮어쓴 effective objective와 고기 collector를 정한다.
8. 각 unit의 desired semantic order를 계산한다.
9. 같은 명령은 억제하고 idle/stuck일 때만 재발행한다.
10. 같은 `(kind, target, point)` 명령을 최대 14기씩 묶어 planner에 넘긴다. Harvest는 tile 예약 보존을 위해 1기씩 보낸다.

### 6.1 유닛별 핵심 동작

| 대상 | 실제 로직 |
|---|---|
| Worker | visible combat threat가 자기 sight 안에 있고 실제 공격 가능한 경우 가까운 base로 도망간다. 위협이 sight×1.5 밖으로 사라지면 복귀한다. 완전 idle이면 explored berry를 base 기준 가까운 순으로 배정한다. tile당 3명 미만을 우선하지만 모두 차면 가장 가까운 tile에 3명을 넘겨 배정할 수 있다. |
| Attack fighter | 현재 유효 표적 유지 → tactic class → 낮은 HP → 가까운 거리 → 낮은 id 순으로 표적을 고른다. render class를 공격할 수 없는 표적은 제외하고 공중에는 별도 사거리를 쓴다. 근접은 기본 표적당 3기로 분산한다. |
| Attack march | 일반 공격은 사거리 안 표적이 없으면 적 위치·기억 건물·선호 cell로 `attack_move`한다. 사냥은 먼 neutral에도 `attack_unit`을 내린다. Search는 같은 이동점이라도 plain `move`라 자동 교전하지 않는다. |
| Defend fighter | 반경 800의 실제 방어는 모든 자기 완성 건물과 post를 anchor로 삼는다. bubble 안 적을 공격하고 radius 밖에서 복귀 상태에 들어가 radius 75% 안에서 해제한다. |
| Retreat fighter | group 중심에서 가장 가까운 base로 plain move하고 중심이 96px 안에 오면 그 base의 defend로 자동 전환한다. |
| Scout 계열 | 전투하지 않는다. 적 melee/ranged가 sight×1.5 안에 오면 반대 방향으로 sight/2만큼 이동해 회피한다. |

### 6.2 전투 안정화 규칙

- **저체력 철수:** ranged만 HP 30% 미만이면서 contact 상태면 가까운 base로 빠진다. contact가 풀리면 복귀하며 120 frame 재발동 cooldown을 둔다. Melee는 교전 중 등을 돌리지 않는다.
- **결속:** 목표에 더 가까운 선두가 같은 wave의 x/y median에서 320px보다 멀면 제자리에서 기다리고, 256px 안에서 해제한다. 이미 weapon contact 중이면 결속 gate를 끈다.
- **공격 wave:** 새 attack을 줄 때 현재 멤버가 첫 wave다. 이후 새로 생산·편입된 전투원은 base에서 defend하며 대기하고 6기가 모이면 별도 wave로 함께 출발한다. 이 규칙은 main과 raid A/B/C 각각에 적용된다.
- **도달 가능한 사냥:** ground fighter가 있으면 4-connect reachable 영역에 닿는 neutral만 고른다. 이때 main은 중심 1200px 안만, raid는 거리 제한 없이 사냥한다. 전원이 flyer면 per-frame executor의 도달성·1200px 제한이 모두 없지만, main 사냥 action을 처음 여는 legal mask에는 여전히 1200px 제한이 있다.
- **고기 회수:** attack/defend 중이며 contact와 policy hold가 아닌 pickup 가능 fighter를 drop당 1기 배정한다. 고기를 들지 않은 unit 우선, 이후 거리·id 순이다. 배정은 drop 소멸/claim/부적격까지 sticky하고, 사거리 안 전투 표적이 없을 때 800px 안 drop으로 이동한다.
- **중복 억제:** 원하는 명령이 바뀌지 않으면 다시 보내지 않는다. unit이 idle이거나 stuck일 때만 최소 4 frame 간격으로 재발행한다.
- **Stuck 복구:** 이동 명령 중 48 frame 동안 8px보다 덜 움직이고 목표가 96px 밖이며 contact가 아니면, unit id 기반 8방향으로 96px jitter를 주어 재시도한다.
- **Policy hold:** 정책이 직접 build/produce/merge 등을 맡긴 unit은 기본 16 frame 동안 micro가 재지시하지 않는다.

### 6.3 본진 방어 reflex

Reflex는 정책 objective를 바꾸지 않는 임시 overlay다.

```text
발동: 자기 완성 건물 800px 안에 visible enemy melee/ranged
      또는 완성 건물 총 HP가 전 frame보다 감소

가시 위협: power = health × (10 + attack_power)
           main에서 가까운 순으로 위협 power의 150% 이상, 최소 2기 파견

공격자 미관측 HP 감소: 최대 3기의 조사대 파견

raid: 중심이 위협 anchor 1600px 안이면 그 raid 전체 합류
해제: 위협이 120 frame 동안 사라짐
예외: policy retreat는 reflex가 덮어쓰지 않음
```

가시 위협의 평균 위치가 anchor이며, main 방어 detail은 overlay 동안 유지되고 필요할 때만 추가된다. 따라서 불필요한 전군 회군과 매 frame 담당자가 바뀌는 현상을 줄인다. 다만 위협이 강하거나 army가 작으면 main 전원이 선택될 수 있고, 반경 안 raid는 통째로 합류한다.

## 7. 실제 semantic 명령

Micro executor가 주로 만드는 명령은 다음과 같다.

| Semantic action | 엔진 command | 사용처 |
|---|---:|---|
| `move` | `0x04` | retreat, search, scout, defend 복귀, 결속 대기 |
| `attack_move` | `0x05` | 적 위치·기억 건물·선호 구역으로 교전 진군 |
| `attack_unit` | `0x05` + target id | visible 적 또는 neutral 집중 공격 |
| `harvest` | `0x07` | berry tile 채집 |
| `pickup_move` | `0x0d` | 고기 자동 pickup을 켜고 drop으로 이동 |
| `build` | `0x06` | high-level 직접 건설; building type과 좌표 전달 |

모든 semantic action은 각 액션 종류에 필요한 owner, 생존, capability, visibility, target, map, 자원, 선행조건을 live state로 다시 검증한 뒤 ordered gameplay packet이 된다. 공격 불가 unit의 공격 이동과 pickup 불가 unit의 pickup 이동은 planner가 일반 move로 낮춘다.

## 8. 정책 밖에서 함께 움직이는 규칙

실제 플레이를 읽을 때 다음 보조 계층을 정책 선택과 혼동하면 안 된다.

| 계층 | 현재 동작 |
|---|---|
| Macro autopilot | check cadence에 worker 10기 floor, population margin 2, bank 1500 이상에서 idle egg producer 보강, frame 1200 이후 적 기지 미발견 시 explorer 파견을 legal mask 안에서 보조 실행 |
| Tech guard | 첫 누락 tech 건물을 짓는 안전장치가 있으나 기본값은 OFF. Tech timing은 정책이 학습한다. |
| Defense reflex | 매 frame 위협을 보고 일부 army/가까운 raid에 임시 defend overlay 적용 |
| Drop runner | `drop_attack`이 시작한 승선·이동·하차·공격 sequence를 8-frame check cadence에 진행 |

Autopilot은 정책이 같은 producer를 쓴 frame에는 그 producer를 건드리지 않으며, 전투 시점과 army objective를 대신 고르지 않는다. Explorer 보조 파견과 economy 생존 규칙만 예외적으로 보탠다.

## 9. 현재 계약의 주의점

- Raw observation `schema v4`와 정책 feature `v10`은 서로 다른 버전 축이다. `BuildAiObservationV1`/`PlanAiSemanticActionV1`의 함수명도 현재 schema 번호를 뜻하지 않는다.
- IPC와 episode/imitation JSONL에는 raw observation이 아니라 feature와 mask만 저장된다. Episode에는 `target_mask`도 저장하지 않는다.
- IPC와 JSONL의 float는 소수점 이하 5자리로 직렬화된다.
- IPC JSON 자체에는 feature/action version 필드가 없으므로 길이와 checkpoint metadata가 맞아야 한다. 802/80 이전 checkpoint는 현재 계약과 호환되지 않는다.
- 현재 high-level translator는 Tyrano faction 전용이다. Replay에서는 명령을 주입하지 않고 imitation 관측만 하며, live P2P(`network_player_count > 1`)에서는 이 AI-play pump가 실행되지 않는다.
- Raid A/B/C feature는 `attack`과 `buildings_first`는 보지만 `units_first`와 `neutral_only`를 완전히 구분하지 못한다. Main army만 별도 `neutral_only` feature가 있다.
- C++ `target_mask`는 8개 공간 액션에 공통이다. Python PPO의 공격/방어별 semantic cell refinement는 현재 main과 raid A에만 적용되고 raid B/C는 공통 mask를 그대로 쓴다.
- Feature encoder의 일부 role 집계는 effective `attack_range`를 쓰지만 micro role은 raw `attack_range_base`를 쓴다. 사거리 upgrade 경계에서는 두 분류가 다를 수 있다.
- `HashAiObservationV1`은 연구·시작 후보·executor 요약·fog memory·map effect 등 여러 후첨 필드를 포함하지 않는 부분 hash다. 전체 raw 관측 동일성 검사용으로 쓰면 안 된다.

## 10. 코드 기준점

- Raw 관측 계약: [`ranker_ai_observation.h`](../include/ranker_ai_observation.h), [`ranker_ai_observation.cpp`](../src/ranker_ai_observation.cpp)
- Feature·action·mask: [`ranker_ai_rl_features.h`](../include/ranker_ai_rl_features.h), [`ranker_ai_rl_features.cpp`](../src/ranker_ai_rl_features.cpp)
- Decision gate·autopilot: [`ranker_ai_decision_gate.h`](../include/ranker_ai_decision_gate.h), [`ranker_ai_autopilot.h`](../include/ranker_ai_autopilot.h)
- High-level translator: [`ranker_ai_scripted_bot.cpp`](../src/ranker_ai_scripted_bot.cpp)
- Micro executor: [`ranker_ai_micro_executor.h`](../include/ranker_ai_micro_executor.h), [`ranker_ai_micro_executor.cpp`](../src/ranker_ai_micro_executor.cpp)
- Semantic action 검증·packet화: [`ranker_ai_actions.cpp`](../src/ranker_ai_actions.cpp)
- Live pump 연결: [`ranker_winmain.cpp`](../src/ranker_winmain.cpp)
- Python action 계약·PPO: [`ranker_rl_env.py`](../tools/ai/ranker_rl_env.py), [`ranker_ppo.py`](../tools/ai/ranker_ppo.py)
- 후속 entity-command 직접 제어 설계: [`AI_PLAY_ENTITY_COMMAND_RL_PLAN.md`](AI_PLAY_ENTITY_COMMAND_RL_PLAN.md)
