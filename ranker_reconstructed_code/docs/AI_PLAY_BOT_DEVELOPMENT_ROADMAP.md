# AI Play 봇 개발 로드맵 (초안)

## 1. 목표

장기 목표는 사람이 화면을 보고 판단하듯 현재 전황을 관찰하고, 생산·건설·이동·전투 명령을 스스로 선택하며, 반복 학습을 통해 실력이 향상되는 AI Play 봇을 만드는 것이다. 스타크래프트의 AlphaStar와 비슷한 방향을 지향하지만, 첫 구현부터 대규모 강화학습을 시도하지는 않는다.

첫 번째 실용 목표(MVP)는 다음과 같이 작게 잡는다.

- `ranker_rebuild.exe`의 1대1 게임에서 한 개의 봇 슬롯을 운용한다.
- 우선 한 개 맵, 한 개 진영, 한 개 게임 모드만 지원한다.
- 봇은 화면 픽셀이나 마우스 좌표가 아니라 구조화된 게임 상태를 관찰한다.
- 봇의 행동은 사람이 만든 것과 같은 정상 gameplay command/packet 경로로 실행한다.
- 고정된 맵과 RNG seed에서 게임을 재현하고 결과를 리플레이로 다시 검증할 수 있다.
- 최초의 봇은 학습형 모델이 아니라 단순 규칙 기반 봇이어도 된다.

원본 `ranker.exe`는 분석과 호환성 검증 대상으로만 사용한다. 봇 기능이나 계측 코드는 `ranker.exe`에 추가하지 않는다.

현재 확정된 첫 구현 범위와 기준 리플레이 분석은 [AI Play 봇 MVP: 티라노 / Python](AI_PLAY_BOT_MVP_TYRANO_PYTHON.md)에 기록한다.

## 2. 먼저 알아야 할 점

AlphaStar 같은 봇은 하나의 신경망만 만들어서 완성되는 기능이 아니다. 최소한 다음 네 부분이 먼저 필요하다.

1. 게임 상태를 안정적으로 읽는 관찰 인터페이스
2. AI의 결정을 정상 게임 명령으로 바꾸는 행동 인터페이스
3. 수천~수백만 게임을 자동 실행하고 초기화할 학습 환경
4. 데이터를 수집하고 모델을 학습·평가하는 파이프라인

따라서 이 프로젝트에서는 “모델 선택”보다 “게임과 AI 사이의 안정된 인터페이스”를 먼저 만드는 것이 중요하다.

## 3. 현재 코드에서 활용할 수 있는 기반

| 기존 영역 | 관련 코드 | AI 봇에서의 용도 |
|---|---|---|
| 원작 규칙 기반 Owner AI | `ranker_owner_ai.*` | 기준 상대와 초기 규칙 봇, 게임 규칙 조사에 활용 |
| 플레이어/자원 상태 | `ranker_player_slots.*` | 자원, 플레이어 관계, 활성 슬롯 관찰 |
| 유닛과 이동 상태 | `ranker_unit_movement.*` | 유닛 종류, 소유자, 위치, 체력, 명령, 이동 상태 관찰 |
| 생산 규칙 | `ranker_production_orders.*`, `ranker_gameplay_production_actions.*` | 생산 가능 여부와 비용 계산, 불가능한 행동 마스킹 |
| 입력 행동 모델 | `ranker_gameplay_input_actions.*` | 사람의 입력이 의미 있는 gameplay action으로 변환되는 규칙 재사용 |
| 동기화 명령 | `ranker_gameplay_packets.*`, `ranker_reliable_packets.*` | AI 행동을 정상 P2P 명령으로 발행 |
| 리플레이 | `ranker_replay.*` | 사람/봇 경기 명령 저장, 재현, 학습 데이터 원본 보관 |
| 동기화 진단 | `ranker_p2p_flight_recorder.*`, `tools/replay_debug` | 봇 경기에서 발생한 첫 동기화 차이 분석 |

현재의 `Owner AI`는 원작에서 복원한 스크립트/규칙 기반 시뮬레이션 로직이다. 앞으로 만들 학습형 정책(policy)과는 별개의 기능이다. 다만 초기 상대, 규칙 기반 기준선, 생산 및 공격 로직의 참고 자료로 사용할 수 있다.

## 4. 권장 구조

```text
게임 시뮬레이션 상태
        ↓
Observation Builder (AI에게 허용된 정보만 추출)
        ↓
Policy (처음에는 규칙, 이후 모방학습/강화학습 모델)
        ↓
Semantic Action Validator (가능 여부와 행동 예산 검사)
        ↓
Gameplay Packet Adapter (기존 명령 패킷으로 변환)
        ↓
기존 reliable packet/replay 처리
        ↓
모든 피어에서 동일한 시뮬레이션 실행
```

