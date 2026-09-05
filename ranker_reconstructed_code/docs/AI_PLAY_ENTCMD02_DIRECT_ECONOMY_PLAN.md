# AI Play ENTCMD02 직접 경제·전투 제어 계획

> 작성·검토 기준: 2026-09-02
>
> 상태: **구현됨 (2026-09-03) + 팀 의도 슬롯/커맨더 확장 feature v3 (2026-09-04)** —
> C++ `ranker_ai_entity_economy.*` + winmain `run_ai_entity2_play_frame`
> (`-AIACT3:PORT`, `-AISHADOW2`→SHD3), Python `tools/ai/ranker_entity2_*.py`.
> act2/ENTCMD01은 별도 모드로 그대로 유지. v3 추가분(슬롯 4개·슬롯 명령 8·전역 셀 64·
> assign 원장·시작 후보 8칸·intent_reward_material·불복종 마스크)은
> [AI_PLAY_INTENT_SLOT_DESIGN_EASY.md](../../docs/AI_PLAY_INTENT_SLOT_DESIGN_EASY.md) §8 구현 메모 참조.
> 버전 튜플 (5,10,3,3,3,1,1,3), 고정 prefix 3624B, own appendix 42B, reply 6U+20B.
>
> 범위: 로컬 `ranker_rebuild.exe` ↔ Python AI의 새 `act3` socket mode
>
> 선행 계약: [ENTCMD01 전투 entity-command 계획](AI_PLAY_ENTITY_COMMAND_RL_PLAN.md)

## 1. 결론

`ENTCMD02`는 `ENTCMD01`의 macro 필드를 남긴 채 경제 명령 몇 개를 덧붙이는
형식이 아니다. macro action/head/cadence/history를 완전히 제거하고, 전투 유닛,
일꾼, 생산·연구 건물을 같은 8-frame entity decision으로 직접 제어하는 별도
계약으로 만든다.

외부 command index는 기존 0..6을 보존하고 뒤에 4개를 append한다.

| ID | command | argument domain | 실행 성격 |
|---:|---|---|---|
| 0 | `KEEP_CURRENT_ORDER` | `-1` | 현재 엔진 order 유지 |
| 1 | `MOVE` | point token `0..95` | 지속 order |
| 2 | `ATTACK_MOVE` | point token `0..95` | 지속 order |
| 3 | `PATROL` | point token `0..95` | 지속 order |
| 4 | `ATTACK_UNIT` | hostile target row `0..E-1` | 지속 order |
| 5 | `HOLD_POSITION` | `-1` | 지속 order |
| 6 | `STOP` | `-1` | 단발 전환 |
| 7 | `HARVEST` | resource candidate row | 지속 경제 order |
| 8 | `BUILD` | atomic build candidate row | 지속 경제 order |
| 9 | `PRODUCE_UNIT` | production candidate row | enqueue event |
| 10 | `RESEARCH_UPGRADE` | research candidate row | enqueue event |

다음 두 명령은 만들지 않는다.

- `RETURN_CARGO` 없음: `HARVEST`를 한 번 내리면 엔진이 채집, 본진 복귀,
  반납, 같은 자원 복귀를 자동 반복한다.
- `EXPAND` 없음: 확장은 `BUILD(base_type, expansion_site_candidate)`다.

generic semantic planner에 이미 있는 `return_cargo` enum을 다른 호출자 때문에
삭제할 필요는 없다. 다만 ENTCMD02 wire, actor head, Shadow label에는 절대 노출하지
않는다.

## 2. 목표와 비목표

### 2.1 목표

- 정책이 일꾼의 자원 선택과 건설 위치를 직접 고른다.
- 정책이 생산 건물별 생산 unit과 연구 건물별 upgrade를 직접 고른다.
- 기존 전투 직접 명령과 경제 명령이 하나의 deterministic frame transaction을 쓴다.
- 모든 행동은 기존 semantic planner와 ordered gameplay packet 경로로만 들어간다.
- 후보, mask, packet, order 추적, PPO log-prob를 같은 snapshot 의미로 고정한다.
- ENTCMD01 checkpoint/data/wire와 조용히 혼합되지 않게 hard version boundary를 둔다.

### 2.2 비목표

- 원본 `ranker.exe` 수정
- 실제 P2P gameplay packet 또는 동기화 state 변경
- scripted macro를 새 이름으로 후보 생성기 안에 숨기는 것
- transport, merge, morph, stance, ability, item, queued tactical command 추가
- 취소·중단 의미가 검증되지 않은 busy 경제 order의 임의 전환

첫 구현 slice는 현재 reference와 footprint가 검증된 **Tyrano session**으로 제한한다.
전 종족을 지원한다고 선언하려면 building type `0x60..0xa9`의 footprint, cost,
interaction ring, producer reference를 먼저 복원하고 같은 테스트를 통과해야 한다.

## 3. 확인된 기존 엔진 의미

새 engine opcode를 만들 필요는 없다. `PlanAiSemanticActionV1`에 필요한 경로가 이미
있으며 ENTCMD02는 그 입력을 entity policy가 직접 채우게 한다.

| semantic action | 기존 ordered packet 의미 | 권위 있는 입력 |
|---|---|---|
| `HARVEST` | subtype `0x02`, cmd `0x07`, exact resource 좌표 | worker action bit와 관측/실행 시 resource tile 검사 |
| `BUILD` | subtype `0x02`, cmd `0x06`, `type-0x60`, 32px 정렬 anchor | session primary reference와 live production/placement validator |
| `PRODUCE_UNIT` | subtype `0x01`, unit type | session alternate reference와 queue/resource/population validator |
| `RESEARCH_UPGRADE` | subtype `0x0c`, order id와 cost | session completion reference와 production-order validator |

HARVEST의 engine state `0x28..0x2d`는 접근, 채집, dropoff 탐색, 운반,
deposit, 원래 자원 복귀를 연결한다. 중간에 engine target이 dropoff로 바뀌어도
controller latch는 원래 resource tile을 보존하며, deposit을 order 완료나 interruption으로
판정하지 않는다.

BUILD는 접근·생성·공사 state `0x23 -> [optional 0x25] -> 0x24` 전체를 한 order로 추적한다.
PRODUCE와 RESEARCH는 이동 order가 아니라 queue event이므로 같은 dedupe 규칙을
적용하지 않는다.

## 4. 제어권과 frame 처리 순서

모든 역할은 frame 0부터 고정 8-frame cadence를 쓴다. `macro_due` stream은 없다.

```text
같은 simulation frame f
  1. 모든 controlled owner의 observation/entity/candidate snapshot 생성
  2. 모든 ACT_REQ 전송 및 ACT_REPLY 수집
  3. owner별 canonical economy autoregressive 결과 재검증
  4. live source/target/candidate 검증
  5. side-effect-free semantic planning
  6. plan 성공 row만 resource/population/queue/research/global-site ledger에 예약
  7. (owner id, EntityKey, planner ordinal) 순으로 packet flatten/sort
  8. reliable ring 전체 용량 preflight 후 한 번에 publish
  9. row별 OUTCOME 기록
```

reliable batch가 보장하는 것은 transport 수준의 all-or-none이다. gameplay receiver가
각 packet을 실제 적용하는 과정까지 원자적이라는 뜻은 아니다. 따라서 publish 전에
receiver와 같은 비용·queue·site 규칙을 shadow ledger로 순차 적용해 후발 packet도
받아들여질 상태를 만들어야 한다. `PUBLISHED`도 `EXECUTED`라고 기록하지 않는다.

ring preflight는 packet을 reliable channel별로 나눠 각 channel의 `0x800` producer/
consumer window와 metadata slot을 따로 검사한다. 여러 channel packet 수를 잘못 합쳐
global 2048 limit으로 거절하지 않는다. 어느 channel 하나라도 실패하면 그 frame의
모든 controlled-owner packet과 controller side effect를 0으로 한다.

ENTCMD02가 owner를 제어하는 동안에는 기존 macro stage와
`AiMicroExecutorStep`의 worker-only packet stage를 decision frame 사이를 포함해 완전히
끄고, 해당 source를 다른 scripted/autopilot 경로에서도 제외한다. controller 장애 때
scripted/random fallback을 켜지 않는다. 이미 엔진에 적용된 HARVEST loop 같은 기존
order만 자연히 계속된다.

ENTCMD01과 같은 local-AI guard를 유지해 실제 network player가 2명 이상인 live P2P에서는
ENTCMD02 controller와 `act3`를 시작하지 않는다.

## 5. own entity 집합

own row는 더 이상 완성된 근접·원거리 mobile만 담지 않는다.

