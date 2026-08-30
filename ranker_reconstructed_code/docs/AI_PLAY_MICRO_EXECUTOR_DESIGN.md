# AI Play — Micro Executor (부대 목표 기반 유닛 실행기) 설계

작성: 2026-08-29. 사용자와의 대화형 설계 세션에서 합의된 내용을 그대로 기록한다.

## 1. 원칙

- RL 정책은 **부대(group)의 목표(objective)** 만 바꾼다. 8프레임마다 한 번.
- 마이크로 실행기는 **매 프레임** 돌며, 각 유닛에 대해
  `f(부대 목표, 유닛 자신의 상황, 주변 적) → 기본 동작` 을 계산한다. 선택이 아니라
  상태에 따라 답이 하나로 정해지는 동작이다.
- 실행기는 새 목표를 지어내지 않는다 ("언제·무엇을"은 정책, "어떻게"만 실행기).
- 명령은 **변화가 있을 때만** 발행한다 (같은 유닛에 같은 명령을 매 프레임 다시 내면
  공격 준비 동작이 리셋될 수 있음). 같은 (종류, 대상) 재발행 최소 간격 4프레임.
- 정책은 실행기의 존재를 모른다 (켜고 끄는 스위치 없음). 목표가 retreat 이면 실행기는
  점사할 이유가 없어 안 할 뿐이다.

## 2. 부대와 유닛

```text
부대(group): economy(일꾼) / army(전투유닛) / scout(정찰 0~1기)
    objective { kind, target_unit_id | target_x/y, radius, set_frame }

유닛 레코드: group_id, micro_state(normal|pulling_back|fleeing|evading),
            micro_since_frame, last_issued{kind,target,x,y,frame},
            policy_hold_until_frame
```

- 유닛은 목표를 갖지 않는다. 해야 할 일 = micro_state 가 normal 이 아니면 임시 동작,
  아니면 소속 부대의 목표. 저장하지 않고 매 프레임 계산.
- 생산 직후 기본 소속: 일꾼 → economy(harvest), 전투유닛 → army(defend, post = 가장
  가까운 티라노 네스트). 목표 없는 유닛은 존재하지 않는다.
- 유닛 역할은 데이터에서 유도: worker = 채집 명령 비트, transport = transport_capacity>0,
  melee = 공격 사거리 ≤ 임계, ranged = 그 이상. 사용자 검증 목록: 원거리 = 딜로포스,
  람포스, 트윈 람포스, 프테라스, 트윈 프테라스, 트리세스, 에그 스로워, 켄트로스, 뮤턴트.
  근접 = 마소스, 벨로시스, 트윈 벨로시스, 티라노스.

## 3. 목표(상태) 5종

