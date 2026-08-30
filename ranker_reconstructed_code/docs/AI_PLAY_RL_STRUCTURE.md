# AI Play 학습 구조 설계 (RL) — 우리 코드에 밀착

이 문서는 self-play 강화학습(RL)의 개념(Return/할인, Value, TD error, Advantage,
PPO, reward shaping, Hierarchical RL)을 **현재 재구축 코드에서 실제로 어떻게
얹을지** 구체적으로 설계한다. 참고 대화가 마지막에 강조한 "State를 무엇으로
잡고 Action을 어디까지 쪼개느냐"가 이 프로젝트의 가장 중요한 설계 문제이며,
좋은 소식은 **그 구조가 이미 우리가 만든 것과 일치**한다는 점이다.

## 0. 한 장 요약 — 이미 갖춘 것과 맞물리는 지점

| RL 개념 | 우리 코드의 대응물 | 상태 |
|---|---|---|
| State S | `AiObservation` (v1, 결정적 해시) — 시야 경계·적 private 상태 redaction 포함 | ✅ 있음 |
| Action a (상위) | `AiSemanticAction` / `PlanAiSemanticActionV1` (Produce/Build/Harvest/AttackMove/…) | ✅ 있음 |
| 하위 micro (Hierarchical) | `TyranoScriptedBot` — 상위 의도를 실제 packet 조작으로 실행 | ✅ 있음(재활용) |
| env `reset(seed)` | `-AISELF -SEED:N` (결정적, 같은 시드=동일 결과) | ✅ 있음 |
| 빠른 rollout | `fast_uncapped` 헤드리스(15배) + 백그라운드 | ✅ 있음 |
| reward 재료 | `ai_selfplay_result.json`의 owner별 유닛/자원/인구/생존 | ✅ 있음(확장 필요) |
| **정책이 결정하는 지점** | `run_default_ai_play_owner` 안의 `DecideTyranoScriptedBotAction` 호출 | 🔧 **여기를 외부 정책으로 교체** |

즉 지금까지 만든 것은 정확히 **Hierarchical RL의 뼈대**다. RL(정책)은 "상위
행동"을 고르고, 스크립트 봇은 그 micro를 실행한다. 참고 대화의 권고(마우스
클릭 전부를 action으로 두지 말고 상위 행동으로 묶고 micro는 기존
알고리즘에 맡겨라)와 완전히 같다.

**핵심 교체점:** 현재 `run_default_ai_play_owner`는 상위 행동을 하드코딩된
`DecideTyranoScriptedBotAction`으로 고른다. 학습에서는 이 **한 줄을 "외부
정책이 고른 상위 행동"으로 바꾼다.** 나머지(관찰 생성, semantic action→packet,
결정론, 고속)는 그대로 재활용한다.

## 1. 아키텍처 — 정책은 시뮬레이션 "밖"에 둔다 (P2P 결정론 불변식)

RTS 시뮬레이션은 모든 피어가 동일 packet으로 동일하게 실행돼야 한다
(`AGENTS.md`의 P2P 동기화 원칙). 신경망 추론은 OS/CPU/라이브러리에 따라
미세하게 달라질 수 있으므로 **시뮬레이션 안에서 직접 상태를 바꾸면 안 된다.**

그래서:

```text
┌────────────────────────────────────────────┐
│ ranker_rebuild.exe (결정적 시뮬레이션)        │
│                                            │
│  매 decision frame(v9: 이벤트 게이트 발화 시, │
│  min 8 / max 64 프레임 -                     │
│  docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md):   │
│   1. AiObservation 생성 (owner 관점)         │
│   2. observation feature → IPC 로 내보냄     │
│   3. 정책의 "상위 행동"을 IPC 로 받음         │
│   4. 상위 행동 → 스크립트 micro → semantic   │
│      action → ordered Mode1 packet 발행      │
│   5. 시뮬레이션 계속 (모든 소유자 동일 packet) │
└───────────────┬────────────────────────────┘
                │  IPC (named pipe / shared mem, 로컬)
                ↓
┌────────────────────────────────────────────┐
│ Python 학습 프로세스 (정책·가치망, PPO)        │
│   obs → π(a|s) 샘플 → action                 │
│   rollout 수집 → GAE/Advantage → weight 갱신  │
└────────────────────────────────────────────┘
```

- **단일 controller 원칙:** 봇 슬롯의 상위 행동을 결정하는 주체는 **한 곳
  (지정 controller)** 뿐이고, 결과는 기존 ordered packet으로만 발행한다.
  다른 피어는 모델을 실행하지 않고 같은 packet만 처리한다. (로드맵 9단계)
