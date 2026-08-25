# `gameplay_1.ply` 티라노 AI Action 매핑

## 1. 분석 기준

- 리플레이: `RankerOCPV_Win/Replays/Debug_replays/gameplay_1.ply`
- 리플레이 SHA-256: `0B3A2721E1B9F5CE88457AEE68F8BCBBAA962523EFDC9A8398E98FC2470C815F`
- 맵: `RankerOCPV_Win/Maps/Rank Maps/(4) Python Jurassic v0.1.trk`
- 맵 SHA-256: `C2D4E81FF81FB189BADE261ED20B7EA881ABC9D6B8D67ADF01244C693E637453`
- 플레이어: owner 0 Tyrano 대 owner 1 Tyrano Computer

의미는 packet 번호만 보고 추측하지 않았다. 다음 자료를 함께 대조했다.

1. 리플레이의 frame, subtype, command, unit offset, 세 payload
2. 현재 gameplay publisher와 packet handler의 wire field 배치
3. `Jw2_09.trc`에서 추출한 unit 이름, 생산 reference와 건설 reference
4. `Jw2_10.trc`에서 추출한 production-order 이름과 비용
5. 같은 source offset이 경기 중 반복해서 발행한 생산·연구·rally 조합

## 2. 베리 채집: subtype `0x02`, command `0x07`

Python 맵의 베리는 별도 unit이 아니라 record 12 map cell의
`kMapCellHarvestAmountMask`에 잔량이 저장된다. 집중 검증 replay
`(2) GP Harvest.ply`와 `tools/gameplay_parity/harvest_manifest.json`은 Dinos를
포함한 네 진영 일꾼이 다음 point 명령으로 실제 자원을 채집하는 것을 확인한다.

```text
subtype      = 0x02
command      = 0x07 | queued_bit
unit_offset  = worker runtime pool offset
target       = 0 for a berry tile
arg1         = resource tile center world_x
arg2         = resource tile center world_y
```

AI의 point `Harvest`는 현재 owner 시야에 보이고, 이동 가능하며, 잔량이 0보다
큰 cell만 허용한다. 이 검사는 hidden-resource probing을 막기 위해 packet 발행
직전 live map과 owner visibility를 다시 대조한다.

## 3. 유닛 생산: subtype `0x01`

일반 생산 packet은 다음 형태다.

```text
subtype      = 0x01
command      = produced_unit_type
unit_offset  = producer runtime pool offset
mode         = 0
arg1         = 0
arg2         = 0
```

`gameplay_1.ply`에서 확인된 티라노 생산은 다음과 같다.

| command | 생산 유닛 | 생산 구조물 | source offset | packet 수 |
|---:|---|---|---|---:|
| `0x20` | Dinos, type 32 | TyranoNest, type 128 | `0x0000fbf0` | 28 |
| `0x21` | Masos, type 33 | EggNest, type 132 | `0x000125a0`, `0x00012eb0`, `0x00014470` | 40 |
| `0x24` | Dilophos, type 36 | EggNest, type 132 | 위 EggNest 3개 | 6 |

구조물의 `alternate_references`도 이 관계를 확인한다.

- TyranoNest: `32, 44`
- EggNest: `33, 34, 36, 46, 40, 37, 39`

따라서 AI의 `ProduceUnit(producer_id, unit_type)`은 subtype `0x01`로 변환한다. producer의 queue, 자원, 인구, prerequisite와 reference는 live validator가 다시 검사해야 한다.

## 4. 건설: subtype `0x02`, command `0x06`

건설은 subtype `0x01`이나 `0x0c`가 아니라 일반 unit-order packet을 사용한다.

```text
subtype      = 0x02
command      = 0x06 | queued_bit
unit_offset  = builder runtime pool offset
mode         = building_type - 0x60
arg1         = world_x aligned down to 32 pixels
arg2         = world_y aligned down to 32 pixels
```

`gameplay_1.ply`의 11개 건설 packet은 다음과 같다.

| wire mode | 실제 건물 type | 이름 | packet 수 |
|---:|---:|---|---:|
| `0x20` | `0x80` / 128 | TyranoNest | 1 |
| `0x22` | `0x82` / 130 | Nest | 6 |
| `0x24` | `0x84` / 132 | EggNest | 3 |
| `0x25` | `0x85` / 133 | LandNest | 1 |

이 네 건물은 Dinos의 `primary_references`에 모두 들어 있다. AI의 `Build`는 좌표 정렬 후에도 original placement predicate와 자원·prerequisite 검사를 통과해야만 packet을 계획한다.

## 5. Rally: subtype `0x08`, command `0x1f`

Rally packet은 다음 형태다.

```text
subtype      = 0x08
command      = 0x1f
unit_offset  = producer runtime pool offset
target       = target unit offset, point rally이면 0
arg1         = rally world_x
arg2         = rally world_y
```

리플레이의 58개 rally는 모두 point rally라 target offset이 0이다.

| 구조물 | source offset | packet 수 |
|---|---|---:|
| TyranoNest | `0x0000fbf0` | 37 |
| EggNest | `0x000125a0`, `0x00012eb0`, `0x00014470` | 21 |

현재 `SetRally` planner는 여러 생산 구조물을 안정된 id 순서로 packet화한다. target unit을 사용할 때는 현재 관측에서 보이는 target만 허용한다.