- 살아 있고 해당 owner가 제어하는 combat unit
- harvest capability가 있는 worker
- primary/alternate/completion reference가 있는 building
- 건설 중 building과 아직 직접 지원하지 않는 transport/other는 context row

초기 role vocabulary는 다음과 같다.

| ID | role | 용도 |
|---:|---|---|
| 0 | `MELEE` | 전투 adapter |
| 1 | `RANGED` | 전투 adapter |
| 2 | `WORKER` | 이동·전투 capability 및 HARVEST/BUILD adapter |
| 3 | `BUILDING` | PRODUCE/RESEARCH adapter |
| 4 | `TRANSPORT` | 현재 context-only |
| 5 | `OTHER` | 현재 context-only |

role은 actor routing과 loss balancing에만 쓴다. 최종 legality는 role 이름이 아니라
live capability/reference와 pair mask가 결정한다. 예를 들어 공격 가능한 worker나
생산과 연구를 모두 지원하는 base의 명령을 role 하드코딩으로 막지 않는다.

초기 slice의 source-state별 명령 범위는 다음처럼 고정한다.

| source state | 열 수 있는 command |
|---|---|
| under-construction, TRANSPORT, OTHER | KEEP only |
| active HARVEST worker | KEEP only |
| active BUILD worker | KEEP only |
| active HARVEST/BUILD latch가 없는 completed worker | KEEP, capability가 있는 MOVE/ATTACK_MOVE/PATROL/ATTACK_UNIT/HOLD/STOP, HARVEST, BUILD |
| completed combat mobile | 기존 capability가 허용한 0..6 |
| completed building | KEEP, pair가 있는 PRODUCE/RESEARCH |

HOLD는 live hold capability, STOP은 completed mobile이라는 ENTCMD02 source gate를
반드시 통과한다. building/context row에 기존 ENTCMD01의 unconditional HOLD/STOP을
상속하지 않는다. HARVEST/BUILD latch가 끝난 다음 decision부터 worker 명령을 다시
연다. 32-bit `command_mask`는 low 11 bits만 쓰고 bits 11..31은 0이다.

## 6. entity feature v2

기존 own row의 field-major 207 byte 기여분과 continuous feature 33개를 prefix
그대로 보존하고, source appendix 36 byte와 effective queue slot 80 byte를 더해
row당 byte 기여분을 323으로 고정한다. 실제 직렬화 순서는 §12.2가 정한다.

| append field | type | 의미 |
|---|---|---|
| `capability_bits` | `u32` | move/attack/harvest/build/produce/research 등 authoritative capability |
| `queued_production_type_id` | `u32` | active production type 또는 sentinel |
| `production_variant` | `u32` | producer runtime variant |
| `deferred_command_count` | `u32` | queue 사용량, limit 4 |
| `walking_build_type_id` | `u32` | active/awaiting BUILD type 또는 sentinel |
| `active_economy_candidate_row` | `i32` | 현재 latch가 C에 있으면 row, 아니면 `-1` |
| `source_state_bits` | `u32` | completed/under-construction/carrying/queue-full/active/reserved |
| `cargo_ratio` | `f32` | cargo flag가 있을 때 amount/capacity, 아니면 0 |
| `queue_fill_ratio` | `f32` | deferred count/4 |

`capability_bits`는 bit0 move, bit1 attack, bit2 patrol, bit3 hold, bit4 harvest,
bit5 build, bit6 produce, bit7 research를 쓰고 나머지는 0이다. `source_state_bits`는
bit0 completed, bit1 under-construction, bit2 cargo-nonzero, bit3 queue-full,
bit4 active-persistent-economy-order, bit5 outstanding-reservation을 쓰고 나머지는 0이다.
capability bit 자체는 action 허가가 아니며 request 시점 pair/command mask와 live
receiver 검사를 함께 통과해야 한다.

`cargo-nonzero`라는 wire 이름은 engine의 carry 상태를 뜻한다. 실제 권위는
`(command_flags & 4) != 0`이며 stale할 수 있는 `cargo_amount != 0` 검사가 아니다.
carry flag가 0이면 amount field가 남아 있어도 `cargo_ratio=0`으로 encode한다.

source마다 다음 16-byte effective queue slot 5개를 engine 실행 순서로 append한다.
engine active 1개와 deferred 최대 4개가 동시에 정상일 수 있기 때문이다.

```text
u8  kind       // 0 EMPTY, 1 PRODUCE, 2 RESEARCH
u8  status     // 0 EMPTY, 1 ENGINE_ACTIVE, 2 ENGINE_DEFERRED, 3 AWAITING_APPLY
u16 origin_channel // 0xffff = controller origin unknown
u32 object_id  // unit type 또는 research order
u32 origin_sequence
u32 queue_ordinal
```

active production, exact `deferred_commands`, publish됐지만 아직 engine queue에 보이지
않는 event를 합쳐 effective queue를 만든다. 빈 slot의 나머지 값은 0이다. valid
origin identity는 `(origin_channel, origin_sequence)`이며 sequence 0도 유효하다.
channel은 `0..0xfffe`, controller sidecar가 없는 engine entry만 `0xffff`를 쓴다.
`queue_ordinal=0`은 active, `1..4`는 engine 실행 예정 순서다. AWAITING_APPLY도
예약한 예정 ordinal을 쓴다.
다섯 slot을 넘는 상태는 contract error이며 policy는 count뿐 아니라 실제 queue
구성을 본다.

`active_economy_candidate_row`는 attention gather에만 쓴다. 값은 반드시 `-1` 또는
`0..C-1`이고 그 밖은 contract error다. snapshot-local 정수를 embedding identity나
연속 숫자로 학습시키지 않는다. 다른 raw categorical OOB만 기존 UNK/presence
정책을 따르며 reserved/high bits는 0이어야 한다. queued/walking type sentinel은
`0xffffffff`이고 observation의 기존 0 sentinel을 wire encode 때 변환한다.

기존 802 global encoder는 byte layout을 바꾸지 않으므로 global feature version 10을
유지한다. ENTCMD02에서 의미 없는 high-level objective/raid/autopilot decision-context
slot은 canonical zero/none으로 넣는다. own spendable budget은 반올림된 global float를
역산하지 않고 request의 exact integer prefix로 별도 전달한다.

## 7. typed economy candidate

경제 argument는 96 point token을 재사용하지 않는다. 요청마다 네 segment를 이어 붙인
typed table `C = R + B + P + Q`를 보낸다.

| segment | kind ID | kind | candidate identity |
|---|---:|---|---|
| R | 0 | `RESOURCE` | exact resource tile |
| B | 1 | `BUILD_SITE` | `(building_type, exact aligned anchor)` |
| P | 2 | `PRODUCE_UNIT` | exact unit type |
| Q | 3 | `RESEARCH_UPGRADE` | exact order id와 next level |

각 candidate row는 64 byte다.

```text
u64 key
u8  kind
u8  flags
u16 object_id
i32 x
i32 y
u32 raw0
u32 raw1
u32 raw2
f32 feature[8]
```

`x/y`는 world pixel 좌표다. RESOURCE는 tile center, BUILD는 32px-aligned anchor를
쓰며 PRODUCE/RESEARCH는 둘 다 0이다. key의 `tile_x/tile_y`는 이 좌표를 map tile로
환산한 값이다.

`raw0..2`는 candidate schema `entcand1`의 kind별 표로 비용, population delta,
resource amount, footprint, next level 같은 exact integer를 담는다. `feature[8]`은
좌표, 비용, 양/level, footprint, owner start와의 거리처럼 bounded normalization한
학습 입력이다. kind, flag, raw field 의미와 normalization은 checkpoint metadata에
hash/ID로 고정한다. NaN/Inf, duplicate `(kind,key)`, undefined flag/high bit는
contract error다.

초기 `entcand1`의 key와 bit layout은 다음처럼 고정한다. identity는 전 구간에서
`(kind,key)` 쌍이며 row index와 무관하다. 같은 쌍이 두 번 나오면 오류지만 서로
다른 kind가 같은 64-bit key 값을 갖는 것은 정상이다. latch, SHD2, checkpoint audit도
항상 이 쌍을 사용한다.

```text
RESOURCE key = compact_tile_index
BUILD key    = (object_id << 32) | (tile_y << 16) | tile_x
PRODUCE key  = unit_type
RESEARCH key = (order_id << 32) | next_level

flags bit0 explored
      bit1 currently_visible
      bit2 expansion_site
      bit3 active_or_reserved
      bit4 at_least_one_source_pair_available_before_joint_reservation
      bit5 value_is_remembered_not_current
      bit6..7 zero
```