- **리플레이에는 모델 출력이 아니라 실제 처리된 packet이 남는다** → 모델 없이도
  경기 재생 가능. 학습·평가는 이 packet 스트림(`.ply`) 기준으로 재현한다.
- 초기에는 로컬 self-play(단일 프로세스)만 다룬다. live P2P에서 모든 피어가
  모델을 동시에 돌리는 구조(정수/고정소수점 결정론 입증 필요)는 나중 목표.

## 2. State 설계 — `AiObservation` → 고정 크기 feature 벡터

정책 신경망의 입력. 원칙: **사람이 볼 수 있는 정보만**(이미 관찰 빌더가
redaction), **실행마다 달라지는 포인터/주소 제외**, **고정 크기·정규화**.

### 2.1 전역 스칼라 (예)
- 자기 자원: primary/secondary (정규화, 예: /1000)
- 인구: used / limit / reserved
- 현재 생산 큐 길이(구조물별), decision frame 번호(정규화)
- 진영 id (one-hot), 맵 id, 시뮬레이션 진행도(프레임/최대)

### 2.2 자기 유닛 요약 (type별 집계)
전 유닛을 개별 슬롯으로 넣으면 action/obs 공간이 폭발한다. 초기에는 **type별
집계**로 압축:
- type별 개수: worker(0x20), Masos(0x21), Dilophos(0x24), base nest(0x80),
  pop nest(0x82), egg nest(0x84), land nest(0x85), …
- type별 평균 체력, 건설중 개수, 채집중 일꾼 수, 유휴 일꾼 수

### 2.3 적/중립 (시야 내만)
- 가시 적 유닛 type별 개수, 가시 적 건물 개수, 최근접 적까지 거리(구간화)
- 가시 적 병력 가치 추정치

### 2.4 맵/공간 (축약)
- 저해상도 그리드(예: 16×16 다운샘플): 각 셀에 {탐사됨, 현재가시, 내 유닛
  존재, 적 유닛 존재, 자원량, 통행가능} 채널. CNN 입력 또는 flatten.
- (초기 MVP는 2.1~2.3의 벡터만으로 시작해도 됨. 2.4는 다음 단계.)

> 구현 위치: `BuildAiObservationV1` 결과 → `encode_observation_features()`(신규).
> **결정성 테스트:** 같은 frame·seed 두 번 실행 → feature 벡터 byte 동일
> (이미 `AiObservation` 해시가 이 성질을 보장; feature 인코더도 동일 검증).

## 3. Action 설계 — 상위 이산 행동 + 스크립트 micro (Hierarchical)

참고 대화의 권고대로 **상위 행동만 정책이 고르고, micro는 스크립트 봇**이
처리한다. 초기 이산 행동 집합(예):

```text
0  NoOp                      (이번 사이클 아무 것도)
1  ProduceWorker             (여유 nest에서 일꾼 1)
2  ProduceArmy(Masos)        (여유 egg nest에서 army 1)
3  ProduceArmy(Dilophos)
4  BuildPopNest              (스크립트가 유효 지점 선택·건설)
5  BuildEggNest
6  BuildLandNest
7  Expand(new base nest)
8  Research(다음 연구)
9  HarvestSaturate           (유휴 일꾼을 가장 가까운 자원에 배치)
10 ScoutMap                  (정찰 유닛 파견)
11 AttackNearestEnemy        (군대를 가시 적에게)
12 AttackEnemyBase(방향)     (군대를 적 시작 방향으로 attack-move)
13 DefendBase                (군대를 본진으로 소집)
14 Retreat                   (교전 회피)
```

- 각 상위 행동은 **기존 `AiSemanticAction`(들)로 번역**되고
  `PlanAiSemanticActionV1` 검증·packet화를 그대로 탄다. 대상 유닛 선택·건설
  지점·경로 등 micro는 스크립트 봇 헬퍼(이미 있는 `select_builder`,
  `nearest_visible_resource_assignment`, `next_build_point`,
  exploration/attack 로직)가 담당.
- **Legal-action mask 필수:** 불가능한 행동(자원 부족, 건물 없음, 적 미가시
  등)은 확률 0으로 마스킹. 우리는 이미 live validator
  (`CheckAiLiveProductionAvailability`)와 관찰의 legal 정보가 있으니, 매
  스텝 `legal_mask[15]`를 함께 내보낸다. 이게 학습 초기 안정성을 크게 높인다.
