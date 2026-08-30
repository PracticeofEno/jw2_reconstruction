# AI Play 티라노 전체 능력 커버 설계 (Action v2 / Observation v2 / Micro v2)

이 문서는 티라노족이 게임에서 할 수 있는 **모든** 조작을 RL 봇이 표현할 수
있도록 semantic action, observation/feature, micro 실행기를 재설계한다.
근거는 세 갈래 감사 결과다.

1. 티라노 로스터·테크트리 실측 (`ai_techtree_audit.txt` 런타임 덤프, JW2_09/10.TRC)
2. 패킷 수신부 명령→상태 매핑 (`ranker_unit_commands.cpp:2833-2900` 진입 테이블,
   `ranker_gameplay_packets.cpp:829-1021`)
3. 현재 RL 스택 제약 (`ranker_ai_rl_features.*`, `ranker_ai_scripted_bot.cpp`,
   `tools/ai/*.py`)

관련 문서: [AI_PLAY_RL_STRUCTURE.md](AI_PLAY_RL_STRUCTURE.md),
[AI_PLAY_BOT_COMMAND_CATALOG.md](AI_PLAY_BOT_COMMAND_CATALOG.md),
[AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md](AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md).

---

## 1. 티라노족 능력 전수 카탈로그 (실측)

### 1.1 유닛 로스터 (JW2_09.TRC 덤프, 비용/인구/생산시간 확정)

| type | 이름 | 비용 | 인구 | 생산처 | 비고 |
|---|---|---:|---:|---|---|
| 0x20 | 다이노스 | 100 | 1 | 0x80 | 일꾼: 채집(0x07)·건설(0x06, 10종) |
| 0x21 | 마소스 | 100 | 1 | 0x84 | 초반 근접 |
| 0x22 | 벨로시스 | 250 | 1 | 0x84 | 주력 (내장 AI 주병력) |
| 0x23 | 트윈 벨로시스 | 300 | 2 | **합체** | 생산 건물 없음 — 링크드 합체 산물 |
| 0x24 | 딜로포스 | 250 | 2 | 0x84 | |
| 0x25 | 람포스 | 300 | 1 | 0x84 | |
| 0x26 | 트윈 람포스 | 0 | 2 | **합체** | cost 0 = 합체 전용 |
| 0x27 | 프테라스 | 450 | 2 | 0x84 | 공중 |
| 0x28 | 트리세스 | 800 | 4 | 0x84 | 중장; 속도연구 0x2d |
| 0x29 | 둥가리 | 400 | 2 | 0x87 | **진영 수송 유닛** (`faction_carrier_unit_types[2]=0x29`) |
| 0x2a | 에그 스로워 | 600 | 4 | 0x87 | 공성 |
| 0x2b | 뮤턴트 | 1500 | 8 | **3기 합체** | 합체 연구 0x18 선행; 자체가 0x18 연구 가능 |
| 0x2c | 티라노스 | 5000 | 25 | 0x80 | 영웅급 |
| 0x2d | 트윈 프테라스 | 800 | 4 | **합체** | |
| 0x2e | 켄트로스 | 600 | 3 | 0x84 | |

### 1.2 건물 로스터 (일꾼 0x20이 전부 건설 가능)

| type | 이름 | 비용 | 역할 |
|---|---|---:|---|
| 0x80 | 티라노 네스트 | 1000 | 본진: 0x20/0x2c 생산, 연구 0x14·0x2a·0x38·0x2b |
| 0x82 | 네스트 | 200 | 인구 +8 |
| 0x83 | 에스코모이드 | 350 | 방어탑 계열 (인구 0; 타 종족 0x63 가드 타워와 병렬) |
| 0x84 | 에그 네스트 | 400 | 유닛 7종 생산 |
| 0x85 | 랜드 네스트 | 600 | 연구 0x19(지상공)·0x1a(지상방)·0x16(이속) |
| 0x86 | 스카이 네스트 | 600 | 연구 0x1c(공중공)·0x1d(공중방)·**0x18(뮤턴트 합체)** |
| 0x87 | 스로우 네스트 | 400 | 0x29/0x2a 생산 |
| 0x88 | 업그레이드 네스트 | 500 | 생산/연구 목록 없음 → 테크 해금 게이트로 추정 |
| 0x89 | 랜드 니스도스 | 800 | 연구 0x1b(근접강화)·0x2d(트리세스 이속) |
| 0x8a | 스카이 니스도스 | 800 | 연구 0x1e(공중강화) |
| 0x8f | 서머닝 네스트 | 500 | 일꾼 건설 목록에 없음 — 소환 계열 경로 (§7 확인 항목) |