AI 연구 코드는 Python으로 시작하는 편이 편리하다. 게임 내부에는 C++로 관찰/행동 어댑터를 만들고, 초기에는 named pipe나 shared memory 같은 로컬 IPC로 Python 정책과 연결할 수 있다. 모델을 최종 실행 파일 안에 포함할지, 별도 봇 프로세스로 유지할지는 환경과 성능을 측정한 뒤 결정한다.

### P2P 동기화 원칙

학습 모델은 운영체제, CPU, 라이브러리에 따라 미세하게 다른 결과나 실행 시간을 낼 수 있다. 따라서 live P2P에서 같은 모델을 모든 피어가 각각 실행하고 시뮬레이션 상태를 직접 수정하게 해서는 안 된다.

- 하나의 지정된 봇 controller만 해당 봇 슬롯의 결정을 만든다.
- 결정 결과는 기존의 ordered gameplay packet으로 발행한다.
- 다른 피어는 모델을 실행하지 않고 같은 패킷만 처리한다.
- AI는 유닛, 자원, RNG 또는 효과 상태를 직접 변경하지 않는다.
- 판단은 고정된 simulation frame 간격에서 수행하고, 제한 시간 안에 결과가 없으면 `no-op` 또는 이전 안전 행동을 사용한다.
- 리플레이에는 모델 출력이 아니라 실제로 처리된 gameplay packet이 남아야 한다. 그러면 모델 없이도 경기를 재생할 수 있다.

봇 슬롯의 명령을 어느 피어가 소유하고 relay가 그 채널을 어떻게 취급할지는 별도의 작은 호환성 실험으로 먼저 확정해야 한다. 기존 `PublishLocalMode1GameplayPacket` 경로를 우회해 가짜 상태 변경을 만드는 방식은 사용하지 않는다.

## 5. 단계별 개발 순서

### 0단계: 범위와 성공 조건 고정

처음부터 모든 맵, 진영, 모드, 다대다 게임을 지원하면 디버깅과 학습이 매우 어려워진다.

먼저 다음을 정한다.

- 첫 대상 게임 모드, 맵, 진영과 상대
- 봇이 사용할 수 있는 정보의 범위
- 판단 주기와 초당 최대 명령 수(APM 제한)
- 승률 외 평가 지표: 게임 시간, 생산 정체 시간, 자원 유휴량, 불법 행동 비율 등
- 목표: 예를 들어 규칙 기반 Owner AI를 100개 고정 seed 중 60개 이상에서 이기기

완료 조건은 한 페이지 분량의 고정된 MVP 사양서가 만들어지는 것이다.

### 1단계: 게임 규칙과 명령 목록 정리

AI가 사용할 최소 행동을 먼저 열거한다.

- 대기
- 이동, 공격 이동, 특정 유닛 공격
- 자원 채집과 복귀
- 유닛 생산과 취소
- 건물 건설과 취소
- 집결지 설정
- 스킬/특수 행동
- 항복

각 행동에 대해 필요한 unit id, target id, 좌표, 생산 id, packet subtype과 유효성 규칙을 표로 만든다. 복원 코드만으로 의미가 불분명한 명령은 리플레이와 원본 `ranker.exe`를 비교하고, 필요할 때만 Ghidra MCP로 원본 구현을 확인한다.

완료 조건은 “의미 있는 AI 행동 → 기존 packet 필드”의 매핑과 작은 회귀 테스트가 존재하는 것이다.

### 2단계: Observation API 구축

렌더링 자료구조와 분리된 읽기 전용 `AiObservation` 스냅샷을 만든다. 첫 버전에는 다음 정보만 포함한다.

- simulation frame, 맵 id/크기, 플레이어/진영 id
- 자신의 자원, 인구, 생산 제한과 현재 생산 목록
- 자신의 유닛: 안정된 id, 종류, 위치, 체력, 현재 명령, 생산/건설 상태
- 현재 시야에 보이는 적과 중립 유닛
- 탐사된 지형, 현재 시야, 이동 가능 영역의 축약 표현
- 선택 가능한 행동과 대상의 legal-action mask

다음 정보는 주의해서 제외한다.

- C++ pointer 주소나 실행할 때마다 달라지는 값
- 화면 표시 전용 상태
- 사람 플레이어가 볼 수 없는 적의 현재 위치, 생산, 자원
- 미래 RNG 결과
- P2P 진단용 full-state checksum이나 숨겨진 효과 정보