| kind | `object_id` | `raw0` | `raw1` | `raw2` |
|---|---|---|---|---|
| RESOURCE | 0 | compact tile index | remembered amount | targeting worker count |
| BUILD | building type | primary cost | secondary cost | width/height/placement-class packed word |
| PRODUCE | unit type | primary cost | secondary cost | population cost |
| RESEARCH | order id | primary cost | secondary cost | next level |

BUILD packed word는 bits 0..7 width, 8..15 height, 16..23 placement class를 쓰고
24..31은 0이다. `entcand1 feature[8]`은 다음 표로 완전히 고정한다. 모든 ratio는
`[0,1]`로 clamp하고 denominator가 0이면 0이다.

| index | RESOURCE | BUILD | PRODUCE | RESEARCH |
|---:|---|---|---|---|
| 0 | `x/map_world_width` | 같음 | 0 | 0 |
| 1 | `y/map_world_height` | 같음 | 0 | 0 |
| 2 | 0 | `primary_cost/1000` | 같음 | 같음 |
| 3 | 0 | `secondary_cost/1000` | 같음 | 같음 |
| 4 | 0 | 0 | `population_cost/100` | 0 |
| 5 | `remembered_amount/4095` | `footprint_area/64` | `compatible_completed_producers/8` | `next_level/max_level` |
| 6 | `targeting_worker_count/8` | any-source available bit | any-source available bit | any-source available bit |
| 7 | `distance(owner_start,xy)/map_world_diagonal` | 같음 | 0 | 0 |

`any-source available`은 joint reservation 전 `economy_pair_mask`에 해당 candidate의
bit가 하나라도 있다는 뜻이다. max-level은 live session completion catalog에서 얻으며
없거나 0이면 feature 5는 0이고 pair는 닫는다. 이 표와 상수는 normalization ID
`entcand1` 및 metadata hash에 포함한다.

reply의 economy argument는 합친 C의 row를 가리킨다.

```text
HARVEST          0 <= arg < R
BUILD            R <= arg < R+B
PRODUCE_UNIT     R+B <= arg < R+B+P
RESEARCH_UPGRADE R+B+P <= arg < C
```

command와 candidate kind가 다르면 receiver가 즉시 그 row를 거절한다.

## 8. 후보 생성과 canonical order

### 8.1 Resource R

- explored이며 관측 기억상 amount가 0보다 큰 harvestable tile을 사용한다.
- exact compact tile index 오름차순으로 정렬한다.
- 어두운 곳의 실시간 고갈 여부를 읽어 candidate/mask에서 누설하지 않는다.
- active HARVEST가 자동 반납 중일 때 original resource key는 latch에 보존한다.

### 8.2 Build B

BUILD는 type과 site를 따로 sample하지 않는다. 한 row가 다음 전체를 원자적으로 가진다.

```text
(building_type, aligned_anchor, footprint, interaction/path ring, site_class)
```

- source의 session primary reference에서 buildable type을 얻는다.
- local/general candidate는 각 building type과 고정 8x8 global geometry bucket의
  중심에서 기존 deterministic site search를 실행해 bucket당 최대 한 exact site를
  만든다. 전략 점수로 순위를 매기지 않는다.
- 모든 undeveloped berry expansion cluster에는 base type 후보를 하나씩 만든다.
- 이미 active/awaiting인 BUILD site도 append한 뒤 key로 dedupe하여 latch attention을
  유지한다.
- expansion 후보는 flag일 뿐 별도 command가 아니다.
- `(type, tile_y, tile_x)` canonical order로 정렬하고 중복을 합친다. 같은 anchor가
  local/general/expansion 생성기에서 겹치면 identity는 하나이며 expansion flag는 OR,
  explored/visible/available은 같은 snapshot predicate로 다시 계산한다.
- full `type x every map tile` Cartesian이나 planner가 고른 최적 한 곳만 보내지 않는다.
- 후보가 cap을 넘으면 전략적 top-k로 조용히 자르지 않는다. candidate schema를
  ragged two-stage pointer로 개정하거나 해당 map을 명시적 unsupported로 실패시킨다.

### 8.3 Produce P와 Research Q

- P는 own producer의 live alternate references에서 unique unit type을 모아 정렬한다.
- Q는 live completion references에서 unique research order를 모아 정렬한다.
- Tyrano enum 표를 새 policy 계약의 권위로 하드코딩하지 않는다.
- unavailable candidate도 비용·prerequisite context로 보낼 수 있지만 pair bit는 0이다.
- RESEARCH source는 재발행에 따른 restart/resource drain을 막기 위해 active type이 없고
  deferred queue도 0인 완전 idle 상태에서만 pair bit를 연다.

### 8.4 cap과 truncation

`U <= 2048`, `E <= 2048`, 각 `R/B/P/Q <= 2048`, 따라서 `C <= 8192`로 시작한다.
어떤 row나 candidate도 silent truncation하지 않는다. cap, payload, checked-multiply를
넘으면 ACT_REQ를 만들기 전에 contract-fatal로 처리한다.

구현 Phase 0에서 지원하는 모든 shipped map과 session catalog의 실제 R/B/P/Q 최대치를
inventory한다. 2048이 부족하면 구현을 강행하지 않고 candidate schema/version과
wire 계산식을 먼저 바꾼다.

## 9. fog-honest mask와 live 검증

request에는 기존 `attack_pair_mask[U,E]`와 새
`economy_pair_mask[U,C]`를 LSB-first row-major bitset으로 보낸다.

mask는 관측으로 정당하게 아는 정보만 쓴다.

- explored/static terrain
- own state와 own queue/resource/research
- visible 또는 기억 정책이 허용한 resource/site 정보
- own/visible occupancy

hidden enemy building, hidden blocker, 어두운 곳의 live depletion을 mask 생성에 쓰지
않는다. authoritative planner는 publish 직전 live 상태로 다시 검증할 수 있으며,
그때 발견한 hidden/stale 충돌은 해당 row outcome으로만 나타난다.

명령 bit는 source capability가 있고 matching candidate pair bit가 하나 이상일 때만
열린다.

| command | pair mask의 핵심 gate |
|---|---|
| HARVEST | owned/alive/completed worker, harvest bit, 관측상 resource, 접근 조건 |
| BUILD | exact primary ref, completed builder, carry flag bit 4가 0, prereq/cost, snapshot-safe placement/path |
| PRODUCE | exact alternate ref, completed producer, queue `<4`, cost/pop/prereq |
| RESEARCH | exact completion ref, idle researcher, cost/prereq/cap/next level |

KEEP은 항상 legal이다. 한 row에 KEEP만 남으면 forced KEEP이고 stochastic actor,
entropy, KL, PPO denominator에서 제외하며 log-prob는 0이다.

## 10. same-tick 경제 예약과 순차 sampling

각 row를 독립 sample하면 개별 mask가 모두 legal이어도 같은 owner의 돈, 인구,
queue, 연구 lock, 건설 부지를 동시에 초과할 수 있다. 따라서 economy row는 canonical
`EntityKey` 순서의 autoregressive distribution으로 sample한다.

1. live bank에서 walking/awaiting BUILD 및 publish 후 아직 관측되지 않은 예약을 뺀
   exact `spendable_primary`, `spendable_secondary`, `spendable_population`으로 시작한다.
2. 첫 economy row의 command/candidate를 sample한다.
3. 선택한 cost, population, queue slot, research order, footprint를 임시 예약한다.
4. 다음 row mask에서 충돌 edge를 제거한 뒤 sample한다.
5. 실제 사용한 row 순서와 매 단계 dynamic mask를 rollout에 저장한다.
6. C++ receiver가 같은 canonical 순서와 shadow ledger로 reply를 재검증한다.

BUILD 비용은 worker가 site에 도착할 때까지 실제 bank에서 빠지지 않을 수 있으므로
walking/awaiting BUILD 예약은 frame을 넘어 지속한다. 비용 예약과 footprint 예약은
별개다. 비용 예약은 engine bank debit이 관측되거나 spawn-success callback이 확인되는
즉시 해제해 이중 차감하지 않는다. footprint 예약은 spawned building의 authoritative
occupancy가 같은 영역을 대신하는 순간 해제한다. spawn 전 실패/source death/order
replacement에는 둘 다 해제한다.

C++에서 bank overspend, population, queue boundary, same-site overlap, duplicate research가
발견되면 앞선 canonical winner만 승인하고 후발 row는 명시적 conflict reject로 보낸다.
정상 ENTCMD02 server는 동적 mask 때문에 이 경로에 도달하지 않아야 하며, conflict와
stale/planner/transport 오류 row는 actor-trainable이 아니다.

