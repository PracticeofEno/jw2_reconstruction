# AI Play 봇 명령 카탈로그

## 1. 목적

이 문서는 `RankerOCPV_Win/Replays/Debug_replays`의 모든 리플레이에서 실제로 처리된 명령을 AI 행동 설계에 활용하기 위한 기준이다.

리플레이의 raw packet 조합은 유용한 실행 증거지만 그 자체가 AI의 action space는 아니다. 같은 command 값도 subtype, source unit type, capability, mode, target 종류에 따라 의미가 달라질 수 있다. 따라서 다음 두 자료를 함께 사용한다.

1. 전체 리플레이에서 관찰한 packet 조합
2. 현재 runtime definition과 기존 gameplay validator가 허용하는 조합

AI에는 raw command 번호를 직접 선택하게 하지 않고, 검증된 semantic action만 제공한다.

## 2. 전체 리플레이 인벤토리

2026-08-25 현재 분석 범위는 다음과 같다.

- 디렉터리: `RankerOCPV_Win/Replays/Debug_replays`
- 리플레이: 33개
- 전체 크기: 12,711,652 bytes
- ordered gameplay packet: 366,551개
- frame 범위: 10~51,918
- 관찰된 subtype/command/queued 조합: 160개
- 모든 replay packet stream의 trailing bytes: 0

전체 raw 값과 리플레이별 사용 현황은 다음 자동 생성 자료가 권위 목록이다.

- `tools/gameplay_parity/reports/debug_replay_command_inventory.md`
- `tools/gameplay_parity/reports/debug_replay_command_inventory.json`

기존 31개 리플레이 보고서에 `error2.ply`와 `gameplay_1.ply`를 포함해 다시 생성하면서 다음 여섯 조합이 추가되었다.

| Subtype | Command | Queued |
|---|---|---|
| `0x01` | `0x34` | 아니요 |
| `0x01` | `0x35` | 아니요 |
| `0x01` | `0x3a` | 아니요 |
| `0x09` | `0x12` | 아니요 |
| `0x0c` | `0x27` | 아니요 |
| `0x0f` | `0x00` | 아니요 |

## 3. Subtype별 관찰 범위와 AI 분류

| Subtype | 의미 | Packet | 조합 | AI Action 분류 |
|---|---|---:|---:|---|
| `0x00` | no-op marker | 2 | 1 | 시스템; policy에 노출하지 않음 |
| `0x01` | unit production/refund | 10,024 | 50 | 생산·취소 semantic action 후보 |
| `0x02` | basic unit order | 339,977 | 24 | 이동·공격·작업의 핵심 action |
| `0x07` | unit death mark | 38 | 1 | 시뮬레이션/시스템; policy에 노출하지 않음 |
| `0x08` | unit auxiliary vector | 5,222 | 1 | `SetRally` 같은 검증된 action을 통해 사용 |
| `0x09` | extended unit order | 812 | 17 | 유닛별 특수 능력 action 후보 |
| `0x0a` | forced order `0x21` | 113 | 1 | 의미 action의 어댑터 출력으로만 허용 |
| `0x0b` | unit status mask | 9,039 | 1 | 의미 action의 보조 packet으로만 허용 |
| `0x0c` | placement resource/refund | 638 | 46 | 건설·생산·취소 semantic action 후보 |
| `0x0f` | catch-up target | 7 | 2 | 동기화 시스템; policy에 노출하지 않음 |
| `0x13` | player inactive | 70 | 1 | 명시적 `Surrender`만 action으로 제공 |
| `0x16` | modal pause | 2 | 2 | UI/동기화 제어; 전략 policy에서 제외 |
| `0x1a` | production-cost order/refund | 557 | 9 | 연구·업그레이드·취소 action 후보 |
| `0x1d` | vote completion | 50 | 4 | 종료 합의 시스템; policy에 노출하지 않음 |

리플레이에 나타나지 않은 subtype이나 command가 곧 불법이라는 뜻은 아니다. 장비, 특수 능력, 시나리오 명령처럼 이 33경기에서 사용되지 않은 정상 경로가 있을 수 있다. 반대로 리플레이에 나타난 시스템 packet을 AI가 직접 발행할 수 있다는 뜻도 아니다.

## 4. `13.ply`의 역할

`13.ply`는 사람 대 사람 조작의 중요한 참고 자료다.

- 파일 크기: 682,129 bytes
- SHA-256: `4A0D73624FF2CA35303912BF70E23445F3B69C4A89AF67CD809AF6BC2CE6E4E8`
- 맵 크기: 128×128
- 진영: owner 0과 owner 1 모두 Primitive(tribe id 0)
- 두 슬롯 모두 human 상태
- frame 범위: 24~17,815
- packet: 15,366개
- owner 0 packet: 9,317개
- owner 1 packet: 6,049개
- 관찰 조합: 28개