- **행동 예산(APM):** v9부터 결정은 고정 간격이 아니라 **이벤트 기반**이다
  (`ranker_ai_decision_gate.*`, min 8 / max 64 프레임): 결정할 이유(생산
  개방·완료·적 출현/소실·교전 전이·기지 위협·손실·목표 완료·건설 거부)가
  생겼을 때만 정책을 호출한다. 8프레임 고정 호출은 no_op가 라벨의 73%를
  차지하고 부대 목표가 매 결정 요동하는 원인이었다
  (docs/AI_PLAY_DECISION_GATE_AUTOPILOT.md). APM 상한(min 8프레임)은 유지.

> 왜 이 분해가 중요한가: 개별 유닛·좌표를 전부 action으로 두면 공간이
> 수천~수만 차원이 되어 학습이 사실상 불가능하다. 상위 15개 + 스크립트 micro면
> 정책이 "무엇을 할지"에 집중하고, "어떻게"는 검증된 코드가 처리한다.

## 4. Reward 설계 — 희소 승패 + 신중한 shaping

참고 대화의 경고를 그대로 반영: **승리가 진짜 목표**이고, shaping을 잘못하면
"킬만 하고 도망가는" 다른 게임을 배운다.

- **주 보상(희소):** 승리 +1, 패배 −1 (게임 종료 시). 이게 최종 정답.
- **보조 shaping(작게, 승패 부호와 정렬):**
  - 적 유닛 처치 +0.01 / 내 유닛 사망 −0.01
  - 적 건물 파괴 +0.05 / 내 건물 파괴 −0.05
  - **potential-based shaping 권장:** `F = γ·Φ(S') − Φ(S)`,
    Φ = (내 병력가치+경제) − (적 병력가치+경제)의 정규화. 이 형태는 이론상
    최적 정책을 바꾸지 않으면서 신호를 조밀하게 만든다(reward hacking 위험↓).
- **credit assignment:** 위 신호를 Return(할인 γ≈0.997, RTS는 길어 큰 γ) +
  **GAE**(λ≈0.95)로 과거 행동에 전파. Value network가 "당장 reward=0이어도
  더 유리한 상태로 갔다"를 학습해 언덕 선점 같은 무보상 호수를 평가한다.

> 구현: `ai_selfplay_result.json`을 owner별 시계열(스텝별 유닛/자원/처치/손실)
> 로 확장해 Φ와 처치/손실 신호를 계산. 종료 시 승패는 end_condition/생존으로.

## 5. Env 인터페이스 — `reset()/step()` (로드맵 4단계)

```text
reset(seed) -> obs0, legal_mask0
    -AISELF -SEED:seed 로 게임 부팅, 첫 decision frame까지 진행, 관찰 반환
step(action) -> obs', reward, done, info
    상위 action을 IPC로 주입 → 스크립트 micro가 packet 발행 →
    다음 decision frame까지 시뮬 전진 → 새 관찰/보상/종료 반환
```

- 이미 있는 것: 결정적 부팅(`-SEED`), 고속(`fast_uncapped`), 자동 종료+결과
  (`ai_selfplay_result.json`), 프로세스 드라이버(`run_selfplay.py`).
- 추가할 것: (a) decision frame에서 **관찰을 내보내고 action을 받을 때까지
  대기**하는 IPC 훅(현재 `DecideTyranoScriptedBotAction` 호출부 교체),
  (b) 스텝 단위 reward 재료 노출, (c) 렌더/음향 완전 off 확인.
- **필수 검증:** 같은 seed + 같은 action sequence → 같은 관찰·checksum·종료
  결과(로드맵 4단계 요건). 우리는 `-SEED` 결정성으로 절반은 이미 확보.

## 6. 알고리즘 — PPO(+GAE, value network, action masking)

- **PPO** 권장(안정적, 이산 action, masking 쉬움). Actor(π)와 Critic(V) 공유
  몸통.
- Loss(개념): `-min(ρ·A, clip(ρ,1±ε)·A) - c1·(V−G)^2 + c2·H(π)`
  (ρ=신규/구정책 확률비, A=GAE advantage, H=엔트로피 보너스).
- **masking:** 불법 action logit을 −∞로 → 확률 0. (5장 legal_mask 사용)
- 상대 pool: 자기 과거 스냅샷 + 내장 Owner AI(강한 고정 상대). 항상 최신
  자신끼리만 두면 특정 전략에 과적합.

## 7. 단계별 구축 순서 (모델보다 배관이 먼저)

참고 대화도, 우리 로드맵도 같은 결론: **알고리즘 선택보다 State/Action/Env
배관이 먼저**다.

1. **feature 인코더** — `AiObservation` → 고정 벡터 + `legal_mask`. 결정성 테스트.
2. **Env 훅(IPC)** — decision frame에서 관찰 내보내고 상위 action 받기.
   먼저 "정책=랜덤 legal action"으로 배관만 검증(한 경기 완주 + 재현).