resource/population/queue/research ledger는 owner별이지만 footprint/site ledger는 같은
frame의 **모든 controlled owner가 공유**한다. 서로 다른 owner의 plan도
`(owner, EntityKey, planner ordinal)` 순서로 같은 global site ledger를 통과한다.
정책은 상대 owner의 비공개 선택을 조건으로 sample하지 않으며 cross-owner site
collision은 C++에서 deterministic reject한다.

각 canonical row의 순서는 `live validate -> side-effect-free semantic plan 성공 ->
ledger check/reserve`다. planner 실패 plan은 어떤 budget/site도 소비하지 않는다.
ledger conflict plan도 packet/latch side effect 없이 폐기한다.

## 11. order와 event 추적

### 11.1 HARVEST

- semantic fingerprint: `(source EntityKey, RESOURCE key)`
- 한 번 publish한 뒤 engine state `0x28..0x2d` 전체를 ACTIVE로 본다.
- dropoff target/좌표로 engine payload가 바뀌어도 original resource를 보존한다.
- 자동 deposit 뒤 같은 자원 복귀는 완료도 interruption도 아니다.
- generic 48-frame stall을 운반·채집 대기 phase에 적용하지 않는다.
- resource가 0이 되어도 carry flag bit 4가 켜져 있으면 deposit이 끝날 때까지 ACTIVE다.
- carry flag가 꺼지고 harvest family를 이탈하거나 engine order가 clear된 뒤에만 depleted
  완료로 판정한다. resource target-lost도 carry flag가 꺼진 뒤 판정하며 source
  death/control loss만 즉시 종료한다.

### 11.2 BUILD

- semantic fingerprint: `(source EntityKey, building type, exact anchor)`
- 접근 `0x23`, optional approach/work state `0x25`, spawned construction `0x24`,
  completion을 하나의 persistent record로 잇는다. 가까우면 `0x25` 없이 바로
  spawn/`0x24`로 갈 수 있다.
- spawned building의 `EntityKey`와 generation을 연결한다.
- 공사 중 generic 이동 stall을 적용하지 않는다.
- 완료, building/source death, placement failure, 명시적 order replacement에 따라 종료한다.
- active BUILD source는 초기 slice에서 KEEP only다.

### 11.3 PRODUCE_UNIT과 RESEARCH_UPGRADE

두 명령은 enqueue event다. source당 단일 active-order dedupe로 막지 않는다.

- PRODUCE는 queue와 budget이 다시 열려 있으면 같은 type을 다음 decision에 또 enqueue할
  수 있다.
- RESEARCH는 busy source와 이미 진행/완료된 level을 live mask가 닫는다.
- `(source key, packet channel, packet sequence/origin)`별 outstanding event를 추적한다.
- PRODUCE completion은 queue origin 이탈과 spawn을, RESEARCH completion은 issue 당시보다
  owner variant/level이 증가한 것을 근거로 삼는다.
- awaiting apply 상태에서 같은 packet을 재발행하지 않는다.
- reservation component는 한꺼번에 넘기지 않는다. origin이 engine deferred/active
  queue에 나타나면 controller queue-slot claim만 해제하고, bank debit 확인 시 resource
  claim만 해제한다. PRODUCE population claim은 command가 active로 승격되어 engine
  `population_reserved`에 반영될 때, RESEARCH lock claim은 owner의 active/completed
  research state가 반영될 때 각각 해제한다. 아직 반영되지 않은 component는 계속
  controller ledger에 남긴다.
- reliable consumer가 origin sequence를 지난 뒤 8 consecutive frame 동안 pending,
  active, deferred queue 어디에도 origin이 없거나 absolute 256 frame을 넘으면
  `HANDLER_REJECTED`로 종료한다. 아직 controller ledger에 남은 component만 해제하고,
  이미 engine bank/population/queue/research state로 넘긴 claim을 되돌리거나 더하지
  않는다. 이미 보낸 `PUBLISHED` OUTCOME을 소급 변경하지는 않으며 다음 snapshot의
  order/last-attempt에 late handler failure를 노출한다.

현재 snapshot의 placeholder semantic/order/last-attempt 값을 실제 order store와 연결하는
것을 ENTCMD02 구현 완료 조건으로 둔다.

### 11.4 wire semantic-order v3

active/last-attempt categorical은 raw `AiSemanticActionKind` cast가 아니라 다음 고정
vocabulary를 쓴다.

| ID | semantic order |
|---:|---|
| 0 | `NONE` |
| 1 | `EXTERNAL_UNKNOWN` |
| 2 | `MOVE` |
| 3 | `ATTACK_MOVE` |
| 4 | `PATROL` |
| 5 | `ATTACK_UNIT` |
| 6 | `HOLD` |
| 7 | `STOP` |
| 8 | `HARVEST` |
| 9 | `BUILD` |
| 10 | `PRODUCE_UNIT` |
| 11 | `RESEARCH_UPGRADE` |

HARVEST의 자동 return/deposit state도 계속 ID 8이다. 외부 generic `return_cargo`
event는 ENTCMD02 semantic order로 번역하지 않고 `EXTERNAL_UNKNOWN` 또는 Shadow
exclude로만 처리한다.

## 12. ENTCMD02 wire

ENTCMD01 header에 count 0을 끼우지 않고 framing부터 분리한다.

```text
magic                 RAI3
protocol              3
header bytes          128
contract id           ENTCMD02
max payload           16 MiB
global count          802
command count         11
point count           96
```

version tuple은 다음 8개다.

```text
observation schema       5
global feature          10
entity feature           2
entity action            2
wire semantic vocabulary 3
point geometry            1
economy candidate         1
outcome                   2
```

engine의 generic `AiSemanticAction` schema 자체는 기존 2를 유지할 수 있다. 위 semantic
3은 ENTCMD02의 외부 order vocabulary가 늘었다는 뜻이다.

### 12.1 128-byte header

| offset | type | field |
|---:|---|---|
| 0 | `u8[4]` | `RAI3` |
| 4 | `u16` | header bytes=128 |
| 6 | `u16` | protocol=3 |
| 8 | `u16` | kind |
| 10 | `u16` | flags: bit0 terminated, bit1 truncated |
| 12 | `u32` | payload bytes |
| 16 | `u8[8]` | `ENTCMD02` |
| 24 | `u16[8]` | version tuple |
| 40 | `u32` | owner |
| 44 | `u32` | episode |
| 48 | `u32` | frame |
| 52 | `u32` | sequence |
| 56 | `u32` | reply-to sequence |
| 60 | `u32` | U |
| 64 | `u32` | E |
| 68 | `u32` | R |
| 72 | `u32` | B |
| 76 | `u32` | P |
| 80 | `u32` | Q |
| 84 | `u32` | global count=802 |
| 88 | `u32` | command count=11 |
| 92 | `u32` | point count=96 |
| 96 | `u32` | payload CRC32 |
| 100 | `u32` | episode-pinned policy version |
| 104 | `u32[6]` | reserved=0 |

`macro_action_count`, `macro_policy_version`, `macro_due` flag는 존재하지 않는다.
terminated와 truncated flag는 동시에 켤 수 없고 정의되지 않은 flag는 모두 0이다.

`act3`는 새 socket mode/CLI selector이고 ENTCMD01의 `act2` peer와 endpoint state를
공유하지 않는다. frame kind ID는 `HELLO=1`, `ACK=2`, `ACT_REQ=3`, `ACT_REPLY=4`,
`OUTCOME=5`, `TERMINAL=6`, `ERROR=7`이다.

HELLO payload prefix는 다음 16 byte다.

```text
u32 max_payload_bytes
u32 reply_timeout_ms
u32 run_mode                 // 0 evaluation, 1 training
u32 controlled_owner_mask
```

그 뒤 owner id 오름차순으로 다음 48-byte requested record를 붙인다.

```text
u32 owner
u32 frozen_hostile_owner_mask
u32 requested_policy_version // 0xffffffff = configured
u32 reserved_zero
u8  requested_checkpoint_sha256[32] // all zero = configured
```

ACK(HELLO)는 같은 prefix와 record layout으로 actual policy version/fingerprint를
돌려준다. owner와 hostile mask는 byte-for-byte echo한다. HELLO/ACK header의 policy
version은 0이고, 이후 owner frame은 ACK에서 고른 version을 episode 동안 고정한다.
header부터 payload 끝까지 하나의 monotonic deadline으로 exact-read/write하며 partial
progress가 timeout을 연장하지 않는다.

