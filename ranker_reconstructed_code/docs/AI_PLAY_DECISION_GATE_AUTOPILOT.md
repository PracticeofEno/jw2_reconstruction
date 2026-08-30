# 이벤트 기반 결정 게이트 + 방어 반사 + 매크로 오토파일럿 (v9)

관련: `docs/AI_PLAY_RL_STRUCTURE.md`(계층 구조), `docs/AI_PLAY_MICRO_EXECUTOR_DESIGN.md`(실행기),
`docs/1순위.md~3순위.md`(v8 계약). 구현: `ranker_ai_decision_gate.{h,cpp}`,
`ranker_ai_autopilot.{h,cpp}`, `ranker_ai_micro_executor.*`(threat 오버레이),
`ranker_winmain.cpp`(펌프/모방 로거), `ranker_p2p_lobby.*`(플래그).

## 1. 왜 (실측 근거)

v8 파이프라인의 학습 퇴행 원인 두 가지가 실측으로 확인됐다:

1. **8프레임 무조건 결정**: 모방 데이터셋 18838샘플 중 no_op 73%,
   BC 정책의 라이브 결정 1911개 중 no_op 84%. 같은 상태에서 매 8프레임
   독립 재샘플링 → 부대 목표 요동(자가대전에서 hunt/attack/retreat가 8프레임
   주기로 뒤바뀜). 에피소드 5000~10000스텝 → γ=0.9998이 필요해진 원인.
2. **방어/매크로 반사 부재**: 러시를 받으면 정책이 수천 게임 동안 방어를
   배우기 전에 전멸(인구 8 정체, 10k~19k 프레임 섬멸). 내장 AI는 방어·일꾼
   유지·생산 유휴 방지를 **규칙**으로 갖고 있다.

메모리의 교훈("실행기 개선이 정책 개선보다 항상 더 큰 점프": 마이크로 실행기
v1 = 21.8→49.7유닛)의 연장선.

## 2. 결정 게이트 (A)

`AiDecisionGateEvaluate(state, observation, encoding, losses, owner_packet_pending, frame, config)`
— 순수 함수 + 명시적 상태. RL 펌프와 모방 로거가 동일 규칙 공유. 모든 입력은
시뮬레이션 상태에서만 파생(P2P 결정론 불변식 유지, 같은 시드 byte-identical).

- `min_interval_frames = 8`: 이보다 자주 결정하지 않음(체크 cadence이기도 함).
- `max_interval_frames = 64`: 이벤트가 없어도 이 주기로는 결정.
- `due = first || (frames_since >= min && triggers != 0)`.
- **스냅샷은 due일 때만 갱신** — 트리거는 "지난 결정 이후" 기준.
- 정책이 결정에서 목표를 바꾼 직후 펌프가 `AiDecisionGateSnapshotObjectives`로
  목표 요약만 재갱신(자기 변경이 objective_done으로 재발화 방지).

### 트리거 (비트, append-only; 피처 [773..784] 순서 동일)

| bit | 이름 | 조건 |
|---|---|---|
| 0 | max_interval | frames_since >= max |
| 1 | production_open | 지난 결정 때 불법이던 produce_*/build_*/research_*가 합법으로 |
| 2 | completion | 완성 유닛+건물 수 또는 연구 레벨 합 상승 |
| 3 | enemy_sighted | 가시 적 0 → >0 |
| 4 | enemy_lost | 가시 적 >0 → 0 |
| 5 | contact | engaged fraction 0 ↔ >0 전이 |
| 6 | base_threat | 아군 건물 defend_radius(800) 내 적 전투 mobile **출현**(에지) 또는 완성 건물 HP 감소(스냅샷 대비) |
| 7 | own_loss | 누적 손실 회계(vl/bl) 증가 |
| 8 | objective_done | army 목표 kind 변화(실행기 전이)/공격 타깃 소진/raid 전멸/단일 유닛 그룹 해제 |
| 9 | build_rejected | 지난 결정 이후 새 건설 거부 발생 |
| 10 | owner_packet | 모방 전용: 기록 대상 owner가 패킷을 보냄 |
| 11 | first | 첫 결정 |