| 목표 | 내용 |
|---|---|
| harvest | 놀면: 일꾼에서 가장 가까운 네스트 → 그 네스트에서 가장 가까운 베리. 포화 무시. 공격받으면 임시 `fleeing`(가장 가까운 네스트로), 위협 사라지면 재개. **베리가 하나도 없으면 economy 부대는 defend 로 전이.** |
| attack | **전술(tactic)** 을 가진다 — 값이 아니라 의도(2026-08-30, v6). 정책은 **건물 우선 / 유닛 우선**만 고르고, 실행기가 **매 프레임** 그 우선순위로 (a) 행군 목적지와 (b) 사거리 안 점사 순서를 정한다. 행군 = 알려진 적 위치로 **attack_move** (이름을 찍어 추격하지 않음; 가는 길에 만나는 적은 엔진이 교전). 목적지 우선순위: `buildings_first` = 보이는 적 건물 → **기억된 적 건물**(관측 fog 메모리) → 보이는 적 유닛 / `units_first` = 보이는 적 유닛 → 보이는 건물 → 기억된 건물 / `neutral_only` = 중립 몬스터를 이름으로 `attack_unit`. 사거리 안 점사: 전술의 클래스 먼저, 그 안에서 최저체력, 근접 타깃당 최대 3기. **대상 사망 → defend 로 바꾸지 않음**: 같은 전술로 다음 목적지, 아무것도 모르면 제자리 대기(8프레임 안에 정책이 다시 고름). |
| search | (v6) 적 건물 위치를 **모를 때** 부대 전체로 맵을 쓸기: 부대 중심에서 가장 가까운 **미탐색 적 시작 후보** → 탐색 프런티어(미탐색·통행가능·탐색된 이웃) → 맵 전부 탐색이면 중앙/모서리 순환. 스윕 지점은 실행기가 정하고, 도착하거나 그 타일이 밝혀지면 재선택(최소 8프레임 간격). **`move` 로만 이동하고 교전하지 않는다** — 스윕이 드러낸 적과 싸울지는 정책이 다음 결정에서 attack 으로 바꿔 정한다. 프런티어 = 미탐색 타일 중 탐색된 타일과 상하좌우로 맞닿은 타일(안개 경계선). |
| defend | post(네스트) + 반경(화면 1개 = 800px). 발동 = 적이 **공유 시야**에 보이고 **어떤 아군 건물(v6: 네스트뿐 아니라 모든 완성 건물)의 반경 안**에 있을 때. 공유 시야에 보여도 수비 범위 밖이면 출동하지 않는다. 추격 한계 = 그 건물 반경, 벗어나면 post 로 복귀(75% 안으로 들어와야 해제). defend 유닛 전원 출동. 일꾼도 같은 규칙. |
| retreat | 부대 무게중심에서 가장 가까운 네스트로 전원 이동, 교전 안 함. 도착하면 defend(그 네스트). |
| scout | (v6 재정의) **전초 감시병(picket)** — 적 건물을 찾는 유닛이 아니라 적의 공격이 오는지를 빨리 알아채는 유닛. post 는 실행기가 **매 프레임** 계산: 내 본진(가장 가까운 네스트) → 가장 가까운 **알려진** 적 건물(보임 ∨ 기억) 선상의 앞쪽 66% 지점, 단 적 건물에서 800px(한 화면) 이상 떨어지게. post 에 도착하면 그대로 대기(돌아다니지 않음), 적 이동유닛이 시야×1.5 안에 보이면 반대 방향으로 시야/2 씩 물러나고, 사라지면 post 로 복귀. 교전 없음. 마스크: 적 건물을 아는 상태에서만. |

접촉 시 공통 규칙(attack/defend): 점사 = 체력 낮은 적 우선; hp < 30% 유닛은
`pulling_back`(가장 가까운 네스트 방향), 접촉이 끊기면 복귀.

## 4. 정책 action → 부대 목표

```text
search_enemy_base       → army = search            [마스크: 부대 ∧ 적 건물 위치 미확인]   (v6 신규)
attack_enemy_base       → army = attack(buildings_first) [마스크: 부대 ∧ (적 보임 ∨ 건물 기억)]
attack_nearest_enemy    → army = attack(units_first)     [마스크: 같음 — 두 액션의 차이는 우선순위뿐]
hunt_neutral_monster    → army = attack(neutral_only)    [마스크: 부대 ∧ 중립 보임]
  — 정책은 전략(찾기/공격/방어)과 전술(무엇을 먼저 때릴지)만 고른다. 첫 타깃 id 는 넘기지 않는다;
    실행기가 매 프레임 전술로 부대 타깃을 다시 고르므로 정책의 선택이 첫 킬 뒤에도 유지된다.
    search ↔ attack_enemy_base 는 "적 건물을 아는가"로 서로 배타적으로 마스킹된다.
defend_base             → army = defend(post = 가장 가까운 네스트, 반경 800, 앵커 = 모든 아군 건물)
hold_army               → army = defend(post = 현재 무게중심, 반경 작음)
patrol_defense          → army = defend(post = 현재 무게중심, 반경 중간)
retreat                 → army = retreat
scout_map               → scout = scout (감시병 1기 분리; post 는 실행기가 본진↔알려진 적 건물 사이에서 매 프레임 계산) [마스크: 유닛 존재 ∧ 적 건물 앎]
harvest_saturate        → (v4 에서 액션 제거: 실행기가 매 프레임 노는 일꾼을 채우므로 할 일이 없었음)
drop_attack             → 기존 드롭 상태기계 유지
build_* / produce_* / research_* / morph / stance / merge
                        → 목표 변경 없음, 즉시 명령 (build 는 일꾼 1기 임시 override)
```