ACT_REQ sequence는 `(connection,episode,owner)`별 1부터 증가하고 wrap하지 않는다.
ACT_REPLY와 OUTCOME은 새 번호를 만들지 않고 q를 echo한다. kind별 invariant는 다음과
같다.

| kind | `sequence/reply_to` | header invariant | payload |
|---|---|---|---|
| HELLO | `0/0` | owner=`0xffffffff`, frame=0, U/E/R/B/P/Q=0, flags/policy=0 | prefix+requested records |
| ACK(HELLO) | `0/0` | HELLO context/count echo, flags/policy=0 | prefix+actual records |
| ACT_REQ | 새 `q` / 이전 q 또는 0 | owner와 current U/E/R/B/P/Q, flags=0, pinned policy | full request |
| ACT_REPLY | `q/q` | ACT_REQ owner/frame/count/flags/policy exact echo | dense reply |
| OUTCOME | `q/q` | ACT_REQ owner/frame/count/flags/policy exact echo | row outcome |
| TERMINAL | 마지막 q 또는 0 / 같은 값 | final counts, pinned policy, terminated XOR truncated | outcome+full final request |
| ACK(TERMINAL) | TERMINAL echo | TERMINAL header context exact echo | empty |
| ERROR | offending q 또는 0 / 같은 값 | 가능한 context, flags=0 | `u16 code,u16 n,u8 utf8[n]`, `n<=1024` |

TERMINAL outcome은 `ONGOING=0`, `WIN=1`, `LOSS=2`, `DRAW=3`이다. terminated이면
1..3, truncated이면 0만 허용한다. controlled owner마다 owner-perspective TERMINAL을
owner id 순서로 보내고 각각 ACK를 받으며, HELLO mask의 모든 owner TERMINAL이 모여야
episode barrier가 열린다.

### 12.2 ACT_REQ와 TERMINAL

모든 integer는 little-endian, `f32`는 little-endian IEEE-754 binary32이며 padding을
넣지 않는다. own/target은 ENTCMD01처럼 field-major SoA이고 candidate와 effective
queue slot만 명시된 row-major AoS다. ACT_REQ의 실제 byte 순서는 다음과 같다.

```text
f32 global[802]
u32 spendable_primary
u32 spendable_secondary
u32 spendable_population
u32 budget_reserved_zero
u64 cumulative_losses[4]
u64 economy_reward_material[10]

// ENTCMD01-compatible own prefix, exactly this SoA order: 207*U bytes
u32 own_runtime_id[U]
u32 own_activation_generation[U]
u32 own_control_epoch[U]
u16 own_type_id[U]
u32 own_movement_class[U]
u32 own_distance_check_mode[U]
u8  own_role[U]
u32 own_render_class[U]
u32 own_command_base_state[U]
u32 own_command_state_high_flags[U]
u32 own_unit_command_flags[U]
u32 own_movement_state[U]
u8  own_semantic_order[U]
u8  own_order_status[U]
u8  own_presence_bits[U]
u8  own_engine_order_match[U]
u8  own_last_attempt_command[U]
u8  own_last_attempt_result[U]
u16 own_last_reject_code[U]
i32 own_active_target_row[U]
u32 own_attackable_class_mask[U]
f32 own_feature[U][33]             // own row outer, feature index inner
u32 own_command_mask[U]            // low 11 bits
u32 own_point_mask[U][3]

// entity-feature-v2 SoA appendix: 36*U bytes
u32 own_capability_bits[U]
u32 own_queued_production_type_id[U]
u32 own_production_variant[U]
u32 own_deferred_command_count[U]
u32 own_walking_build_type_id[U]
i32 own_active_economy_candidate_row[U]
u32 own_source_state_bits[U]
f32 own_cargo_ratio[U]
f32 own_queue_fill_ratio[U]

// row outer, slot inner, slot field order는 §6: 80*U bytes
effective_queue_slot[U][5]

// target SoA: 76*E bytes
u32 target_runtime_id[E]
u32 target_activation_generation[E]
u16 target_type_id[E]
u8  target_owner_id[E]
u8  target_role[E]                 // 0 melee, 1 ranged, 2 noncombat
u32 target_render_class[E]
u32 target_kind_bits[E]
f32 target_feature[E][14]

// candidate AoS, row order R then B then P then Q: 64*C bytes
candidate_row[C]

u32 attack_pair_mask[U][ceil(E/32)]
u32 economy_pair_mask[U][ceil(C/32)]
```

정확한 payload 크기는 다음 식이다.

```text
ACT_REQ = 3336 + 323*U + 76*E + 64*C
        + 4*U*(ceil(E/32) + ceil(C/32))
TERMINAL = 4 + ACT_REQ      // leading u32 terminal outcome
```

모든 곱셈·합은 `u64` checked arithmetic를 사용하고 header 길이와 exact equality를
검사한다. `U/E/C=0` 조합도 정상적으로 encode/decode하며 empty mask의 high bit 규칙을
지킨다. bitset의 마지막 word에서 실제 E/C를 넘는 high bit는 0이다. `command_mask`의
bits 11..31도 0이다.

207-byte own prefix와 76-byte target block의 categorical ID, presence/kind bit,
normalization은 ENTCMD01 entity-feature-v1 값을 그대로 상속한다. 이 문서에서 숫자를
추가로 고정한 own role, semantic-order, command mask와 outcome-v2 기반
last-attempt result/reject만 entity-feature-v2 override다.

### 12.3 ACT_REPLY

```text
u8 command[U]
i32 argument[U]
```

payload는 정확히 `5*U` byte다. argument sentinel/domain은 §1과 §7의 표를 따르며
unused argument가 `-1`이 아니거나 command/argument가 hard domain을 넘으면 reply
framing violation으로 connection을 닫고 그 frame packet을 0으로 한다. domain 안이지만
request mask가 0, candidate kind 불일치, snapshot 이후 stale/live-illegal인 선택은
connection을 닫지 않고 해당 row OUTCOME으로 거절한다.

### 12.4 OUTCOME

```text
u16 result[U]
u16 reject_code[U]
u32 trainable_mask[ceil(U/32)]
```

payload는 정확히 `4*U + 4*ceil(U/32)` byte다. `U=0`이면 empty payload가
정상이다.

result enum은 다음과 같다.

```text
0 KEPT
1 DEDUPED
2 PUBLISHED
3 REJECTED_MASK
4 REJECTED_STALE
5 PLANNER_FAILED
6 ENCODE_FAILED
7 REJECTED_CONFLICT
8 TRANSACTION_ABORTED
9 CONTROLLER_FAILED
```

reject code는 다음과 같다.

```text
 0 NONE                 1 OUT_OF_RANGE          2 MASKED
 3 STALE_SOURCE         4 STALE_TARGET          5 STALE_CANDIDATE
 6 OWNERSHIP            7 INACTIVE              8 VISIBILITY
 9 HOSTILITY           10 CAPABILITY           11 RENDER_CLASS
12 TERRAIN             13 POINT                14 DEPLETED
15 PLACEMENT           16 PREREQUISITE         17 RESOURCE_CONFLICT
18 POPULATION_CONFLICT 19 QUEUE_CONFLICT       20 SITE_CONFLICT
21 RESEARCH_CONFLICT   22 CANDIDATE_KIND       23 PLANNER
24 ENCODE              25 TRANSPORT_CAPACITY   26 HANDLER_REJECTED
27 INTERNAL_ERROR
```

성공 result 0..2는 reject `NONE`, 실패 result 3..9는 nonzero reject를 요구한다.
trainable bit는 result 0..2이면서 실제로 둘 이상의 legal joint choice에서 sample한
row에만 1이다. 따라서 forced KEEP은 `KEPT/NONE`이지만 bit 0이다. mask/stale/
depletion/placement/planner/encode/conflict/transaction/controller 오류도 모두 bit 0이다.
undefined enum과 mask high bit는 0이어야 한다.

### 12.5 framing 실패

header, contract, version, count, CRC, exact size, sequence echo, reserved/high flag,
finite float, cap 위반은 connection close와 persistent `controller_failed`를 일으킨다.
그 frame에는 packet을 하나도 publish하지 않고 scripted fallback도 하지 않는다.

in-range reply가 snapshot 이후 stale해진 경우는 해당 row만 거절하고 기존 order를
유지한다. reliable ring preflight 실패는 모든 승인 row를 `TRANSACTION_ABORTED`로
바꾸고 모든 owner의 packet/latch/reservation commit을 0으로 만든다. publish 이후
OUTCOME write 실패는 rollback할 수 없으므로 open transition을 폐기하고 episode를
infrastructure-invalid로 종료한다.