- no_op은 계속 합법: 이벤트 결정에서 no_op = "현재 의도 유지".
- 모방 로거: 명령이 있는 8프레임 창 직전 상태에 라벨(≤8프레임 stale),
  무사건 구간은 max_interval당 no_op 1개.
- **-AIGATE:0** = max=min=8 → 8프레임 결정(구버전 A/B 재현).

## 3. 방어 반사 + 매크로 오토파일럿 (C)

### 3.1 기지 방어 반사 (실행기, 매 프레임)

`AiMicroThreatOverlay` — 정책의 `objectives[]`는 **절대 불변**(오버레이라 복원
불필요). 발동: 아군 건물(≥0x60, 0x6a 제외) defend_radius 내 적 전투
mobile(role melee/ranged; 일꾼 하베스트는 위협 아님) 또는 완성 건물 HP 감소.
반응: army(+앵커 1600px 내 raid)가 위협 건물 앵커의 임시 defend 목표로 교전 —
기존 defend 로직(모든 아군 건물 버블, 표적 선택/집중/멜레 분산/leash/저체력
후퇴) 전부 재사용. 해제: 반경 내 위협이 `threat_clear_frames`(120) 동안 없을 때.
예외: 정책 army 목표가 retreat이면 미개입. `-AIREFLEX:0`으로 비활성.
게이트 trigger_base_threat와 연동(정책도 즉시 깨어남).

구현 선택 메모: 명세의 "attack/units_first 로직"과 달리 **defend 오버레이**로
구현 — defend가 이미 units-first 표적 선택과 leash(멀리 끌려가지 않음)를
제공하고, 기지 방어에서 leash가 정확히 원하는 성질이라서다.

### 3.2 매크로 오토파일럿 (펌프, check cadence = 8프레임)

`AiAutopilotPlan` — 관측+합법 마스크(예약 자원 차감 포함 → 정책의 확장을 굶기지
않음) → 0..n개의 보조 `AiRlHighLevelAction`. 기존 번역기+라이브 밸리데이터
경로로 발행. 우선순위: 정책 > 일꾼 하한 > 인구 가드 > 생산 유휴 가드; 같은
생산자에 같은 프레임 두 명령 금지(정책 픽과 충돌 시 규칙 스킵).

| 규칙 | 조건 | 행동 |
|---|---|---|
| 일꾼 하한 | 일꾼(생존+건설중+큐) < 10 ∧ produce_worker 합법 | produce_worker |
| 인구 가드 | demand+2 ≥ supply ∧ 건설 중 pop nest 없음 ∧ 합법 | build_population_nest |
| 생산 유휴 가드 | 에그 네스트 96프레임 연속 유휴 ∧ 예약 제외 뱅크 ≥ 600 ∧ 합법 | 정책이 마지막 고른 에그 전투유닛(기본 masos) 1개 |

- 트레이스에 결정으로 기록하지 않음. 발동 성공만 카운트.
- `-AIAUTOPILOT:0` 비활성. `-AIIMITATE` 관찰 owner(내장 AI)에는 미적용.

## 4. 계약 (v9)

- **피처 788 (v8 772 + 16, append-only), `kAiRlFeatureVersion = 9`**:
  - [772] 지난 결정 이후 프레임/64 clamp
  - [773..784] 트리거 비트 12개 one-hot (§2 표 순서)
  - [785..787] 지난 결정 이후 오토파일럿 발동(일꾼/pop nest/전투유닛)/8 clamp
  - 인코더는 0을 기록; `ApplyAiRlDecisionContext`가 패치(게이트/펌프 상태라
    관측이 아님; 레이아웃은 rl_features 한 곳에 유지)
- **에피소드 JSONL**: `"why"`(트리거 비트), `"dt"`(이전 결정과의 프레임 차)
  append-only. 관측 JSONL(ai_rl_observe.jsonl)도 동일.
- **결과 JSON**: owner별 `autopilot_workers/pop_nests/fighters`,
  `reflex_activations` 추가.
- **플래그**: `-AIAUTOPILOT:0/1 -AIREFLEX:0/1 -AIGATE:0/1` (기본 1).
- **Python**: `N_FEATURES=788`; `Transition.why/dt`; `compute_gae(..., dt=)`
  SMDP 보정(γ**(dt/8), `ranker_ppo --smdp`; 기본은 기존 동작);
  `load_observations`가 why/dt 반환; 데이터셋 빌드가 no_op 비율 출력.