### 1.3 연구 13종 (order → 건물, 비용·레벨 실측)

`0x14` 베리채집(0x80, 1렙 500) / `0x2a` **공룡 변신 업그레이드**(0x80, 300) /
`0x38` 티라노 헤이스트(0x80, 300) / `0x2b` 레벨업 경험치 감소(0x80, 500→1000) /
`0x19`·`0x1a` 지상 공/방 5렙(0x85, 200→600) / `0x16` 이속(0x85, 400) /
`0x1c`·`0x1d` 공중 공/방 5렙(0x86) / `0x18` **뮤턴트 합체**(0x86 또는 0x2b, 300) /
`0x1b` 근접강화(0x89, 600) / `0x2d` 트리세스 이속(0x89, 500) / `0x1e` 공중강화(0x8a, 600).

### 1.4 종족 고유 메커니즘 (패킷 도달 경로 확정)

| 메커니즘 | 와이어 경로 | 엔진 근거 |
|---|---|---|
| **합체(링크드 릴리즈)** — 트윈 3종·뮤턴트 | subtype 0x02 cmd `0x0b` (pair=양방향 2패킷, triad=3패킷 링) | 진입 `:2865`→상태 0x5f, capability `type_flags&0x800`; 송신 UI `ranker_winmain.cpp:7039-7237` |
| **변신(morph)** | cmd `0x11` 진입 / `0x1b` 해제 | `:2874/:2883`→상태 0x6e/0x6f; 게이트 `morph_type_id!=0 && type_flags&0x20000`; 연구 0x2a와 연동 |
| **특수 능력 시전** | subtype `0x09`, **command 바이트 = ability_id (0x00~0x2d)** | 전부 상태 0x64 진입(`:2821-2831`); 비용=JW2_11 catalog `secondary_cost`, 효과=효과정의 `0x3d+ability_id` |
| **스탠스 토글** | cmd `0x12~0x15` → `state_flags |= 0x4000/0x8000/0x10000/0x20000`; UI 0x26~0x29로 해제; subtype 0x09 cmd `0x13`은 별도 0x840 토글 | `ranker_gameplay_packets.cpp:834-851, 983-1000` |
| **마나(2차값) 이전/균등화** | cmd `0x23` → 상태 0x73 value-transfer | `:2892`, 송신 집계 `ranker_winmain.cpp:7437-7516`; 리플레이 3,988회 |
| **수송(둥가리)** | 탑승 cmd `0x0a`(상태 0x3c→0x3d~0x3f), 하차 cmd `0x24`(상태 0x41~0x43), 탑재중 0x45 | `:2862/:2895`, `BeginUnitCarrierBoardingCommand :5131`; capability bit 0x0a(`0x400`) |
| **순찰** | cmd `0x09` → 상태 0x35 | `:2859`; 리플레이 36회 |
| **정지 / 홀드** | cmd `0x00`(정지=즉시 idle) / subtype `0x0a` cmd `0x21`(홀드, `order_flags|=8`) | `:2835` / `ranker_gameplay_packets.cpp:1012-1021` |
| **자원 반납** | cmd `0x07`+target `0x80000000` 또는 드롭오프로 cmd `0x02` | 송신 UI `ranker_winmain.cpp:7366-7414` |
| **아이템 사용** | cmd `0x16` → 상태 0x87 (usable item id 0x1b만) | `:2877`, `:6301-6347` |
| **레벨/경험치** | 연구 0x2b가 레벨업 경험치 감소; 능력 데미지는 레벨 보정(`CalculateUnitVariantScaledBonus61c`) | `ranker_winmain.cpp:26156-26160` |

**중요한 원리 (mask/검증의 근간):** 유닛이 명령 N을 쓸 수 있는가 =
`type_flags & (1<<N)` (`ranker_winmain.cpp:20511`, `supports_action`
`ranker_ai_actions.cpp:35`). 즉 **와이어 command 번호가 곧 capability 비트
인덱스**다. 관찰에 이미 `type_flags`가 있으므로 새 액션의 legal mask는 전부
관찰만으로 계산 가능하다.

---

## 2. Semantic Action v2 — `AiSemanticActionKind` 확장

`kAiActionSchemaVersion`을 2로 올리고 다음을 추가한다. 기존 10종의 인덱스와
패킷 형식은 그대로 유지(v1 하위호환), 새 kind는 뒤에 append.