## 13. policy 구조와 조건부 log-prob

```text
global encoder
own entity encoder
hostile target encoder
typed economy candidate encoder
role별 masked pool(combat/worker/building/target/candidate)
  -> role-conditioned KEEP/ISSUE gate
  -> command-conditioned non-KEEP head
  -> 선택된 command의 argument head 하나
```

argument branch는 point, attack target, resource, build, produce, research로 분리한다.
point score도 command embedding/projection으로 실제 선택 command에 조건화한다.

combat row는 static state에 조건화한다. owner 안의 economy row `i`는 이전에 sample한
canonical economy prefix까지 조건화한다.

```text
P(a_econ | s) = product_i P(a_i | s, a_<i, L_i, M_i)

log P(a_i) = log P(issue_i | role_i,s,L_i,M_i)
           + log P(command_i | issue_i,role_i,s,L_i,M_i) // ISSUE일 때
           + log P(argument_i | command_i,s,L_i,M_i)     // 필요한 command만
```

`M_i`는 prefix 예약을 적용한 stored dynamic command/pair mask다. `L_i`는 remaining
primary/secondary/population, prefix에서 선택한 kind별 수, 그리고 선택 candidate
embedding의 kind별 masked mean으로 구성한다. 이전 action은 이 ledger context와 mask로만
들어가며 별도 hidden recurrent state는 쓰지 않는다.

inference는 앞 row의 sampled action으로 ledger를 갱신한다. PPO/BC recompute는 저장된
action prefix를 teacher-force하고 저장된 `M_i`와 remaining budget을 그대로 사용한다.
candidate embedding mean은 새 model weight로 stored prefix에서 다시 계산하되 raw ledger
replay가 만든 remaining budget, dynamic command mask, dynamic economy pair mask가 stored
block과 byte-for-byte 같아야 한다. 하나라도 다르면 record를 invalid로 버린다. 이렇게
해야 new log-prob가 같은 conditional distribution에서 유일하게 계산된다.

선택되지 않은 head의 log-prob, entropy, gradient는 0이다. `U/E/C=0`, candidate 1개,
padding-only batch에서도 NaN 없이 finite해야 한다.

경제 actor는 §10의 canonical 순차 mask를 사용한다. rollout 당시 dynamic mask를 저장하고
PPO update 때 현재 snapshot에서 다시 생성하지 않는다.

## 14. rollout, PPO, reward

transition key는 `(connection, episode, owner, request sequence)`다. matching OUTCOME이
오기 전에는 다음 ACT_REQ가 와도 trainable transition으로 seal하지 않는다. OUTCOME
누락은 open action 폐기와 infrastructure collector cutoff다.

bootstrap 규칙은 세 종료를 구분한다.

- true terminated: bootstrap 0
- time-limit truncated: 완전히 수신한 TERMINAL final observation의 `V(s_final)`
- infrastructure cutoff: matching OUTCOME과 next full observation까지 이미 seal된 prefix만
  유지하고, open/half-framed action은 폐기한다. retained prefix는 마지막 완전히 parse된
  observation의 stored `V`로 bootstrap하며 terminal payoff를 넣지 않는다. 안전한 next
  observation이 없는 미완성 transition은 함께 버린다.

episode는 평가·resume 관점에서는 infrastructure-invalid지만 위 sealed prefix는 학습에
사용할 수 있다. framing 이후 state나 누락 action의 reward를 추정해 붙이지 않는다.

rollout에는 다음을 저장한다.

- exact global/own/target/candidate rows와 segment offsets
- command, point, attack, economy base mask
- economy row 순서와 단계별 dynamic mask/budget
- sampled command/argument와 gate/command/argument old log-prob
- value, OUTCOME result/reject/trainable bit
- cumulative loss와 economy potential의 raw material, `dt`
- terminated/truncated, policy/checkpoint/contract fingerprints

한 episode에서 policy version을 바꾸지 않는다. 모든 controlled owner의 terminal/cutoff가
모인 barrier 뒤 update한다. advantage와 return은 update 시작 전에 한 번 계산하고 모든
PPO epoch에서 고정한다.

### 14.1 병렬 collector와 단일 learner

학습 실행기는 `--workers W`개의 게임 process를 한 cohort로 동시에 실행한다. 권장
기본값은 4이고 마지막 cohort는 `min(W, remaining_games)`만 실행한다. worker는 optimizer나
checkpoint writer를 소유하지 않으며, 중앙 learner/server 하나만 다음 순서로 진행한다.

```text
policy v immutable snapshot publish
  -> W connections concurrent HELLO/rollout
  -> connection별 모든 controlled owner terminal/cutoff barrier
  -> cohort의 모든 worker barrier
  -> trajectory별 GAE/bootstrap 후 하나의 PPO update
  -> checkpoint v+1 atomic replace
  -> READY v+1
```

HELLO ACK는 실제 `policy_version`과 checkpoint fingerprint를 pin하고 이후 모든
ACT_REQ/OUTCOME/TERMINAL에서 일치 여부를 검사한다. inference는 같은 immutable net을
읽으며 짧은 model-call lock만 공유한다. update는 active connection이 0인 cohort 경계에서만
일어나므로 허용 policy lag는 0이다. owner 하나가 먼저 TERMINAL을 보내도 같은 connection의
나머지 owner와 다른 worker는 계속 `v`로 행동한다.

각 worker에는 고유 seed, `-AIOUT`, `-AINET` P2P offset, controller port
`base_port+worker_slot`, process log와 diagnostic/drop namespace를 준다. worker별 listener는
cohort job 하나만 accept한 뒤 닫고 다음 cohort 직전에 다시 bind한다. 따라서 접속 순서가 RNG
stream을 바꾸거나 늦은 이전 worker가 다음 policy version에 들어갈 수 없다. 별도 launcher
instance끼리는 base port range, run id, network-offset range가 겹치면 안 된다. P2P flight
recorder는 worker별 `RANKER_RECONSTRUCTED_REPLAY_DIR`에, 실행 로그는 worker별
`RANKER_RECONSTRUCTED_LOG_PATH`에 기록해 동시 실행에서도 서로 덮지 않게 한다. 일반 실행에서
두 환경변수가 없으면 기존 `RankerOCPV_Win/Replays`와 `Jw2.log` 경로를 그대로 사용한다.
학습 launcher가 지정하는 replay 경로도 반드시
`RankerOCPV_Win/Replays/EntityTrain/<run-id>/<global-job>` 아래에 둔다.

checkpoint는 learner RNG뿐 아니라 완료한 global `rollout_jobs`, policy seed와 환경
`seed0` 기준값을 함께 저장한다. 재시작한 launcher는 local game 0부터 seed를 재사용하지 않고
`global_job=rollout_jobs+local_game`, `game_seed=seed0+global_job`으로 이어 간다. worker 접속
순서와 무관하게 connection RNG id와 게임 환경 seed가 같은 global job에 고정되어야 한다.
sealed transition이 0개인 정상 cohort도 optimizer/version은 유지하되 `rollout_jobs` 증가와
atomic checkpoint publish는 수행한다.

online checkpoint에는 learner-state schema version을 두고 model/optimizer/update RNG,
policy·macro fingerprint, counters, seed, learning rate/PPO epoch와 cohort 설정을 하나의 atomic
bundle로 저장한다. online 흔적은 있지만 필수 상태가 빠진 구 checkpoint를 fresh optimizer로
조용히 resume하지 않는다. 모델 가중치만 가져와 새 계보를 시작하려면 명시적인
`--reset-lineage`를 사용하고 optimizer/update RNG/counter가 0에서 시작함을 기록한다.
launcher가 먼저 읽은 `(rollout_jobs, policy fingerprint)` manifest를
server에 전달하며 server는 READY 전에 실제 load와 비교한다. in-place resume는 output lock을
얻은 뒤 checkpoint를 다시 읽어 load와 lock 사이의 atomic replace도 검출한다.

같은 lineage를 exact resume할 때 `--workers`, 환경 `--max-frames`, `seed0`, policy seed,
PPO epoch/learning rate와 per-worker sampling budget을 변경할 수 없다. 설정 변경은 명시적인
새 lineage로만 허용한다. ENTCMD01 호환 구현의 sampling cap은
`per_worker_cap * actual_cohort_size`로 계산해 마지막 partial cohort가 worker당 더 많은
timestep을 쓰지 않게 한다. ENTCMD02 구현은 아래 규칙대로 cap 자체를 제거하고 모든 sealed
chunk를 사용한다.