3. **상위 action → semantic action 번역기** — 15개 각각을 스크립트 micro로.
   (스크립트 봇 헬퍼 재활용; 하드코딩된 build order 의존 제거.)
4. **reward 시계열** — 스텝 보상 + 종료 승패.
5. **Python gym-스타일 wrapper + PPO** (작은 네트로 시작).
6. **모방학습으로 초기화(선택, 강력 추천)** — 내장 Owner AI 경기를 상위 action
   라벨로 기록 → behavior cloning으로 정책 웜스타트 → 그 위에 RL. (내장 AI는
   packet을 안 쓰지만, 그 in-sim 행동을 상위 action으로 라벨링하는 관측기를
   두면 교사 데이터가 된다.)
7. **RL self-play** — 상대 pool, frozen eval, 승률/게임시간 지표.

## 8. 지금 당장의 다음 액션 (1~3)

- (1) `encode_observation_features()` + `legal_mask` 정의·구현(결정성 테스트).
- (2) `run_default_ai_play_owner`의 결정 지점을 "외부 상위 action 주입"으로
  교체(우선 랜덤 legal 정책으로 배관 검증). 스크립트 봇은 **micro 실행기**로
  강등(하드코딩 정책 아님).
- (3) 상위 15개 action → semantic action 번역기 구현.

이 3개가 되면 "관찰→정책→행동→다음관찰"이 실제로 도는 **학습 환경**이 완성되고,
그 위에 PPO를 얹을 수 있다. 스크립트 봇을 손으로 강하게 만드는 대신, 이
구조에서 **정책이 스스로 강해진다.**

> 관련 문서: 로드맵 [AI_PLAY_BOT_DEVELOPMENT_ROADMAP.md](AI_PLAY_BOT_DEVELOPMENT_ROADMAP.md),
> MVP [AI_PLAY_BOT_MVP_TYRANO_PYTHON.md](AI_PLAY_BOT_MVP_TYRANO_PYTHON.md),
> 명령 매핑 [AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md](AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md).


---

## 부록: 구현 현황 (2026-08-28 기준)

설계는 전부 구현되어 돌아간다. 현재 스택:

- **Feature 531 / Action 52, feature v5** (`ranker_ai_rl_features.{h,cpp}`,
  `kAiRlFeatureCount`/`kAiRlActionCount`): v1 46 = 기본 36 + 연구 3
  (0x14/0x16/0x19 레벨) + 중립몬스터 3 + 테크확장 4; v2 +34 = 연구 10 +
  로스터 11 + 건물 4 + 메커닉 집계 9(스탠스/변신/탑승/군 HP비/기지 침입(800px)/
  진군거리 등); v4 +6 = 교전 비율, 국지 전력비, 부대 목표 one-hot(attack/defend/
  retreat), 저체력 후퇴 비율. v4 에서 `harvest_saturate` 제거(실행기가 매 프레임
  일꾼을 채움). v5 +445 = 8×8 공간 격자 6채널(내 건물/내 부대/적 유닛/적 건물(기억
  포함)/자원/탐색) + 시작 셀 + 방향·거리 벡터 4개(부대→적, 부대→적 건물, 시작→미탐색
  시작 후보, 부대→집) + 적 구성(역할 4·종족 one-hot 4·체력·건설 중·최대 사거리) +
  내 부대 상태(체력 분포 3·산개·유휴·공격 중·원정 비율·역할 3·최대 사거리) + 생산
  파이프라인(유닛 11종 큐·후반 건물 6종 건설 중) + 정찰 5. 관측에 `scout_unit_id`,
  `enemy_building_memory`(owner 별 안개 기억, 펌프 유지) 추가. PPO 은닉 256.
  연구는 `research_next`(고정 순서) 대신 **주문별 액션 13개**(`kAiRlResearchActions`:
  주문·연구 건물·레벨 상한·레벨별 비용) — 정책이 무엇을 연구할지 고른다.
  Python `ranker_rl_env.py` 는 531×52; **r10 체크포인트와
  `imitation_dataset_v3.npz`(80×41)는 v4 와 호환되지 않음 — 재수집/재학습 필요.**
  액션은 전 로스터
  커버 — 벨로시스·람포스·프테라스·트리세스·켄트로스·티라노스·둥가리·
  에그 스로워 생산 + 전 건물 건설 + 연구/사냥/전투. 마스크 게이트 비용은
  게임 데이터 실측치(`ai_techtree_audit.txt` 덤프, 툴팁과 동일 경로).