```text
기존: no_op, move, attack_move, attack_unit, harvest, produce_unit,
      research, build, set_rally, cancel_production
추가:
  stop              // cmd 0x00, 다중선택 가능
  hold_position     // subtype 0x0a / cmd 0x21, 다중선택
  patrol            // cmd 0x09 + 목표점, 다중선택
  use_ability       // subtype 0x09, ability_id(0x00~0x2d), 대상 유닛 or 지점
  morph_enter       // cmd 0x11 (게이트: morph_type_id && type_flags&0x20000)
  morph_exit        // cmd 0x1b (게이트: type_flags&0x08000000)
  merge_units       // cmd 0x0b: unit_ids 2기(pair 양방향 2패킷) or 3기(triad 링)
  board_transport   // cmd 0x0a: 승객들+target=수송 유닛
  unload_transport  // cmd 0x24: 수송 유닛+하차 지점
  transfer_secondary// cmd 0x23: 균등화(planner가 donor/recipient/threshold 계산)
  set_stance        // cmd 0x12~0x15 on / 0x26~0x29식 off (stance_id 0~3 + on/off)
  return_cargo      // cmd 0x07+0x80000000 or 드롭오프 cmd 0x02
  use_item          // cmd 0x16 (아이템 슬롯 보유 시)
```

`AiSemanticAction` 구조체 추가 필드: `ability_id`(u32), `stance_id`(u32),
`stance_on`(bool). 기존 필드(`unit_ids`, `target_unit_id`, `target_x/y`,
`queued`) 재사용.

**검증 규칙 (fail-closed 원칙 유지, `PlanAiSemanticActionV1` 패턴):**
- 모든 kind: capability 비트(`type_flags & (1<<cmd)`) 확인, 소유·생존·중복 검사 재사용.
- `use_ability`: `ability_id < 0x2e`; `secondary_value >= JW2_11 secondary_cost`
  (live validator 콜백으로 위임 — 생산 검증과 같은 패턴으로
  `AiAbilityAvailabilityCallback` 신설); 대상 가시성 검사(적 대상 시).
- `merge_units`: 2기는 동일 type + `type_flags&0x800`; 3기는 유효 조합
  (§7 런타임 덤프로 조합 테이블 확정 후 코드화); 뮤턴트 합체는 연구 0x18
  완료 확인(`research_order_levels[0x18]`).
- `morph_enter`: 연구 0x2a 게이트가 데이터에 있는지 §7에서 확인 후 반영.
- `board_transport`: 승객 `!can_carry`, 수송 `can_carry(0x400)`+용량,
  `transport_flags&4`.
- `set_stance`: on은 `action_mode != 0`(마나 보유) 확인, off는 해당
  `command_flags` 비트가 켜져 있는 유닛만.

---

## 3. Observation v2 — 인코더가 버리던 정보 활용

`BuildAiObservationV1`은 이미 필요한 원천을 다 담고 있다(마나, command_flags,
큐, 타일, 연구 64행). **C++ 관찰 빌더는 변경 최소** — 추가할 것은 유닛별
`type_flags`(이미 있음), `state_flags`의 스탠스 비트 노출 여부 점검 정도.

### 3.1 Feature v2 (46 → 84, append-only)

| idx | 내용 | 근거 |
|---|---|---|
| 46~55 | 연구 레벨 10종 추가: 0x1a,0x1c,0x1d,0x18,0x2a,0x38,0x2b,0x1b,0x2d,0x1e (각 /max) | 기존 36-38이 0x14/0x16/0x19만 커버 |
| 56~66 | 유닛 수 11종 추가: 0x23,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e (각 /50, 0x2b·0x2c는 /10) | 합체·수송·영웅 상태 인지 |
| 67~70 | 건물 수 4종 추가: 0x83,0x88,0x89,0x8a (각 /5) | 테크 상태 인지 |
| 71,72 | 아군 마나 합/최대 합 (정규화) | 능력·스탠스 예산 |
| 73 | 마나 준비 유닛 수(secondary ≥ 최소 능력비용)/20 | cast mask 근거 |
| 74 | 스탠스 활성 유닛 수/20 (`command_flags & 0x3c000`) | 토글 상태 |
| 75 | 변신 상태 유닛 수/20 (`runtime_flags & 0x40000` 대응 관찰 필드) | morph_exit mask |
| 76,77 | 수송: 탑재 승객 수/14, 탑승 가능 여유 용량/14 | 수송 액션 mask |
| 78 | 아군 army 평균 체력비 | 교전 판단 |
| 79,80 | 가시 적 지상/공중 병력 수 분리 (/50) | 공중강화·대공 판단 |
| 81 | 생산 큐 총 대기 수/20 (`deferred_command_count` 합) | 이중 주문 방지 |
| 82 | 유휴 생산건물 수/10 | 생산 mask 보조 |
| 83 | 일꾼 cargo 보유 수/50 (`cargo_amount>0`) | return_cargo mask |