timeout/disconnect worker도 barrier에서 사라지지 않는다. §14의 규칙대로 안전하게 seal된
prefix만 infrastructure cutoff trajectory로 보존하고 open action은 버린다. 초기 구현은
all-or-nothing cohort다. accept 누락, HELLO/owner terminal 미완료, version/hash/wire 위반이 하나라도
있으면 그 cohort 전체를 update하지 않고 READY도 올리지 않은 채 launcher를 nonzero로 종료한다.
향후 retry를 넣을 때만 같은 manifest job id와 같은 pinned version으로 실패 slot을 재수집한 뒤
barrier를 재개한다. 새 version의 다음 cohort로 늦은 client를 넘기는 동작은 금지한다.

여러 trajectory를 먼저 이어 붙인 뒤 GAE를 계산해서는 안 된다. owner/worker별 raw GAE와
return을 독립적으로 만든 다음 actor advantage만 cohort 전체에서 normalize하고 timestep
minibatch를 합친다. ENTCMD02는 아래의 모든 sealed 256-step chunk를 사용하며 worker 수와
무관한 1,024-step cap으로 병렬 수집 데이터를 버리지 않는다.

actor loss는 대규모 combat army가 적은 worker/building gradient를 덮지 않게 다음
equal-present-role 평균으로 고정한다.

```text
R_t = stochastic trainable row가 하나 이상 있는 role 집합
S_t,r = 그 role row의 clipped PPO surrogate 평균
L_actor = mean_t ((1 / |R_t|) * sum_(r in R_t) S_t,r)
```

없는 role과 forced KEEP-only role은 `R_t`에서 빠진다. entropy와 approximate KL도 같은
timestep/role/row 평균을 쓰며 active branch만 다음처럼 합친다.

```text
H_i = H_gate + 1[ISSUE] * H_command + 1[argument command] * H_argument
KL_i도 같은 active-branch 합
```

head별 추가 weight는 두지 않고 합계에 하나의 global entropy coefficient를 적용한다.
centralized value loss는 timestep당 한 번만 계산한다. rollout은 256 timestep sealed
chunk와 terminal/cutoff의 1..255 final partial chunk로 저장하고, ENTCMD02 초기
implementation은 모든 sealed chunk를 PPO epoch마다 한 번씩 shuffled minibatch로
사용한다. 기존 uniform 1,024-step subsample cap은 쓰지 않으므로 희귀
BUILD/EXPANSION/RESEARCH가 임의로 빠지지 않는다.

### 14.2 reward

경제 actor에 command 횟수 보상을 주지 않고 potential-based shaping을 쓴다.

```text
gamma_dt = gamma_8^(dt/8)
r_t = delta_war
    + beta * (gamma_dt * Phi_econ(s_next) - Phi_econ(s_t))
    + terminal_payoff
```

`delta_war = 5 * (delta_hostile_loss - delta_own_loss) / 1000`이고 loss counter는
HELLO에서 freeze한 hostile owner set의 monotonic `u64`를 쓴다. terminal payoff는
WIN `+1`, LOSS `-1`, DRAW/ONGOING `0`이다.

ACT_REQ의 reward-only `economy_reward_material[10]`은 다음 cost-basis raw material이다.
actor/critic input으로 concat하지 않는다.

| index | raw `u64` |
|---:|---|
| 0 | live primary bank |
| 1 | live secondary bank |
| 2 | completed worker catalog-cost 합 |
| 3 | completed non-worker unit catalog-cost 합 |
| 4 | completed building catalog-cost 합 |
| 5 | bank debit이 끝난 under-construction building cost 합 |
| 6 | bank debit이 끝난 queued/active unit production cost 합 |
| 7 | completed research에 실제 투자된 cost 합 |
| 8 | bank debit이 끝난 in-flight research cost 합 |
| 9 | population capacity |

한 asset은 2..8 중 정확히 한 category에만 들어간다. controller reservation만 있고
bank debit 전인 action은 bank에도 남아 있으므로 asset category에 더하지 않는다.
queue/construction/completion 전환은 같은 cost를 category 사이에서 옮긴다.

```text
z = sum(material[0..8]) + 25 * material[9]
Phi_econ = tanh(z / 8000)
beta = 1.0
```

합과 곱은 checked `u64`이며 overflow는 contract-fatal이다. true terminal에서는 next
potential을 0으로 둔다. harvested amount, BUILD/PRODUCE/RESEARCH 횟수, packet 수,
reject 수를 직접 양의 reward로 주지 않는다. positive-only economy delta도
파괴·재생산 cycle exploit 때문에 쓰지 않는다.

potential 항목, weight, scale, `beta/gamma/lambda`는 reward metadata ID로 freeze하고
telescoping test를 둔다. 기존 `dt`-aware GAE와 truncated bootstrap을 유지한다.

## 15. Shadow BC와 checkpoint 이동

### 15.1 SHD2

ENTCMD02용 Shadow는 `SHD2`로 새로 수집한다.

```text
SHD2
u32 body_size
ENTCMD02 header + ACT_REQ
u32 U
label[U]                    // 16 bytes each
u32 dynamic_command_mask[U]
u32 remaining_budget_before_row[U][3] // primary, secondary, population
u32 dynamic_economy_pair_mask[U][ceil(C/32)]

label:
  u8  label                 // 0 KEEP, 1 ISSUE, 2 EXCLUDED
  u8  command
  u16 exclude_reason
  i32 argument
  f32 inclusion_probability
  u32 reserved_zero
```

exclude reason은 `NONE=0`, `SOURCE_MISSING=1`, `TARGET_MISSING=2`,
`CANDIDATE_MISSING=3`, `STALE=4`, `MULTIPLE_DESIRED=5`, `RETURN_CARGO=6`,
`PREFIX_UNRESOLVED=7`, `MASK_MISMATCH=8`로 고정한다. KEEP은 command 0/argument -1,
ISSUE는 정상 command/domain, EXCLUDED는 command 255/argument -1을 쓴다.

dynamic block은 §10의 teacher prefix를 row 순서로 적용한 실제 mask와 budget이다.
SHD2 reader는 label prefix를 teacher-force해 ledger를 재생하고 stored block과
byte-for-byte 비교한다. excluded teacher event가 비용, site, queue, research lock을
어떻게 소비했는지 resolve할 수 없으면 그 row를 `PREFIX_UNRESOLVED`로 만들고 이후
economy row도 모두 같은 reason으로 제외하며 dynamic economy mask를 0으로 둔다.
combat label은 계속 사용할 수 있다. 이 규칙 없이 excluded row를 KEEP으로 가정하지
않는다.

teacher packet을 최종 source/type/site/order가 resolve된 pre-dedupe 지점에서 candidate에
매핑한다. expansion은 base BUILD+expansion candidate다. 자동 return/deposit은 새 ISSUE가
아니라 기존 HARVEST의 KEEP label이다. explicit RETURN_CARGO teacher event, candidate
missing, stale, 한 row의 multiple desired action은 각각 전용 exclude reason으로 버리고
KEEP/STOP으로 바꾸지 않는다.

role별 KEEP subsampling 확률을 record에 남기고 BC importance weight에 사용한다.
episode/owner 경계를 넘어 warmup sequence를 연결하지 않으며, expansion/research가
포함된 장기 match를 별도로 확보한다.

### 15.2 checkpoint

runtime loader는 ENTCMD01 checkpoint, SHD1, rollout을 hard reject한다. 한 connection,
dataset, chunk에서 01/02를 섞지 않는다.

선택적인 offline one-way converter만 다음 allowlist를 복사할 수 있다.

- shape와 의미가 같은 global/target encoder
- combat KEEP/ISSUE gate
- command 1..6 logit row
- attack pointer
- 96-point head는 새 command-conditioned projection으로의 exact tensor mapping과
  동일 preprocessing hash가 converter allowlist에 있을 때만 부분 복사

macro tower/head/value, 기존 critic, optimizer, worker/building/candidate head는 복사하지
않는다. point mapping 조건을 만족하지 않으면 point head도 새로 초기화한다. 새 명령
7..10과 candidate encoder는 새 초기화 후 SHD2 BC로 학습한다. 변환된
파일은 ENTCMD02 checkpoint이며 parent SHA-256, tensor allowlist, mapping ID를 기록한다.
현재 작업 트리의 ENTCMD01용 `--macro-policy` 실험은 ENTCMD02 runtime dependency가 아니다.

metadata는 contract/protocol/header와 모든 version/count/cap, command enum hash,
argument-domain ID, candidate key/order/resolver ID, build geometry/catalog hash,
pair-mask layout, source inclusion, normalization, reward, cadence, architecture,
`gamma/lambda`를 hard pin한다.