## 5. 기존 오토파일럿 처분

```text
IdleWorkerHarvest  → harvest 실행기로 흡수
DefenseAutopilot   → defend 규칙으로 흡수 (주력이 attack 중일 땐 기지 방어에 돌아오지 않음 — 정책의 defend_base 몫)
OffenseAutopilot   → 삭제 ("언제 공격할지"는 정책이 배울 것; 대치가 재발하면 보상으로 풀지 실행기로 덮지 않는다)
DropAttackAutopilot→ 유지
```

## 6. 데이터

관측 스키마 v3 (`kAiObservationSchemaVersion = 3`).

- `AiObservedUnit`: `attack_range`, `attack_range_vs_air`, `attack_range_base`,
  `sight_range`, `transport_capacity`, `render_class`, `attackable_class_mask`.
- **공격 클래스 게이트**: 엔진은 모든 공격을 두 번 막는다.
  `default_unit_action_profile_allows_target_render_class` 가 공격자의 데미지
  프로필(`action_profile_index`)의 `allowed_target_render_class_mask` 와 대상의
  `render_class` 를 비교한다. 마스크 밖 대상에 내린 공격 명령은 **조용히 거부되고
  유닛은 idle 이 된다** — 그러면 실행기의 idle 재발행 규칙이 같은 불가능한 명령을
  영원히 다시 보낸다(부대 전체가 "공격 중"인데 아무것도 안 함).
  헤드리스 실측: 일꾼(type 0x20)의 마스크는 `0x00000007` 로 **클래스 3(비행)을
  포함하지 않는다** — 마스크는 관대한 기본값이 아니라 실제 제약이다.
  → `pick_target` / `nearest_visible_target` / army 목표 유효성 모두 이 게이트를 통과한다.
- **유효 사거리**: 실제 사거리는 `action_range_base` 가 아니라
  `CalculateUnitActionRangeWithProductionAndEquipmentEffects` 다 (variant 보너스 +
  연구 완료 효과 + 장비 보정). 비행 대상에는 `action_range_base_vs_class3` 라는
  **다른 스탯**을 읽는다. 헤드리스 실측: 일꾼 range=50 인데 air=35.
  연구·장비는 사적 정보이므로 **controlled 유닛에만** 적용하고, 보이는 적은 공개
  기본 스탯을 유지한다(`AiUnitCombatProfileCallback`, winmain 의
  `default_ai_play_unit_combat_profile`).
- 근접/원거리 분류는 업그레이드가 붙지 않는 `attack_range_base` 로 한다. 유효
  사거리로 하면 사거리 연구 한 번에 근접 유닛이 원거리로 재분류된다.
- 감사된 티라노 사거리(px, `ai_techtree_audit.txt` range=): 근접 4종(마소스/벨로시스/
  트윈 벨로시스/티라노스)과 일꾼 = 50, 원거리 = 딜로포스 230, 람포스 250, 프테라스 170,
  트윈 프테라스 310, 트리세스 230, 에그 스로워 430, 켄트로스 170, 뮤턴트 250,
  트윈 람포스 0(정의에 사거리 없음 → 공격 가능 + 사거리 0 은 원거리로 취급). 임계값 64.
- 공격 쿨타임/DPS 는 아직 관측에 없음 → 카이팅·오버킬 방지·위협 기반 점사는 후속 작업.
  정의에는 이미 있다: `action_cycle_ticks`, `action_recovery_base_ticks`,
  `action_impact_frames`, 데미지 프로필, `runtime_stat_1c`/`stat_20`.
- 범위 공격 플래그는 effect 정의에 있어 유닛 정의와의 링크를 아직 찾지 못함 → 근접
  타깃 분산(최대 3기)은 항상 적용.