## 6. 연구: subtype `0x0c`

Subtype `0x0c`는 건설 packet이 아니라 production-order, 즉 연구·업그레이드 queue다.

```text
subtype      = 0x0c
command      = production_order_id
unit_offset  = research structure runtime pool offset
mode         = calculated secondary-resource cost
arg1         = 0
arg2         = 0
```

`gameplay_1.ply`의 세 연구는 모두 secondary cost가 0이다.

| order | 이름 | 연구 구조물 | source offset | frame |
|---:|---|---|---|---:|
| `0x14` / 20 | 베리 채집량 증가 | TyranoNest | `0x0000fbf0` | 3932 |
| `0x19` / 25 | 티라노 지상 유닛 공격 업 | LandNest | `0x00017e70` | 6317 |
| `0x16` / 22 | 벨로시스, 딜로포스 이동 속도 증가 | LandNest | `0x00017e70` | 7583 |

구조물의 `completion_references`도 이를 확인한다.

- TyranoNest: order `20, 42, 56, 43`
- LandNest: order `25, 26, 22`

## 7. replay 기반 첫 빌드 순서

건설과 연구 packet을 프레임 순서로 정렬하면 핵심 진행은 다음과 같다.

| frame | 행동 | 누적 구조물/연구 |
|---:|---|---|
| 694 | Nest 건설 | Nest 1 |
| 1471 | EggNest 건설 | EggNest 1 |
| 1778 | EggNest 건설 | EggNest 2 |
| 2660 | Nest 건설 | Nest 2 |
| 3132 | EggNest 건설 | EggNest 3 |
| 3557 | Nest 건설 | Nest 3 |
| 3932 | order `0x14` 연구 | 채집량 증가 요청 |
| 4679 | Nest 건설 | Nest 4 |
| 4832 | LandNest 건설 | LandNest 1 |
| 5592 | Nest 건설 | Nest 5 |
| 6317 | order `0x19` 연구 | 지상 공격 증가 요청 |
| 6422 | TyranoNest 건설 | TyranoNest 2 |
| 7209 | Nest 건설 | Nest 6 |
| 7583 | order `0x16` 연구 | 이동 속도 증가 요청 |

Masos는 frame 2227에 처음 생산되고, 3·5·7·9·15번째 생산 요청은 각각 frame
2594, 3076, 3401, 3709, 4516에 나온다. Dilophos는 frame 7468부터 6기를
요청한다.

`ScriptedBot`은 이 시간을 절대 timer로 사용하지 않는다. 다음과 같은 완료 조건
순서로 변환했다.

```text
Nest 1 -> EggNest 1 -> EggNest 2 -> Masos 3 -> Nest 2
-> Masos 5 -> EggNest 3 -> Masos 7 -> Nest 3 -> Masos 9
-> 채집 연구 -> Masos 15 -> Nest 4 -> LandNest 1 -> Nest 5
-> 지상 공격 연구 -> TyranoNest 2 -> Nest 6 -> 이동 연구 -> Dilophos 6
```

구조물은 `under_construction`이 끝나야 다음 단계로 진행하고, 유닛은 살아 있는
수와 producer queue를 합쳐 계산한다. 연구는 ordered packet이 성공적으로
발행된 시점에 요청 완료로 기록한다. 생산 불가나 배치 실패는 목표별 deterministic
backoff를 적용해 같은 명령을 매 decision frame마다 반복하지 않는다.

이는 핵심 기술 진행과 초기 병력 구성을 재현하는 첫 규칙 기반 순서다. 원본
replay의 이후 Masos 40기 생산, rally 변경, 취소, 전투 클릭 전체를 모사하는
행동 복제 모델은 아니다.

## 8. 최신 생산 취소

프레임 7450, 7455, 7459와 7549에서 다음 tuple이 사용됐다.

```text
subtype      = 0x01
command      = 0
unit_offset  = producer runtime pool offset
mode         = 1
queue_index  = 0xffffffff
arg2         = 0
```

이는 producer의 live queue tail을 취소하는 형식이다. packet handler가 최신 command state를 확인해 일반 유닛 생산은 subtype `0x01` 환불 경로로 처리하고, production order나 production-cost 계열이면 해당 환불 handler로 reroute한다.

현재 AI API는 이 리플레이로 확인된 `CancelProduction(..., latest)`만 공개한다. 특정 queue index를 취소하는 형식은 원본 queue 번호와 UI 표시 번호의 대응을 별도로 검증하기 전까지 노출하지 않는다.

## 9. 구현 경계

현재 Action Planner가 확정한 action은 다음과 같다.

- `ProduceUnit`: subtype `0x01`
- `Research`: subtype `0x0c`
- `Build`: subtype `0x02`, command `0x06`
- `SetRally`: subtype `0x08`, command `0x1f`
- `CancelProduction(latest)`: subtype `0x01`, mode 1, index `0xffffffff`

`ProduceUnit`, `Research`, `Build`는 packet 모양만 맞아도 유효한 행동이 되는 것이 아니다. Action Planner는 live session의 authoritative validator callback이 없으면 이 세 action을 거절한다. 현재 live adapter는 unit catalog reference를 먼저 확인하고 기존 unit/research requirement와 placement predicate를 호출한 뒤, 통과한 action만 packet으로 발행한다.