- 실행기 정합: 정책 호출이 드물어도 목표(objective)는 지속형이고
  `policy_hold_frames`(16)/재발행 규칙은 결정 간격과 무관하게 동작 — "의도
  지속"은 추가 구현 없이 성립(확인함).

## 5. 측정 결과 (2026-08-30, 검증 배터리)

> 측정 절차: `-AISELF -AIRANDOM`(랜덤-합법 정책) 8시드(1..8), `-AITRIBE:4`,
> 20000프레임 또는 섬멸까지. (a) = `-AIGATE:0 -AIAUTOPILOT:0 -AIREFLEX:0`
> (구버전 거동), (b) = 전부 기본 ON.

### 결정론

`-AISELF -AIRANDOM -SEED:7 -MAXFRAMES:20000` 2회 →
`ai_rl_episode.jsonl`/`ai_selfplay_result.json` **byte-identical**.

### A/B (랜덤 정책 vs 내장 AI, 8시드 평균)

| 항목 | (a) 구버전 | (b) v9 | 변화 |
|---|---|---|---|
| 게임당 결정 수 | 1446 | 533 | **-63%** |
| 에피소드 no_op 픽 비율 | 29.6% | 15.1% | -49% |
| 섬멸 프레임(생존 시간) | 11568 | 13707 | **+18%** (시드별 6/8 우세) |
| 종료 시 own 유닛/가치 | 1.2u / 125v | 1.9u / 288v | +130% 가치 |
| 종료 시 opp 유닛 | 66.8 | 83.5 | (더 오래 살아 opp도 성장) |
| 오토파일럿 발동 (w/p/f) | 0/0/0 | 4.8/0/0.1 | — |
| 반사 발동 | 0 | 5.4 | — |
| 생존(건물 보유) | 0/8 | 0/8 | 랜덤 chooser는 여전히 전패 (예상 범위) |

시드7 단독 트리거 분포(12035프레임 게임, 408결정): base_threat 191,
max_interval 96, production_open 50, completion 39, own_loss 39, contact 14,
enemy_sighted 7, enemy_lost 6, objective_done 2, first 1. dt 평균 29.5 / 최대 64.

### 모방 라벨 분포 (게이트 로거)

| 소스 | 이전(8프레임 고정) | v9 게이트 |
|---|---|---|
| 내장 AI 관찰 (2게임×20000f) | no_op 73% | 992샘플, **no_op 47.5%** |
| 인간 리플레이 gameplay1.ply | 2872샘플/2805라벨, no_op 73%, 패킷 라벨 190 | 1346샘플/1279라벨, **no_op 57.8%**, **패킷 라벨 190 유지** |

패킷-정확 라벨(190)이 전부 보존됨 = 명령이 있는 창은 하나도 잃지 않았다.
델타 추론 라벨은 창이 길어져 일부 병합·소실(757→540) — 정확 라벨 중심의
리플레이 데이터에는 영향 미미, 내장 AI 관찰은 라벨 밀도가 27%→52.5%로 상승.

### 완료 기준 대비

(b)는 (a) 대비 **섬멸이 늦고**(+18%, 6/8 시드) **결정 수가 급감**(-63%) —
명세 §7 기준 충족. `ranker_ppo.py --selftest` / `ai_play_interface_regression`
(게이트/반사/오토파일럿 케이스 포함) 전부 통과.

## 6. 남은 선택지

- 모방 로거의 게이트는 owner_packet 트리거 중심 — 내장 AI 관찰에서 손실
  회계(vl/bl)를 전달하지 않아 own_loss 트리거는 모방 모드에서 비활성(전달
  배선은 사소하나 라벨 품질에 영향 없음이라 보류).
- SMDP 보정(--smdp)은 기본 off — dt 분포가 안정된 뒤 짧은 A/B로 결정.
- 오토파일럿 생산 유휴 가드는 에그 네스트 기준(정책 픽이 0x87/base 유닛이면
  masos 폴백) — 필요해지면 생산자 타입별 유휴 클록으로 확장.