## 6-1. 실행기 기본 동작 보강 (2026-08-30)

| # | 항목 | 내용 |
|---|---|---|
| 1 | 공격 클래스 게이트 | 위 §6. 공격 불가 대상은 후보에서 제외하고, 정책이 그런 대상을 지정하면 부대가 칠 수 있는 적으로 재타겟한다. |
| 2 | 유효 사거리 | 위 §6. 접촉 판정·표적 도달 판정 모두 대상 클래스별 유효 사거리를 쓴다. |
| 4 | 축차 투입 방지 | attack 목표에서 그룹 무게중심보다 `cohesion_radius`(256px) 이상 떨어졌고 목표에 더 가까운("앞선") 유닛은 무게중심으로 돌아가 합류한다. 뒤처진 유닛은 계속 전진하므로 무게중심이 앞으로 이동해 선두가 풀린다 — 교착 불가. 접촉 중이면 게이트 해제. |
| 5 | 히스테리시스 | (a) defend leash: 이탈은 `radius`, 복귀 종료는 `radius * leash_return_percent/100`. 새 상태 `returning`. (b) 저체력 후퇴는 **원거리만** (근접은 등을 보이면 무료 피격), 그리고 후퇴 해제 후 `pullback_cooldown_frames`(120) 동안 재진입 금지 — 이전에는 나갔다 들어왔다를 무한 반복했다. |
| 6 | 스턱 복구 | 재발행 조건이 `unit_is_idle` 뿐이라, 엔진이 non-idle 상태에서 명령을 흘리면 유닛이 영원히 멈춘다. 이동/채집 명령 하에서 `stuck_frames`(48) 동안 `stuck_move_epsilon` 이상 움직이지 않고 목적지에서 멀며 비접촉이면 재발행하고, 유닛 id 로 결정되는 방향 오프셋을 시도마다 회전시켜 붙인다. 헤드리스 10895프레임 실측 stuck=85 — 실제로 멈추고 있었다. |
| 7 | 채집 분산 | 베리 타일별 배정 수를 세어 `workers_per_resource_tile`(3) 을 넘으면 다음 가까운 타일로 보낸다. 배정은 `AiMicroUnitRecord::assigned_resource_tile` 에 남아 왕복 중에도 유지되고, 정책이 유닛을 가져가면(`AiMicroHoldUnits`) 반납된다. |
| 8 | 정찰 스윕 | 정책의 post 에 도착한 뒤 서 있지 않고 계속 탐색한다: 미탐색 시작 후보(`start_candidate_mask`, 자기 시작점 제외) 우선, 없으면 탐색된 통행 가능 타일에 인접한 미탐색 통행 가능 타일(frontier) 중 최근접. 재선택은 `scout_repick_interval_frames`(8) 로 제한. post 이동은 여전히 정책의 몫 — 실행기는 "어떻게"만 채운다. |

## 6-2. 헤드리스 처리량 작업: 시도 후 전량 롤백 (2026-08-30)

프레임당 비용을 줄이려고 네 가지를 구현했다가 **전부 되돌렸다.** 코드는 남아 있지
않으며, 이 절은 같은 길을 다시 파지 않기 위한 기록이다.

시도한 것:

1. **관측 저장소 재사용 + 타일 갱신 스로틀** — `BuildAiObservationV1Into` 로 owner 별
   영속 `AiObservation` 에 in-place 채우기, fog/자원 패스를 8프레임마다만 실행.
2. **자원 타일 캐시** — 실행기의 전체 맵 스캔을 16프레임마다로. `map_scans` 10001 -> 626.
3. **적 공간 격자** — CSR 256px 그리드로 근접 질의를 셀 단위로. defend 버블/앵커
   계산을 유닛당 -> 부대당 1회로 hoist.
4. **명령 발행 비용** — 플래너 유닛 id 이분 탐색 인덱스, harvest 배칭.

**측정 결과 (128x128, 활성 유닛 206, 10895프레임 소멸 종료, `-AISELF -AIRANDOM -SEED:12345`)**