공간(타일) 그리드는 v3로 미룬다(§8). `kAiRlFeatureVersion = 2`.

### 3.2 관찰 스키마 보강 (필요 시)

- `AiObservedUnit`에 스탠스 비트(원본 `state_flags` 0x4000~0x20000, 0x840)가
  현재 `command_flags`로 노출되는지 검증하고, 아니면 v2 필드로 추가.
- 능력 가용성 오라클: 관찰에 넣지 말고 **live validator 콜백**으로 유지
  (생산과 동일 원칙 — 관찰이 fog/카탈로그 오라클이 되면 안 됨).

---

## 4. RL 상위 액션 v2 — 30 → 44 (append-only)

기존 0~29 유지. 추가:

```text
30 merge_twin_velocis   // 벨로시스 2기 → 트윈 벨로시스
31 merge_twin_rhampos   // 람포스 2기 → 트윈 람포스
32 merge_twin_pteras    // 프테라스 2기 → 트윈 프테라스
33 merge_mutant         // 3기 조합 → 뮤턴트 (연구 0x18 후)
34 morph_enter_army     // 변신 가능 유닛 일괄 변신
35 morph_exit_army
36 cast_offense         // 마나 준비 유닛이 최근접 적 밀집점에 시전
37 cast_support         // 아군 대상 시전(회복/버프 계열)
38 stance_on            // 헤이스트 등 지속 모드 일괄 on
39 stance_off
40 balance_mana         // 마나 균등화 (0x23)
41 hold_army            // 군대 현위치 홀드
42 patrol_defense       // 본진~자원선 순찰
43 drop_attack          // 둥가리 탑승→적 본진 하차 (composite: board→move→unload)
```

- `stop`/`return_cargo`는 상위 액션으로 노출하지 않고 `defend_base`/`retreat`
  실행기의 micro 개선에 흡수한다(후퇴 시 자원 반납, 도착 시 홀드).
- **mask 계산**: 전부 feature v2 신규 항목으로 게이트 (예: `merge_mutant` =
  연구 0x18 완료 ∧ 조합 3종 각 1기 이상; `cast_offense` = f[73]>0 ∧ 가시 적;
  `drop_attack` = 둥가리≥1 ∧ army≥4).
- 능력별 세분화(cast를 ability_id 단위로 쪼개기)는 §7 카탈로그 덤프로
  티라노 보유 ability 목록 확정 후 v2.1에서 결정. 초기에는 offense/support
  2슬롯 + micro가 ability_id 선택.

## 5. Micro 실행기 v2 — `DecideTyranoScriptedBotForHighLevelAction` 확장

### 5.1 신규 실행기

| 액션 | micro 로직 |
|---|---|
| merge_* | 같은 type 2기 중 서로 최근접 쌍 선택 → 양방향 0x0b 패킷 2개 (UI `7216-7223` 형식 재현). triad는 3패킷 링(`7200-7212`) |
| morph_enter/exit_army | 대상: morph 게이트 통과 유닛 전원(14기 배치 제한 준수, 여러 decision frame에 걸쳐 자동 분할) |
| cast_offense | 마나 최대 유닛부터, 대상 = 반경 내 적 최다 밀집 유닛; ability_id는 유닛별 가용 목록(§7 테이블)에서 비용 대비 효과 우선순위 |
| cast_support | 대상 = 체력비 최저 아군 (heal 계열 delta<0 능력 보유 시) |
| stance_on/off | `action_mode != 0`(on) / 플래그 보유(off) 유닛 일괄 |
| balance_mana | UI 알고리즘 재현: 평균 threshold 계산 → donor/recipient 쌍 생성 (`7437-7516`) |
| hold_army | army 전원 subtype 0x0a 홀드 |
| patrol_defense | 본진↔최근접 자원 클러스터 2점 순찰 |
| drop_attack | 상태기계: 둥가리에 최근접 전투유닛 탑승(용량까지) → 적 시작점으로 move → 도착 반경 내 unload → attack_move. `intent` 상태로 여러 frame에 걸쳐 진행 |