`gameplay_1.ply`와 비교하면 `13.ply`에는 18개 조합이 더 있다. 특히 다음 항목을 확인하는 데 유용하다.

- 양쪽 human owner가 발행한 명령 순서
- command `0x04`와 `0x05`의 queued 사용
- subtype `0x1a` production-cost 명령
- subtype `0x02` command `0x12` 특수 flag 명령
- 더 다양한 subtype `0x01`과 `0x0c` 생산/배치 값

그러나 `13.ply`는 티라노 경기가 아니므로 티라노의 빌드 순서나 production id를 학습하는 기준으로 사용하지 않는다. 진영에 독립적인 이동, 공격, action batching, queued 명령과 사람의 명령 간격을 추출하는 자료로 사용한다.

## 5. `gameplay_1.ply`와의 상호 보완

`gameplay_1.ply`는 packet 수와 raw 조합 수는 적지만 첫 MVP와 정확히 같은 티라노/Python 조건이다.

- packet: 1,612개
- 관찰 조합: 20개
- `13.ply`에 없는 조합: 10개
- 사람 owner 0의 티라노 조작과 기존 Computer owner 1의 상대 동작

특히 `gameplay_1.ply`의 subtype `0x01` command `0x20`, `0x21`, `0x24`와 subtype `0x0c` command `0x14`, `0x16`, `0x19`는 `13.ply`에 없다. 따라서 자료의 역할을 다음처럼 나눈다.

| 목적 | 우선 자료 |
|---|---|
| 티라노/Python 생산·건설 순서 | `gameplay_1.ply` |
| 사람 대 사람의 이동·전투 timing | `13.ply` 및 나머지 human replay |
| 전체 raw command coverage | `Debug_replays` 33개 전체 |
| legal action 판정 | 현재 runtime definition과 validator |
| P2P packet 실행 결과 | 원본/재구축 replay parity 비교 |

티라노/Python 리플레이의 생산·건설·연구·rally·취소 payload는 packet handler와 catalog reference까지 대조해 [`AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md`](AI_PLAY_BOT_GAMEPLAY1_SEMANTIC_MAPPING.md)에 별도로 확정했다.

## 6. 올바른 명령 조합 키

AI 행동 카탈로그는 `(subtype, command)` 두 값만으로 만들지 않는다. 최소한 다음 정보를 함께 기록한다.

```text
SemanticActionKey
  action_kind
  packet_subtype
  normalized_command
  queued
  source_owner
  source_unit_type
  source_capability
  target_kind (none / point / unit / resource / producer)
  target_unit_type_or_class
  mode_semantics
  production_or_ability_id
  faction
```

리플레이 packet에는 source unit offset만 들어 있으므로 source unit type과 당시 capability를 알려면 해당 replay를 실행하면서 packet 직전의 simulation state와 정렬해야 한다. 이 정렬 없이 command 번호에 임의의 이름을 붙이면 서로 다른 유닛 행동을 하나로 잘못 합칠 수 있다.

## 7. Semantic Action 구성 순서

전체 리플레이는 다음 순서로 AI Action API에 반영한다.

1. 33개 리플레이의 raw packet 인벤토리를 자동 생성한다.
2. 각 replay를 실행하며 packet 직전 source unit, target, capability, 자원 상태를 기록한다.
3. 같은 사용자 의도에서 여러 유닛별로 생긴 packet을 한 semantic action으로 묶는다.
4. 현재 코드의 production/action validator로 합법 여부를 다시 판정한다.
5. 의미가 확인된 action만 `Move`, `AttackMove`, `AttackUnit`, `Harvest`, `ProduceUnit`, `Research`, `Build`, `SetRally`, `UseAbility`, `CancelProduction` 등에 연결한다.
6. 시스템 및 보조 packet은 Action Adapter가 자동 생성하게 한다.
7. source unit type/target 종류별 replay 회귀 테스트를 추가한다.

## 8. 학습 데이터 사용 원칙

- episode는 replay와 owner별로 분리한다.
- 같은 replay의 owner 0/1 데이터가 학습과 평가 양쪽에 나뉘지 않게 한다.
- 여러 유닛에 같은 명령을 내린 packet 묶음을 한 action으로 복원한다.
- 시야 밖 적 정보와 full-state flight recorder 값은 observation에서 제외한다.
- 진영 공통 micro policy와 진영별 macro/production policy의 dataset을 구분한다.
- command 의미를 확정하지 못한 sample은 `unknown`으로 남기고 다른 action으로 추측 변환하지 않는다.

전체 33개 리플레이는 명령 coverage와 모방학습 초기 자료로 사용하되, 최종 legal action 목록은 게임 runtime의 실제 capability와 validator가 결정한다.