`-MAXFRAMES` 를 바꿔가며 잰 기울기: 시작 오버헤드 약 5.0초 + **2.50 ms/frame**(약 400 fps).
`QueryPerformanceCounter` 로 AI 펌프를 직접 계측한 결과:

- AI 펌프 전체: **0.32~0.43 ms** = 프레임의 **13~17%**
- 관측 빌드: **0.065~0.093 ms** = 프레임의 **3%**
- 나머지 **83~87% 는 게임 시뮬레이션**

즉 AI 쪽을 0으로 만들어도 상한이 13~17% 다. 네 항목을 켜고 끈 벽시계 비교는
29.9~34.0 초로 구분되지 않았고, 슬라이스 단위 차이도 크로스 세션 드리프트와 분리되지
않았다(같은 빌드 반복은 +-5%, 세션이 바뀌면 그보다 큰 폭). 결정적 근거: 4번은 관측
빌드를 건드리지 않는데도 `obs_ms` 가 0.065 <-> 0.093 으로 벌어졌다.

**전제가 틀렸다.** "실행기가 매 프레임 도니 그게 학습 속도를 정한다" 는 가정으로 시작한
작업인데, AI 는 프레임의 1/6 이하다.

**다음에 볼 곳**: 처리량을 실제로 올리려면 시뮬레이션 루프(프레임당 약 2.1 ms) 를 먼저
계측해야 한다. 1998년작 RTS 가 128x128 맵/유닛 206기에서 2.1 ms 를 쓰는 것은 재구축
과정에서 원본보다 무거워진 지점이 있다는 신호일 수 있다. AI 쪽 최적화는 그 뒤에,
세션 내 A/B 로 재면서 볼 일이다.

부수 확인: 성능 작업 유무와 무관하게 실행기 동작은 완전히 동일했다
(`orders=10203 stuck=85`, 소멸 종료 프레임 10895).

## 7. 확장 (expand) — v7 (2026-08-30)

원칙: 정책이 "언제", 실행기/번역기는 "어디·누구"만 계산. 상태 없이 관측에서 매번 계산.

**안개 규칙**
- 베리 **위치·초기량은 맵 데이터로 공개**: `resource_memory` 를 맵 초기 자원량으로 seed
  (`ranker_ai_observation.cpp`). 미관측 타일도 초기량을 보고한다. **잔량**은 본 것만 갱신.
- **건설·채집 명령은 탐색된 타일만** (`ranker_ai_actions.cpp` build 분기에 explored 게이트 추가,
  채집과 동일). 엔진은 안개를 보지 않고 **도착·착공 때** 비용을 차감한다
  (`complete_legacy_spawn_placement` → `reserve_building_primary_resource`).

**예약 회계** (`ranker_ai_rl_features.cpp`)
```
reserved = Σ 일꾼 u (command_state ∈ {0x23 배치 시작, 0x25 접근}) : cost(type(command_value))
type(v) = v < 0x60 ? v + 0x60 : v        (0x23 첫 틱은 건물 index, 0x25 부터 type id)
마스크·feature 의 자원 = primary − reserved.   0x24(건설 중)는 이미 차감이라 제외.
```

**계산** (`ranker_ai_expansion.{h,cpp}`, `ComputeAiExpansionPlan`)
- 군집 = `amount>0` 타일의 8방향 연결 성분 (관측의 알려진 잔량 기준 — 다 캔 군집은 사라진다).
- 군집당 자리 1개 = `argmin_t Σ amount(b)·dist(t,b)`, t ∈ 경계상자 ±4타일, 통행 가능·베리 아님·
  베리와 1칸 이상. tie: 시작점에 가까운 쪽. 적 건물 거리 제약 없음(안전은 정책 몫).
- 개발됨 = 자리 512px 안에 내 네스트(완성·건설 중·**네스트를 지으러 가는 일꾼의 path_target**).
- 후보 = 미개발 군집 중 시작점 최근접. 그 자리 타일의 explored 가 "밝음".