### 5.2 기존 실행기 개선 (신규 명령 활용)

- `defend_base`: 도착 후 **hold_position** (현재 move뿐이라 적을 쫓아 흩어짐);
  침입 없으면 patrol_defense 폴백.
- `retreat`: 후퇴 전 cargo 보유 일꾼 **return_cargo**; 군대는 본진 도착 후 hold.
- `attack_enemy_base`: 이동 중 스탠스(헤이스트) on 지원.
- 오토파일럿은 현행 유지(신규 명령은 정책 결정으로만 발동; auto-cast는
  reward hacking 위험이 있어 배제).

### 5.3 상수/구조 변경

- `kTyranoScriptedBotIntentCount` 16 → 신규 intent 수만큼 확장 (retry backoff 슬롯).
- `TyranoScriptedBotState`에 drop_attack 단계 저장 필드 추가 (결정론 유지:
  frame 기반 전이만).

## 6. 인프라 변경 목록 (전파 지점)

| 항목 | 파일 | 변경 |
|---|---|---|
| `kAiRlFeatureCount` 46→84, `kAiRlActionCount` 30→44, `kAiRlFeatureVersion` 1→2 | `ranker_ai_rl_features.h` | 인코더/mask 본체 확장 |
| `kAiActionSchemaVersion` 1→2 + 신규 kind 검증 | `ranker_ai_actions.{h,cpp}` | §2 |
| 능력 가용성 콜백 | `ranker_ai_actions.h`, winmain 배선 | 생산 콜백과 동일 패턴 |
| IPC 주석 갱신(feat[46→84]/mask[30→44]) | `ranker_ai_ipc.h`, `ranker_ai_ipc.cpp` | 배열 길이는 상수 따라감 |
| `N_FEATURES/N_ACTIONS/ACTION_NAMES` | `tools/ai/ranker_rl_env.py` | enum 순서와 정확히 일치 |
| 체크포인트 호환성 검사 | `tools/ai/ranker_ppo.py` | **load 시 n_features/n_actions 불일치 즉시 에러** (현재 미검사 — 이번에 반드시 추가) |
| 모방 라벨러 인덱스 | `tools/ai/ranker_imitation.py` | `_counts`/`infer_label` 재작성 or v2 라벨 범위 제한 명시 |
| 회귀 테스트 | `tests/ai_play_interface_regression.cpp` | 신규 kind별 okay/reject 케이스, 결정론 해시 |

주의: feature/action 수 변경으로 **기존 체크포인트는 전부 무효** (첫/끝
레이어 shape 변경). Round 6까지의 스냅샷과 호환되지 않으므로 v2 전환 시점을
세대 경계로 잡는다.

## 7. 선행 확인 작업 — **완료 (2026-08-28 실측 결과)**

감사 덤프를 확장해(`caps` 행 + `ability` 카탈로그) 1회 실행, 전부 확정:

1. **capability 비트 (`type_flags`)**: 전 유닛 [0x00 정지, 0x01, 0x04 이동,
   0x05 공격, 0x09 순찰, 0x0d] 공통. 추가로 —
   합체 0x0b: 0x22/0x24/0x25/0x27/0x28; 변신 0x11: 0x22/0x24/0x25/0x27/0x2e
   (딜로포스·벨로시스·람포스·프테라스·켄트로스); 스탠스는 **0x14 하나만**
   (마소스·벨로시스·트윈들·트리세스·둥가리·뮤턴트·티라노스);
   둥가리(0x29)만 수송 0x0a 보유(공격 0x05 **없음** — 순수 수송);
   일꾼(0x20)만 건설 0x06·채집 0x07. 건물은 0x0e/0x10(생산 계열),
   에스코모이드(0x83)는 0x05 보유(방어탑 확정).
2. **변신 대상 (`morph`)**: 야생 공룡 블록으로 변신 — 0x22→0x45, 0x24→0x44,
   0x25→0x4e, 0x27→0x4d, 0x28→0x41(35pop급!), 0x2e→0x47. 연구 0x2a가 UI 게이트.
3. **합체 레시피 (`linked`)**: 0x22→0x23, 0x25→0x26, 0x27→0x2d (pair).
   triad는 원본 UI 코드가 {0x24,0x27,0x28}→0x2b 하드코딩 + 인구 투영 검사
   (`ranker_winmain.cpp:7040-7156`). 연구 0x18 게이트.