- **연구 완료 동작 확인**: 재시작-드레인 2종 수정(유휴 연구건물 마스크 게이트
  + 번역기 `select_idle_completed_type`). 번역기는 감사된 전체 연구 트리
  (건물→주문 매핑, 다레벨 스킵)를 순회. `research-diag` 로그로 STARTED/
  COMPLETED 관측 가능.
- **CLI**: `-AISELF -AIRANDOM -AIIMITATE -AIIPC:PORT -AIVS -AIOUT:DIR
  -AINET:N -AITRIBE:N -MAXFRAMES:N -SEED:N`. `-AITRIBE` 0=원시 1=엘프
  2=티라노 3=데몬 4=시드회전(내장 상대 종족; Computer(AI) 자신은 티라노 고정).
- **Python 도구** (`tools/ai/`):
  - `ranker_imitation.py` — 내장 AI 관찰 수집(-AITRIBE:4 기본) + BC
  - `ranker_ppo.py` — PPO(분리 policy/value, value warmup, 성장 보상,
    병렬 롤아웃, 내결함, `--opp-tribe 4` 기본, it-스탬프 체크포인트)
  - `ranker_ipc_server.py` — 온라인 정책 서빙(소유자별 라우팅, -AIVS)
  - `ranker_eval.py` — 고정시드 평가(`--stochastic`, `--opp-tribe`)
  - `ranker_league.py` — 미러 페어 헤드투헤드
  - `ranker_selfplay.py` — **세대 셀프플레이 루프**: 챔피언 복제→-AIVS 양측
    자기대전 PPO(게임당 궤적 2개)→리그 게이트(미러 페어)→승격/기각.
- **배포 주의**: 빌드 산출물은 설치 폴더의 `ranker.exe` **와**
  `ranker_rebuild.exe` 양쪽에 복사할 것 (Python 도구는 후자를 실행).

## 2026-08-30 학습 보상 v4 — 전투 분석 후속 (`tools/ai/ranker_ppo.py::augment_rewards`)

**리그가 전투하지 않은 원인(실측, 40000f 자가대전 13게임):** 양측 `unit_value_lost` 0~1500
(대부분 0~200), 생산의 60~100%가 `produce_worker`(게임당 112~246회), 군대 최대 0~33기, 군대가
있어도 attack/retreat/hunt 목표가 평균 1~2스텝(8~16프레임)마다 뒤집힘(게임당 전환 ~1000회 —
BC 라벨 빈도 hunt:attack:retreat ≈ 2138:1077:656 을 그대로 독립 샘플링). 건물 `render_class`는
전부 1이라 근접 유닛도 건물 공격 가능(`unattackable` 카운터는 원인 아님).

**정정:** 학습에 쓰이는 보상은 게임이 내는 `r`(potential shaping, `w_enemy_army` 시야 감점 포함)이
아니라 Python `augment_rewards` 다. 그쪽에는 처치/파괴 war-score 항이 이미 있었고, 문제는
종료 보상의 절대항 `own_v/5000×0.2·tw` 가 **워커 포함·상한 없음**(246워커 ≈ +5.3, 제거 보너스 +6과
동급)이라 워커 축적이 최적이었다는 점.

**v4 변경:**
- 종료 절대항 → `tanh(전투 군대가치/8000)×0.2·tw`(워커·건물 제외, 상한 0.6). 종료 상대항 →
  자가대전에서는 양측 **군대가치** 차이(`opp_army_value`, selfplay가 같은 게임의 상대 roll 마지막
  상태에서 계산), 내장 AI 상대일 땐 기존 `unit_value` 차이로 폴백.
- **접근 shaping** 추가: Φ = −approach_weight·f[478](부대 중심→가장 가까운 알려진 적 건물 거리/2048,
  미확인이면 1.0), 보상 Φ(s')−Φ(s) **무할인**(γ 사용 시 (γ−1)Φ 드리프트만으로 5000스텝에 +15가
  쌓였음). 합계는 Φ_T−Φ_0 ∈ [−1,1]. attack 스텝은 +, retreat 스텝은 − 가 매 스텝 걸린다.
- war 항·군대가치 성장 항은 그대로. 게임 측 `AiRlRewardConfig`(`w_enemy_army`)는 학습에
  쓰이지 않으므로 손대지 않음.

**검증(복사한 리그 에피소드에 오프라인 적용):** w0 owner2(워커 246, 군대 0) 종료항 5.28→0.00;
w8 owner1(처치 7700) war=+38.5 로 전투가 압도. 리그 재시작: `selfplay_v7b.log`.
미해결(사용자 결정 대기): 목표 요동(부대 목표 최소 유지/no_op 재정의), 워커 상한.