관찰 schema에는 버전 번호를 둔다. 같은 frame과 seed를 두 번 실행했을 때 byte-for-byte 같은 관찰이 생성되는지 테스트한다.

### 3단계: Semantic Action API 구축

모델이 마우스 클릭을 직접 흉내 내는 대신 다음과 같은 의미 단위 행동을 출력하게 한다.

```text
NoOp
Move(unit_ids, world_x, world_y)
AttackUnit(unit_ids, target_id)
AttackMove(unit_ids, world_x, world_y)
Harvest(unit_ids, resource_or_point)
ProduceUnit(structure_id, unit_type)
Research(structure_id, research_order)
Build(builder_id, structure_type, world_x, world_y)
SetRally(structure_ids, target_or_point)
CancelProduction(structure_id, latest)
UseAbility(unit_ids, ability_id, target_or_point)
```

Action API는 다음 순서로 처리한다.

1. 소유권, 시야, 비용, cooldown, 지형, 생산 조건을 검사한다.
2. 불가능한 행동은 게임 상태를 바꾸지 않고 명확한 사유 코드로 거절한다.
3. 가능한 행동만 기존 `GameplayPublishedAction`과 mode-1 packet으로 변환한다.
4. 실제로 발행된 frame, packet, 결과를 episode log에 남긴다.

대규모 유닛을 모두 독립적으로 조작하는 action space는 너무 크다. 초기에는 부대 단위 선택, 제한된 후보 target, 좌표 격자화, macro와 micro 정책 분리 등을 사용한다.

완료 조건은 모델 없이 만든 테스트 controller가 기본적인 생산→이동→공격 경기를 정상 packet 경로로 수행하고, 그 리플레이가 같은 결과로 재생되는 것이다.

### 4단계: 자동 실행 환경 구축

학습에 필요한 최소 환경 API는 일반적인 `reset()`/`step()` 형태로 만든다.

- `reset(map, players, factions, seed)` → 첫 observation
- `step(action)` → 다음 observation, reward, 종료 여부, 진단 정보
- 고정된 decision interval만큼 simulation frame을 전진
- 승리, 패배, 무승부, timeout 판정
- 렌더링/음향을 끌 수 있는 빠른 실행 모드
- 경기 종료 후 모든 runtime global과 임시 자원을 완전히 초기화

처음에는 한 프로세스에서 한 경기만 안정적으로 반복한다. 그 다음 여러 프로세스로 병렬 실행한다. 불안정한 한 프로세스 안에서 여러 게임을 동시에 돌리는 구조는 나중 문제로 미룬다.

필수 검증은 같은 seed와 같은 action sequence가 같은 관찰, checksum, 종료 결과를 만드는지 확인하는 것이다.

### 5단계: 학습 전 기준 봇 만들기

학습형 AI의 성능을 판단하려면 비교 대상이 필요하다.

다음 세 봇을 먼저 만든다.

1. `RandomLegalBot`: legal-action mask 안에서만 임의 행동
2. `ScriptedBot`: 정해진 빌드 순서, 자원 채집, 단순 공격
3. 기존 Owner AI를 사용하는 기준 상대

이 단계에서 observation/action API, 종료 조건, 통계, replay 저장이 실제 게임 전체에서 동작하는지 검증한다. `ScriptedBot`이 완주하지 못한다면 학습 모델을 시작하기에는 아직 이르다.

### 6단계: 데이터 수집 파이프라인 구축

사람 경기와 규칙 봇 경기에서 episode dataset을 만든다. 각 episode에는 다음을 저장한다.

- 실행 파일 hash, observation/action schema 버전, 데이터 버전
- 맵, 모드, 진영, 플레이어, RNG seed
- 각 decision frame의 observation, legal-action mask, 선택 action
- 실제 발행 packet과 적용 결과
- reward, 승패와 종료 사유
- 원본 `.ply` 리플레이 경로 또는 식별 hash

`.ply`는 명령 재현의 기준 자료로 유지한다. `P2PDrop_*.sync.csv`는 동기화 원인 분석용 full-state flight trace이므로 일반 학습 입력으로 사용하지 않는다. 이를 학습에 넣으면 시야 밖 정보가 섞이고 데이터 목적도 혼동된다.

사람의 기존 리플레이에서 semantic action을 완전히 복원할 수 있는지도 여기서 검증한다. 복원할 수 없는 명령은 dataset에서 표시하거나 제외하고 조용히 다른 행동으로 바꾸지 않는다.

### 7단계: 모방학습으로 첫 모델 만들기