4. **마나: 티라노 전 유닛 `mana=0`** → 시전형 능력·마나 균등화(0x23)·아이템
   (0x16) 비트도 전무. **cast/balance/item 계열은 티라노 RL 액션에서 제외**
   (semantic 계층은 종족 공통으로 구현 완료 — 타 종족 확장 대비).
5. **능력 카탈로그**: id 0x00~0x2d 비용/데미지/회복/생성 실측 완료(§부록 덤프).
   티라노는 사용 유닛이 없으므로 v2에서는 데이터로만 보존.
6. 남은 미확정(비차단): 0x8f 서머닝 네스트 생성 경로, 0x88 업그레이드
   네스트의 해금 역할, 스탠스 0x14의 `action_mode` 충전 조건(연구 0x38
   헤이스트 추정 — 라이브 검증 항목).

## 8. 단계별 구현 순서

1. **§7-1,2 덤프 확장 + 1회 실행** → 미확정 테이블 확정 (반나절)
2. **Semantic v2** (§2) + 회귀 테스트 — 검증 계층부터 (패킷 발행이 정확해야 이후 전부 성립)
3. **Micro 실행기** (§5) — 액션별 단독 검증 (`-AISCRIPT` 강제 실행 경로로 눈 확인)
4. **Feature/mask v2 + 상위 액션 44종** (§3,4) + Python 상수 동기화
5. **모방→PPO 재가동**: BC 웜스타트 재수집(신규 feature로) → Round 7을 v2로 시작
6. (v3) 공간 그리드 feature + 능력별 세분화 cast 액션

성공 판정: (a) 신규 kind 전부 회귀 테스트 통과 + 결정론 해시 불변,
(b) 스크립트 강제 실행으로 합체·변신·시전·수송·홀드가 리플레이에서 육안 확인,
(c) v2 랜덤 정책이 한 경기 완주(배관 검증), (d) v2 PPO가 v1 챔피언 상대 승률 ≥50%.

---

## 부록: 구현 결과 (2026-08-28)

**전부 구현·검증 완료.** 최종 수치는 설계와 다르게 확정: **feature 46→80,
action 30→41** (티라노에 능력 시전/마나 균등화/아이템이 없음이 실측으로
확인되어 cast/balance/item 상위 액션 제외 — semantic 계층 13종은 종족 공통
으로 전부 구현, `use_ability`/`transfer_secondary`/`use_item` 포함).

구현하며 발견·수정한 기존 버그 2건 (v1 시절부터 존재):

1. **14기 초과 부대 명령 무시**: `kAiMaximumUnitsPerAction=14` 초과 셀렉션을
   플래너가 `too_many_units`로 거부하는데 실행기/오토파일럿 어디도 분할하지
   않아 대군 명령이 통째로 무시됨 (대군 교착의 유력 원인).
   → `ChunkAiSemanticActionUnits` 신설, 펌프·오토파일럿 전체 청크 적용.
2. **인구 mask 의미론 오류**: `population_used`는 사용량이 아니라 **class-2
   건물이 공급한 인구 한도**(감사 pop= 열), `population_reserved`가 실제
   수요. 검증식은 `reserved+cost ≤ min(supply, limit)`인데 v1 mask는
   `limit−used`로 계산해 인구 게이트가 사실상 항상 통과 → 생산 픽 낭비.
   → 인코더 `pop_free = min(supply,limit) − reserved`로 수정.

추가 실측: **선행 건물 트리** (덤프 prereq= 열) — 딜로포스/에그스로워←랜드,
람포스/트윈람포스/둥가리←스카이, 프테라스←스카이+업글, 트리세스←랜드니스도스,
켄트로스←랜드+업글, 티라노스←고급 5종 전부, 업글 네스트←랜드+스카이,
니스도스 쌍←(랜드|스카이)+업글, 0x8f 서머닝←본진+네스트. 전부 mask에 반영.

라이브 검증 (강제 우선순위 프로브, -AIVS 무저항 샌드박스, seed 77):
합체 → 트윈 벨로시스 실생성(×2), 둥가리 탑승(3기 attached) → 목표 이동 →
하차 → 강습 루프 반복 실작동. 같은 시드 재실행 시 히스토그램 바이트 동일
(결정론 유지). 변신·스탠스 라이브 발화는 연구 체인(0x2a/0x38) 도달이 필요해
Round 7 실전 로그에서 확인 예정 (플래너·수신부 게이트는 회귀 테스트 커버).

스탠스 미확정 잔건: cmd 0x14 게이트 `action_mode != 0`의 충전 조건(연구
0x38 헤이스트 추정) — 관찰 필요.