**액션**
| 액션 | 마스크 | 효과 |
|---|---|---|
| `scout_berry` (#53) | 유닛 존재 ∧ 후보 자리가 어두움 | `berry_scout` 그룹 1기(마소스 → 전투유닛 → 일꾼, 감시병 제외). post = 후보 자리. `move`, 회피, 무교전. 자리 타일이 탐색되면 실행기가 그룹을 해제(기본 전이) |
| `expand_base_nest` | 일꾼 ∧ (자원−예약) ≥1000 ∧ 후보 자리가 밝음 ∧ 네스트를 지으러 가는 일꾼 없음 | 자리에서 가장 가까운 일꾼이 그 자리에 build. 배치 거부 재시도(5회)는 **자리 중심** 나선 |

두 마스크는 후보 자리의 밝음/어두움으로 갈라져 겹치지 않는다. 어느 쪽도 fallback 이 없다.

**계약**: 액션 54, feature 541 (534–540: 후보 자리 dx·dy·거리·밝음, 베리 정찰병 존재,
예약 자원 /1000, 걸어가는 건설 수 /5). 관측 `berry_scout_unit_id` 추가, `resource_amount` 의미 변경.

**기각**: `expand_pending` 대기 상태기계 / expand→scout_berry 대체 / 본진 옆 fallback /
미탐색 건설 허용 / 엔진 차감 시점 변경 / 후보 자리 적 거리 제약.

**관측 `passable` 잠복 버그 수정 (v7, 헤드리스 실측으로 발견)**: 관측은 `terrain == 0x100`
(`kMapCellPassableTerrain`) 만 통행 가능으로 잡았는데, 엔진에서 0x100 은 **베리(채집) 지형**이고
걸을 수 있는 맨땅은 `terrain == 0` (`legacy_movement_class_can_enter_cell` 의 terrain_clear) 이다.
실제 맵에서 passable 이 16384 타일 중 100(= 베리 타일)뿐이었다 → search 프런티어·확장 자리
후보가 전부 비었다. 수정: `passable = terrain ∈ {0, 0x100} ∧ !blocked` (15835/16384),
새 필드 `buildable = terrain == 0 ∧ alternate_flags & 0x80000000(배치 허용) ∧ !blocked`
(엔진 배치 규칙의 정적 부분). 확장 자리 후보는 `buildable` 을 쓴다.

**관측 `explored` 비영속 버그 수정 (v7, 헤드리스 실측)**: 엔진의 owner 별 explored 비트는 AI owner 에
대해 영속적이지 않다 — 정찰병이 밝힌 타일이 떠나면 다시 어두워졌다(`lit` 가 0↔1 로 진동, scout_berry
가 910회 반복). 펌프에 owner 별 탐색 기억(`ai_play_explored_memory`)을 두어 활성 시야를 누적하고
그것을 `explored` 로 보고한다(사람 플레이어의 안개 기억과 동일, 단조 증가). 건설/채집 게이트도 같은
벡터를 쓴다. 네스트 발자국은 definition 0x80 기준 **6×4 타일** — 후보 자리는 발자국 전체가
buildable·베리 없음·같은 placement_class 이고 둘레 1칸에 베리가 없어야 한다.

**본진 계열 건물의 베리 여유 규칙 (엔진, 헤드리스 실측으로 확인)**: 배치 게이트
(`CheckPreviewProductionPlacementGateCell`, allow_nearby_probe = 0x60/0x70/0x80/0x90)는 발자국의
**모든 칸에서 ±4타일 안에 베리 지형(0x100)이 있으면 거부**한다(`find_nearby_passable_placement_tile`).
따라서 확장 자리 후보는 발자국(6×4)을 4칸 키운 사각형 안에 베리가 없어야 하고, 탐색 상자는 군집
경계상자 ±12타일이다. 거리합 최소 규칙은 이 제약 안에서 가장 가까운 자리를 고른다.

**검증 (2026-08-30, `tools/ai/ranker_expand_probe.py`, seed 1, 20000f)**: 고정 정책(scout_berry → expand
우선, 일꾼 6기까지만 생산)으로 체인이 끝까지 돈다 — 프레임 1472 자원 1000 도달 → expand → 일꾼 이동
(inflight=1, expand 마스크 닫힘, 그 군집은 "개발됨"이 되어 scout_berry 가 다음 자리로 열림) → 1944 착공
(nests=2) → 6000 에 네스트 5개, 배치 거부 0회. 이후 적에게 파괴되어 소멸(정책에 군대가 없음 — 의도된
검증 범위 밖). 알려진 잡음: build 명령 후 일꾼이 0x23 에 들어가기까지 ~15 결정 동안 expand 가 열린 채로
남아 같은 명령이 반복된다(무해하지만 RL 에겐 노이즈) — 관측에 "발행됐지만 아직 걷지 않는 건설" 을
노출하면 해결된다(후속).

**정정 + 자리 막힘 규칙 (2026-08-30, 로그로 확인)**: 앞 절의 "build 명령 후 ~15 결정 지연"의 원인은 엔진이
아니었다. 로그: 1472f 채집 중(0x2a) 일꾼에게 build → **1473f 즉시 0x25(이동)**. 지연의 실체는 **발자국
위에 선 중립 몬스터**(type 0x49, owner 8) — 도착 시 엔진 배치 실패 → idle → 채집 복귀, 이후 플래너가
매 결정 거부(code 26, `units_on_footprint=1`)하는데 마스크는 "밝음·걷는 일꾼 없음"이라 expand 가 열린
채 반복됐다. 엔진 게이트는 발자국 위 유닛(모든 owner)을 거부한다(route 충돌 플래그). 수정:
`AiExpansionPlan.target_blocked` (보이는 유닛이 발자국 타일 위에 있음) → expand 마스크 닫힘, feature
541 `expand_site_blocked` (feature 542개). 정책은 `hunt_neutral_monster` 로 치운 뒤 확장하면 된다.
남는 경우: 안개 속(보이지 않는) 유닛이 발자국 위에 있으면 관측은 알 수 없으므로 마스크가 열리고
플래너/엔진이 거부한다(엔진 게이트 `preview_collision_visibility_gate` 는 일반 유닛을 안개와 무관하게
센다 — FUN_004d6cb0, 사람 플레이어도 같은 거부를 받는다). 재검증(seed 1): 첫 확장이 2000f 전에
착공, 숨은 몬스터 때문에 거부된 expand 선택 7회(이전 14회) — 안개 규칙상 남는 비용으로 둔다.

**공통 배치 규칙 (v7, 2026-08-30)** — 확장에만 있던 자리 조건을 모든 건설로 확장했다. 무작위 정책 로그에서
인구/에그 네스트도 `plan failed code=26/17` 을 반복하고 있었다(마스크는 열렸는데 나선 점이 미탐색이거나
발자국이 안 맞음). `FindAiBuildSite(type, center=시작점, radius=16타일)`: 발자국(타입별 실측 표 —
0x80 6×4, 0x82 3×2, 0x83 2×2, 0x84 4×2, 0x85/86/89/8a 4×3, 0x87/88 5×3, 0x8f 4×3) 전체가
buildable ∧ 같은 placement_class ∧ 탐색됨 ∧ 베리 없음, 보이는 유닛이 위에 없음, 본진 계열은 베리 ±4.
`build_*` 마스크는 "자리 있음"을 추가로 요구하고, 번역기는 같은 자리에 짓는다(가장 가까운 일꾼, 자리 중심
나선 재시도). 확장의 군집 자리 계산도 같은 판정 함수(`AiBuildSiteCandidateOk`)를 쓴다. 계약 변동 없음.

## 8. 탐색 분할 — search / explore / roam (v7, 2026-08-30)

`search_enemy_base` 를 목적별 세 액션으로 나눴다. 각각 할 일이 끝나면 마스크가 닫힌다.

| 액션 | 누가 | 목표 | 마스크 | 끝 |
|---|---|---|---|---|
| `search_enemy_base` (#52) | army 전체 | **미탐색 시작 후보**만(부대 중심에서 가까운 순), `move`, 무교전 | 부대 ∧ 미탐색 시작 후보 있음 | 후보가 다 밝혀지면 부대는 서고 마스크 닫힘 |
| `explore_frontier` (#54) | 1기 (`explorer` 그룹): **공중 유닛 우선**, 다음 최고 속도(`movement_step_limit/movement_period`), 일꾼은 최후 | 유닛이 **도달 가능한**(지상: 통행 타일 4-연결 flood fill, 공중: 전부) 프런티어 중 최근접, `move`, 회피 | 유닛 존재 ∧ 프런티어 있음 | 도달 가능한 프런티어가 없으면 실행기가 그룹 해제(기본 전이) |
| `roam_scout` (#55) | 1기 (`roamer` 그룹): 공중 → 최고 속도 | **활성 시야 밖**의 도달 가능한 통행 타일을 **무작위**(결정적 xorshift32)로 골라 이동, 도착하면 다시 선택, 회피 | 유닛 존재 | 끝 없음(정책이 다른 데 쓰기 전까지) |

- 관측: `movement_step_limit`, `movement_period`(공개 타입 스탯), `explorer_unit_id`, `roamer_unit_id`.
- feature 542 `explorer_alive`, 543 `roamer_alive` → feature 544개, 액션 56개.
- 이전 search 의 "프런티어 → 순환점" 부분은 제거(explore 가 대체). 감시병(scout_map)·베리 정찰(scout_berry)과 역할이 겹치는 유닛은 선택에서 제외.

**`passable` 를 엔진 판정에 맞춤 (v7, 사용자 결정)**: `legacy_movement_class_can_enter_cell` 의 이동 클래스 0
정적 규칙 — `지형 클래스 == 0 ∧ alternate_flags & 0x20000000 ∧ alternate_flags & 0x60000000`. 베리 타일
(0x100)은 **통행 불가**(채집 명령의 shortcut 으로만 진입) → 채집 후보는 `resource_amount` 로만 잡는다
(실행기 `gather_resource_tiles`, 인코더 자원 격자에서 passable 조건 제거). 건물 발자국 점유
(`visibility_flags & 0x20000000`)는 동적이라 넣지 않고 엔진 경로 탐색·stuck 복구에 맡긴다. 공중은 전부 도달.

**학습 시작 중 발견 (2026-08-30, PPO 롤아웃 로그)**: expand 발행 759회 중 666회가 플래너 거부 —
(1) 발자국 ±4타일 안의 **잔량 0 베리 지형**(엔진 probe 는 지형 클래스 0x100 을 보지 잔량을 보지 않음) →
계획의 베리 여유 판정을 `terrain_flags & 0x700 == 0x100` 기준으로 변경(군집은 여전히 잔량>0 기준).
(2) 인구/에그 네스트 거부 = 기존 건물 **발자국**과 겹침(계획은 유닛 중심 타일만 점유로 봤음) →
`AiBuildOccupancyGrid`: 보이는 건물의 발자국 전체(표 없는 타입 4×3)를 점유 격자로 만들어 모든 후보 판정에
반영. 플래너 첫 시도 거부는 `ai-expand: site rejected` 로 따로 로그(재시도 나선의 거부와 구분).
(3) 인구 네스트 반복 거부의 실체 = **걸어가는 중인 건설의 예약 자리**(엔진 TemporaryBlock) → 점유 격자에
walking builder 의 목적지 발자국(`AiBuildingInteractionOf` 로 path_target → 앵커 복원, ±1타일 여유)을 포함.
(4) 확장 첫 시도 거부 84회 중 40회 = **안개 속 유닛**(적 0x21·중립 0x49)이 자리 위 — 관측으로 알 수 없으므로
"거부당했다"는 사실 자체를 관측에 노출: `last_build_reject_type / _frames_ago`(펌프가 플래너 최종 실패 시 기록)
→ 그 타입 건설 마스크를 64프레임 닫고 feature 544 `build_recently_rejected` 로 보고(feature 545개).
사람도 "여기 못 지음" 메시지를 받으므로 안개 규칙에 어긋나지 않는다.