사람 또는 좋은 규칙 봇의 `(observation, action)` 기록으로 행동 모방(behavior cloning) 모델을 학습한다. 강화학습보다 먼저 모방학습을 권장하는 이유는 다음과 같다.

- 처음부터 무작위 행동으로 긴 RTS 게임을 학습하는 비용이 매우 크다.
- 최소한의 생산 순서와 조작 습관을 빠르게 배울 수 있다.
- observation/action encoding의 오류를 작은 데이터로 찾기 쉽다.

학습/검증/평가 데이터는 match 단위로 분리한다. 같은 경기의 인접 frame이 양쪽에 섞이면 평가 점수가 과장된다.

첫 모델의 목표는 강한 실력이 아니라 `ScriptedBot`과 비슷한 빈도로 유효한 명령을 내고 경기를 끝까지 수행하는 것이다.

### 8단계: 강화학습과 self-play 추가

모방학습 모델을 초기 정책으로 사용해 강화학습을 시작한다.

- 최종 승패 보상을 중심으로 둔다.
- 자원, 유닛 가치, 탐색 등 중간 보상은 작게 두고 악용 가능성을 검사한다.
- 짧은 전투, 생산 과제, 제한된 맵부터 시작하는 curriculum을 사용한다.
- 현재 모델끼리만 싸우지 말고 규칙 봇과 과거 모델 snapshot을 상대 pool에 보관한다.
- 일정 간격으로 frozen evaluation set에서만 실력을 판정한다.

AlphaStar 수준의 league training, 다중 정책, 대규모 분산 actor는 이 기본 파이프라인이 안정된 뒤 추가한다.

### 9단계: 게임 내 봇 슬롯과 P2P 통합

학습과 single-player 실행이 안정된 후 live P2P에 연결한다.

- lobby에서 봇 슬롯과 봇 난이도/모델 버전을 명시한다.
- 봇 controller의 명령 권한과 packet channel 소유권을 한 곳에만 둔다.
- decision frame, 최대 추론 시간, timeout 행동을 고정한다.
- 모델 오류나 IPC 종료 시 안전하게 `no-op`하거나 봇을 항복 처리한다.
- 모델 파일 hash와 설정을 경기 metadata에 남긴다.
- 봇 명령이 사람 명령과 똑같이 `.ply`에 기록되는지 확인한다.

학습 모델을 모든 피어에서 동시에 실행하는 구조가 꼭 필요하다면, 정수/고정소수점 inference와 실행 순서까지 결정론을 입증해야 한다. 이는 초기 목표로 잡지 않는다.

### 10단계: 평가와 회귀 방지

모델 성능과 실행 안정성을 별도로 평가한다.

AI 품질 지표:

- 고정 seed/맵/상대별 승률과 신뢰구간
- 평균 게임 시간과 timeout 비율
- 불가능하거나 거절된 action 비율
- 자원 유휴량, 생산 중단 시간, 유닛 손실 대비 피해량
- 분당 action 수와 같은 action 반복 비율

엔진 안정성 지표:

- 동일 seed/action의 deterministic replay 성공 여부
- 봇 경기 `.ply`의 원본/재구축 실행 상태 일치 여부
- 두 P2P 피어의 frame checksum 일치 여부
- policy inference 시간의 평균과 상위 percentile
- 장시간 반복 경기의 메모리/handle 증가 여부

동기화가 어긋나면 기존 절차대로 `RankerOCPV_Win/Replays/P2PDrop_*.ply`와 `.sync.csv`를 보존하고, 원본 `ranker.exe`와 `ranker_rebuild.exe`에서 같은 리플레이의 최초 분기 frame을 비교한다.

## 6. 권장 구현 단위

실제 구현을 시작할 때는 기존 대형 파일에 AI 코드를 계속 추가하지 말고 다음과 같이 역할을 나눈다. 아래 이름은 제안이며 구현 과정에서 바꿀 수 있다.

```text
ranker_reconstructed_code/
  include/ranker_ai/
    ranker_ai_observation.h
    ranker_ai_action.h
    ranker_ai_controller.h
    ranker_ai_episode.h
  src/ranker_ai/
    ranker_ai_observation.cpp
    ranker_ai_action.cpp
    ranker_ai_controller.cpp
    ranker_ai_episode.cpp
  tools/ai/
    protocol/
    trainer/
    dataset/
    evaluation/
  tests/ai/
```

게임 C++ 코드와 Python 학습 코드가 공유하는 observation/action protocol은 명시적으로 versioning한다. 성능 때문에 나중에 JSON에서 binary 형식으로 바꾸더라도 의미와 호환 규칙은 유지한다.

