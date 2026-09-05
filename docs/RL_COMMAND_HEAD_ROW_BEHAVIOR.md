# Command HEAD: 부대와 개별 작업

> 2026-09-05, `entv6-type-squads-mlp` / `type-squads-v1`
> Wire는 `ENTCMD02`, action version 5를 유지한다.

[현재 RL Flow](RL_FLOW_CURRENT.md)의 행동 선택 상세다. 정책의 `D`개 control row와
게임이 사용하는 `U`개 entity row를 구분한다.

## 1. 정책 row와 출력

| control row | 적용 대상 | 명령 범위 |
|---|---|---|
| 종류별 부대 | 같은 type의 일반 부대원 전체 | KEEP/MOVE/ATTACK_MOVE/PATROL/ATTACK_UNIT/HOLD의 공통 mask |
| 경제 전용 source | 유휴 일꾼 한 명 | KEEP과 해당 source의 경제 명령 |
| 개별 작업/정찰/건물/context | 개체 한 명/하나 | 기존 C++ mask |

```text
gate_logit       [D]      KEEP / ISSUE
command_logits  [D,9]    non-KEEP command 1..9
assign_logits   [D,5]    KEEP + 기존 4 slot vocabulary (허용 범위는 아래 참조)
```

non-KEEP bit가 없으면 강제 KEEP, log-probability 0, command loss 제외다.
그 외에는 gate → non-KEEP command → 필요한 argument 순서로 선택한다.
경제 예약이 바뀌기 전까지 나머지 row head를 batch 계산하며, PPO에서는 당시 저장한
mask와 같은 prefix로 확률을 재계산한다.

## 2. 명령 vocabulary

| ID | 명령 | argument | 적용 |
|---:|---|---|---|
| 0 | KEEP_CURRENT_ORDER | -1 | 현재 order 유지. 경제 KEEP은 부대 명령을 덮지 않음 |
| 1 | MOVE | point 0..95 | 부대는 같은 token을 모든 수신자에게 전달 |
| 2 | ATTACK_MOVE | point 0..95 | 같은 목표 cell 또는 방향·반경으로 공격 이동 |
| 3 | PATROL | point 0..95 | 같은 token으로 순찰 |
| 4 | ATTACK_UNIT | target 0..E-1 | 모든 수신자에게 합법인 동일 visible target |
| 5 | HOLD_POSITION | -1 | 각자의 현재 위치에서 HOLD |
| 6 | HARVEST | kind R candidate | 선택한 일꾼 한 명만 채집 |
| 7 | BUILD | kind B candidate | 선택한 일꾼 한 명만 건설, 비용·footprint 예약 |
| 8 | PRODUCE_UNIT | kind P candidate | 선택한 건물 하나에 enqueue, 비용·인구 예약 |
| 9 | RESEARCH_UPGRADE | kind Q candidate | 선택한 source 하나에 enqueue, 비용·연구 claim 예약 |

STOP은 현재 개인 command vocabulary에 없다. 엔진 내부 watchdog 복구나 기존 slot
STOP은 별도다. 경제 argument는 전체 C 배열의 index다.
Point 0..63은 8×8 global cell, 64..95는 반경 64/128/256/512 px × 8방향이다.
global token은 같은 목표 cell이고, local token은 각자 위치 기준의 같은 변위다.
C++가 실제 world point를 source별로 계산하고 재검증한다.

## 3. 일꾼 작업과 명령 우선순위

같은 종류의 유휴 일꾼 A/B/C 중 A에게만 BUILD를 선택한 tick의 예다.

```text
부대 head       MOVE(cell 9)        → A/B/C 후보 명령
A의 경제 head   BUILD(site 17)      → A만 BUILD로 변경
B의 경제 head   KEEP                → B는 부대 MOVE 유지
C의 경제 head   KEEP                → C는 부대 MOVE 유지

게임 전송       A=BUILD, B=MOVE, C=MOVE
부대 loss       실제 MOVE 수신자 B/C의 결과를 합쳐 한 번
경제 loss       A의 BUILD, B/C가 경제 작업을 선택하지 않은 결정
```

이미 경제 order/reservation이 있거나 cargo를 운반하거나 건설 이동 중인 유닛은
일반 부대 수신자에서 뺀다. 본인의 작업·위협 대응 mask를 유지한다.
C++가 건설/ACK 대기 중 KEEP-only로 제한하면 그대로 따른다. 평온한 일꾼의
이동·공격을 임의로 열지 않는다. 위 예시는 MOVE와 BUILD가 각각 합법인 snapshot의
명령 우선순위를 설명한다.

## 4. Mask와 공유 예산

부대 command/point/attack mask는 member들의 교집합이다. 공통 point나 target이 없으면
해당 command를 닫는다. 같은 type이라도 SCOUT/개별 경제 작업자는 분리한다.
경제 source는 원래 `(runtime_id, activation_generation)` 순서를 유지한다.
부대 row는 자원을 예약하지 않으므로 중간에 들어가도 ledger가 달라지지 않는다.
BUILD/PRODUCE/RESEARCH는 다음 경제 source의 mask에 반영되고 HARVEST는 돈을 차감하지 않는다.

## 5. 정찰 배정

- 일반 부대 assign은 KEEP 또는 SCOUT만 허용한다.
- SCOUT는 가능한 첫 canonical member 한 명에게만 적용한다. 부대 전체에 복제하지 않는다.
- 새 정찰병은 같은 tick의 부대 command 수신자에서도 제외한다.
- SCOUT capacity는 1이다. 같은 tick에 기존 정찰병이 떠나도 자리를 재사용하지 않는다.
- 정찰병은 MAIN으로 복귀하고 다음 관측부터 같은 종류 부대로 들어간다.
- MAIN/RAID commander는 강제 KEEP이다. SCOUT commander는 기존 7개 command
  (KEEP/MOVE/ATTACK_MOVE/PATROL/HUNT_NEUTRAL/HOLD/STOP)와 cell head를 유지한다.

## 6. 게임 적용과 학습

전송 직전 `D`개 결정을 기존 `command[U]`, `argument[U]`, `assign[U]`로 확장한다.
C++는 개별 source/target/candidate, mask, generation, ownership을 검증한다.
같은 진행 중 order는 dedupe하고 생산의 반복 enqueue는 허용한다.
전체 batch publish 실패는 기존 atomic 처리에 따른다.

학습용 log-probability는 확장하지 않는다. OUTCOME의 U개 결과를 실제 수신자 map으로
D개 학습 bit로 축약한다. 수신자가 없거나 일부가 거절되면 부대 command loss를 제외한다.
전원이 정상 수락했고 stochastic choice였을 때 부대 한 번의 PPO 항을 만든다.
개별 작업 override와 정찰 assign도 담당자 기준으로 처리한다. churn 비용도 부대 결정
한 번으로 센다. `published`는 등록 성공이며 실행 완료는 다음 tracker 관측으로 확인한다.

SHD3의 서로 다른 개인 명령을 다수결로 묶어 학습하지 않는다. 호환되는 공통 명령만
부대 label로 쓰고 충돌/부분 KEEP 표본은 제외한다. 개별 경제 label은 보존한다.
모델·서버·데이터 변환 코드는 [현재 RL Flow](RL_FLOW_CURRENT.md)에 정리했다.