## 16. 구현 단계와 파일 경계

### Phase 0 — 계약 freeze와 inventory

- shipped map별 R/B 최대치와 session catalog별 P/Q 최대치 측정
- Tyrano building footprint/cost/reference table 교차 확인
- candidate key/order/normalization, row byte 수, payload 식 golden 문서화
- P2P-disabled local AI gate와 old scripted ownership 지점 inventory

### Phase 1 — C++ snapshot/wire

- `ranker_ai_entity_control.*`: ENTCMD02 types, expanded own rows, candidates,
  pair masks, RAI3 encode/decode, exact-size/CRC tests
- observation schema v5의 기존 tile/resource/buildable/deferred-command field만 사용하며
  `ranker_ai_observation.*` wire field는 append하지 않음. 추가가 필요하다고 판명되면
  구현 전에 observation schema를 6으로 올리고 ENTCMD02 version tuple도 함께 변경
- candidate generation은 live session references와 기존 expansion/build helper 재사용

### Phase 2 — order/event와 publish

- HARVEST/BUILD persistent latch와 event-origin queue store
- snapshot의 active order/last attempt 실제 연결
- persistent build reservation, owner별 budget/queue ledger, 전 owner global site ledger
- collect-all, canonical plan, reliable batch, outcome commit
- ENTCMD02 owner의 macro/worker executor 완전 격리

### Phase 3 — Python contract/model/PPO

- `ranker_entity_contract.py`: RAI3/ENTCMD02 strict parser와 padded ragged batch
- `ranker_entity_ppo.py`: role adapters, candidate pointer, sequential economy mask,
  exact conditional log-prob와 role-balanced loss
- `ranker_entity_server.py`: `(connection,episode,owner,sequence)` join,
  concurrent collector, all-owner/cohort barrier와 단일 learner
- `ranker_entity_train.py`: ENTCMD02-only parallel launch, worker별
  seed/AIOUT/AINET/diagnostic namespace와 checkpoint metadata

### Phase 4 — SHD2/BC/migration

- economy teacher tap과 SHD2 writer/reader
- role별 BC와 exclude accounting
- explicit allowlist checkpoint converter

### Phase 5 — focused integration

- starting economy에서 harvest, worker/army 생산, supply/tech 건설, expansion,
  research까지 ENTCMD02만으로 완주
- simulation direct mutation 없이 ordered packet 경로 확인
- 원본/rebuild 동일 명령 replay의 engine state parity 확인

## 17. 직접 관련 테스트 완료 조건

### 17.1 contract/model

- ENTCMD02 C++/Python golden header/body/CRC/size와 ENTCMD01 상호 reject
- U/E/R/B/P/Q가 0, 1, cap일 때 encode/decode와 finite inference
- candidate canonical sort, duplicate/OOB/nonfinite/cap hard failure
- command별 argument domain과 inactive head log-prob 0
- candidate permutation equivariance와 build atomic pair 보존
- sequential budget/site/research dynamic mask 저장 및 exact PPO recompute
- role-balanced loss가 forced-KEEP row 수에 불변
- dt-aware GAE, terminated/truncated, potential telescoping
- multi-owner terminal barrier, policy pin, missing OUTCOME cutoff
- 2~4 worker가 같은 version으로 수집하고 첫 worker 종료 뒤 update 0회, 마지막 worker
  종료 뒤 update/checkpoint/READY가 정확히 1회
- 서로 다른 connection의 같은 episode/owner/sequence 격리, 마지막 partial cohort,
  timeout/disconnect worker가 있어도 barrier deadlock 없음
- trajectory별 GAE 경계, raw return/normalized actor advantage 분리, 모든 PPO epoch에서
  target 불변, scheduling 순서와 무관한 worker별 RNG
- atomic checkpoint가 model/optimizer/version/RNG를 resume하고 interrupted save 뒤에도 load 가능
- launcher manifest와 locked in-place re-read가 checkpoint TOCTOU replace를 거절하고,
  learner-state 필드가 빠진 legacy online checkpoint를 hard reject
- resume에서 worker cohort 폭·환경 horizon·seed/objective 변경을 거절하고, empty cohort도
  global rollout job counter를 정확히 이어 감
- OUTCOME 전 다음 ACT/TERMINAL과 owner TERMINAL 뒤 모든 gameplay frame을 protocol-fatal 처리
- SHD2 경제 label/exclude와 ENTCMD01 hard reject/converter allowlist
- command/head/label에 `RETURN_CARGO`가 없다는 정적 검사

### 17.2 engine/integration

- HARVEST packet 1개 뒤 2회 이상 채집·자동 반납·재채집과 owner 자원 증가
- 자동 반납 중 KEEP은 packet 0, latch ACTIVE
- deposit 후 stale `cargo_amount`가 남아도 carry flag 0이면 cargo ratio/BUILD gate가
  비어 있고, 마지막 자원 고갈 시 carry flag가 꺼질 때까지 latch ACTIVE
- resource 고갈/target loss와 retarget lifecycle
- BUILD exact type/site/footprint packet과 expansion==base BUILD
- BUILD state `0x23/[optional 0x25]/0x24`, spawned key, completion/failure,
  bank-debit cost 예약 해제와 occupancy footprint 예약 해제 분리
- 같은 PRODUCE type을 queue 여유만큼 반복 enqueue 가능
- active/deferred/awaiting effective queue slot 내용과 handler-missing bounded timeout
- queue full과 busy RESEARCH mask, level/cost/prerequisite 검증
- same-tick bank overspend/pop/queue/same-site/duplicate-research deterministic winner,
  서로 다른 controlled owner의 same-site도 global ledger에서 한 row만 승인
- existing walking BUILD를 포함한 persistent reservation
- KEEP reply frame에서 scripted macro/worker packet 0
- reliable ring 실패 시 packet/latch/reservation side effect 0
- success packet/origin이 `(owner, EntityKey, ordinal)` canonical order
- snapshot에 실제 active order와 last attempt가 반영됨
- 전 테스트에서 원본 `ranker.exe`와 P2P packet/checksum 경로를 수정하지 않음

관련 테스트만 실행하며, live P2P 활성화는 이 계획의 완료 조건이 아니다.

## 18. 설계 검토 결과

wire, engine/order, learning/PPO 관점으로 따로 검토했다.

| 검토 항목 | 결론 | 계획의 방어선 |
|---|---|---|
| ENTCMD01에 억지로 끼웠는가 | 통과 | RAI3/protocol3/header128/ENTCMD02 독립 계약 |
| macro가 남았는가 | 통과 | gate/mask/reply/outcome/version/head 전체 제거 |
| 필수 경제 행동이 빠졌는가 | 통과 | HARVEST/BUILD/PRODUCE/RESEARCH 4개 포함 |
| RETURN_CARGO가 노출되는가 | 통과 | command/head/SHD2에서 제외, 자동 loop latch 명시 |
| 확장이 숨은 macro인가 | 통과 | base BUILD+expansion candidate, 별도 EXPAND 없음 |
| build type/site 불법 조합이 생기는가 | 통과 | atomic `(type, exact site, footprint)` row |
| scripted worker가 명령을 덮는가 | 통과 | ENTCMD02 owner의 기존 macro/worker stage 완전 격리 |
| 동시 경제 명령이 overspend하는가 | 통과 | Python autoregressive mask+C++ 동일 shadow ledger |
| queue event를 지속 order로 오인하는가 | 통과 | HARVEST/BUILD와 PRODUCE/RESEARCH 추적 분리 |
| fog oracle이 생기는가 | 통과 | 관측 mask와 live 실행 검증 분리 |
| wire/checkpoint가 조용히 호환되는가 | 통과 | ENTCMD01/SHD1 hard reject, offline allowlist converter만 허용 |
| 대규모 후보를 자르는가 | 조건부 통과 | 2048 segment cap inventory, 초과 시 schema 개정/명시 실패 |
| 전 종족 지원 근거가 충분한가 | 범위 고정 | 초기 Tyrano-only, 전체 footprint 복원 후 확장 |
| P2P 동기화 경로가 바뀌는가 | 통과 | local AI IPC만 변경, 기존 ordered packet 사용 |

따라서 이 문서는 구현 계획으로 사용해도 된다. 다만 **Phase 0 candidate 최대치
inventory**와 **Tyrano footprint/reference freeze** 두 gate를 통과하기 전에는 wire
상수를 코드에 확정하지 않는다. 구현이 시작되면 이 문서의 상태를 바꾸고, 실제 고정한
field/hash/golden fixture가 문서와 다르면 먼저 ENTCMD02 version을 올린다.