## 7. 주요 위험과 대응

| 위험 | 대응 |
|---|---|
| 아직 복원이 덜 된 게임 동작이 학습 중 자주 노출됨 | AI 작업과 엔진 parity 문제를 분리하고 최소 replay 회귀 테스트를 남김 |
| AI가 시야 밖 정보를 사용해 부정한 플레이를 함 | Observation Builder에서 정보 경계를 강제하고 테스트함 |
| action space가 지나치게 큼 | semantic action, 후보 target, 좌표 격자, 계층형 정책 사용 |
| 모델 출력이 P2P 동기화를 깨뜨림 | 단일 controller가 기존 ordered packet만 발행하도록 제한 |
| 학습 속도가 너무 느림 | 렌더링/음향 없는 빠른 모드, 짧은 과제, 이후 multi-process 병렬화 |
| 보상 점수만 올리고 실제로는 이상하게 플레이함 | 승패 중심 보상, 행동 로그와 replay 샘플 검토, 다양한 평가 상대 사용 |
| 모델/데이터 버전이 섞여 결과를 재현할 수 없음 | executable/model/schema hash와 seed를 episode마다 기록 |

## 8. 첫 번째 개발 이터레이션

바로 다음 구현에서는 학습 모델을 만들지 않고 아래까지만 진행하는 것을 권장한다.

1. 1대1 단일 맵 MVP 사양을 확정한다.
2. 이동, 공격, 생산, 건설의 semantic action과 packet 매핑을 문서화한다.
3. 한 플레이어 관점의 읽기 전용 `AiObservation`을 한 frame에서 추출한다.
4. `RandomLegalBot`이 정상 packet 경로로 명령을 내리게 한다.
5. 같은 seed의 한 경기를 두 번 실행해 observation hash, 처리 packet, 최종 결과가 같은지 확인한다.
6. 생성된 `.ply`를 모델 없이 재생해 같은 게임 결과가 나오는지 확인한다.

이 이터레이션이 통과하면 AI 학습에 필요한 토대가 생긴다. 그 다음 `ScriptedBot`, episode dataset, 모방학습 순서로 확장하는 것이 가장 안전하다.

## 9. 현재 진행 위치 (2026-08-25)

전체 `Debug_replays` 33개 command inventory를 기준으로 첫 기반 코드를 추가했다.

- `AiObservation` schema v1, snapshot builder와 deterministic hash: 완료
- 봇 제어 owner 관점의 fog gate와 적 private-state redaction: 완료
- 현재 visible인 map cell의 harvest 잔량 관찰과 hidden-resource redaction: 완료
- `NoOp`, `Move`, `AttackMove`, `AttackUnit`, `Harvest` 검증 및 subtype `0x02` packet 계획: 완료
- 전용 `ai_play_interface_regression`: 통과
- 로비 `Computer(AI)` 슬롯과 티라노 `ScriptedBot`, ordered gameplay packet 발행 연결: 첫 버전 완료
- 티라노 `ProduceUnit`, `Research`, `Build`, `SetRally`, `CancelProduction(latest)`: replay와 handler/catalog 대조 및 packet planner 완료
- live production/placement validator callback 연결: 완료
- `gameplay_1.ply`의 건설·생산·연구 순서를 완료 조건 기반 macro와 실패 backoff로 변환: 첫 버전 완료

현재 controller는 게임 방의 빈 슬롯을 `Computer(AI)`로 지정하면 활성화된다. 해당 슬롯은 티라노로 고정되며, 기존 Computer와 같은 세션 상태값을 사용하되 그 owner의 내장 Owner AI만 중지한다. 새 AI의 별도 ordered packet 채널은 로컬 시뮬레이션 pump가 처리한다. replay와 다중 P2P 세션에서는 새 controller를 실행하지 않는다. 현재 정책은 Nest 복구, rally 지정, visible 베리 탐지, 최대 4기 Dinos의 지속 채집, Dinos 생산, replay 기반 Nest·EggNest·LandNest 확장, Masos·Dilophos 생산, 세 연구, 보이는 적 공격과 맵 탐색까지 지원한다. 채집·복귀 중인 Dinos는 공격 부대에서 제외한다. 구조물 완성 및 생산 queue를 확인해 다음 목표로 진행하며, 실패한 macro action은 deterministic backoff 후 다시 시도한다.

따라서 다음 순서는 Python 맵 한 경기 자동 완주와 저장 replay 재생을 검증하고, 실제 경기에서 드러난 막힘·배치·종료 조건을 보완하는 작업이다.
