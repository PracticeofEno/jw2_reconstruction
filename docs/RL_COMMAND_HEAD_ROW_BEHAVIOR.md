# Command HEAD: 부대와 개별 작업

> 2026-09-05, `entv7-worker-autopilot-mlp` / `type-squads-worker-tasks-v2`
> Wire는 `ENTCMD02`, action version 6이다.

[현재 RL Flow](RL_FLOW_CURRENT.md)의 행동 선택 상세다. 정책의 `D`개 control row와
게임이 사용하는 `U`개 entity row를 구분한다.

## 1. 정책 row와 출력

| control row | 적용 대상 | 명령 범위 |
|---|---|---|
| 종류별 부대 | 같은 type의 일반 부대원 전체 | KEEP/MOVE/ATTACK_MOVE/PATROL/ATTACK_UNIT/HOLD의 공통 mask |
| 일꾼 작업 배정 | 전체 일꾼을 집계한 한 행 | KEEP / MOVE(정찰) / BUILD(건설·확장) |
| 전투 정찰/건물/context | 개체 한 명/하나 | C++ mask |

```text
gate_logit       [D]      KEEP / ISSUE
command_logits  [D,8]    wire command 1,2,3,4,5,7,8,9 (HARVEST 제거)
assign_logits   [D,5]    KEEP + 기존 4 slot vocabulary (허용 범위는 아래 참조)
```

non-KEEP bit가 없으면 강제 KEEP, log-probability 0, command loss 제외다.
그 외에는 gate → non-KEEP command → 필요한 argument 순서로 선택한다.
경제 예약이 바뀌기 전까지 나머지 row head를 batch 계산하며, PPO에서는 당시 저장한
mask와 같은 prefix로 확률을 재계산한다.

## 2. 명령 vocabulary

| ID | 명령 | argument | 적용 |
|---:|---|---|---|
| 0 | KEEP_CURRENT_ORDER | -1 | 현재 order 유지. 일꾼 작업 행은 아무도 새로 배정하지 않음 |
| 1 | MOVE | point 0..95 | 전투 부대는 공유. 일꾼은 global 0..63에 한 명만 정찰 배정 |
| 2 | ATTACK_MOVE | point 0..95 | 같은 목표 cell 또는 방향·반경으로 공격 이동 |
| 3 | PATROL | point 0..95 | 같은 token으로 순찰 |
| 4 | ATTACK_UNIT | target 0..E-1 | 모든 수신자에게 합법인 동일 visible target |
| 5 | HOLD_POSITION | -1 | 각자의 현재 위치에서 HOLD |
| 6 | HARVEST | kind R candidate | 자동 조종 전용. 신경망 출력·행동 손실에서 제거 |
| 7 | BUILD | kind B candidate | 선택한 일꾼 한 명만 건설, 비용·footprint 예약 |
| 8 | PRODUCE_UNIT | kind P candidate | 선택한 건물 하나에 enqueue, 비용·인구 예약 |
| 9 | RESEARCH_UPGRADE | kind Q candidate | 선택한 source 하나에 enqueue, 비용·연구 claim 예약 |

STOP은 현재 개인 command vocabulary에 없다. 엔진 내부 watchdog 복구나 기존 slot
STOP은 별도다. 경제 argument는 전체 C 배열의 index다.
Point 0..63은 8×8 global cell, 64..95는 반경 64/128/256/512 px × 8방향이다.
global token은 같은 목표 cell이고, local token은 각자 위치 기준의 같은 변위다.
C++가 실제 world point를 source별로 계산하고 재검증한다.

## 3. 일꾼 작업과 명령 우선순위

일꾼 A/B/C가 있어도 정책은 작업 배정 행 하나만 계산한다. BUILD 위치 또는 정찰
목표를 고르면, 해당 후보를 수행할 수 있는 일꾼 중 유휴 일꾼을 우선하고 거리가
가까운 한 명을 배정한다. 적합한 유휴 일꾼이 없으면 채집 중인 일꾼도 선택한다.

```text
작업 head       BUILD(site 17)      → 배정기가 A 한 명 선택
자동 조종       B는 채집 시작, C는 진행 중인 채집 유지
게임 전송       A=BUILD, B=HARVEST, C=KEEP
정책 loss       BUILD 결정 한 번, A의 실제 OUTCOME으로 판정
```

자동 조종은 유휴 일꾼에게 접근 가능한 자원을 배정하고, 채집 중인 일꾼의 명령을 유지한다.
위협받는 일꾼은 각자의 합법적인 local point로 후퇴한다. 후퇴할 수 없으면 합법적인
가까운 적을 공격한다. 이미 위협 반대 방향으로 후퇴 중이면 그 이동을 유지한다.
자동 행동에는 개인 신경망 행·log-probability·actor loss·churn 비용이 없다.

건설/ACK 대기, 예약, 정찰 등 다른 작업 수행 중인 일꾼은 새 작업에 배정하지 않는다.
일꾼 정찰은 한 명의 global MOVE로 실행하며, 기존 정찰 이동이 끝나기 전에는 새 일꾼
정찰을 배정하지 않는다. 도착 후 유휴 상태가 되면 자동으로 채집에 복귀한다.
확장은 BUILD 후보에서 확장 건물·위치를 고르는 작업이며 별도 중복 action을 만들지 않는다.

## 4. Mask와 공유 예산

부대 command/point/attack mask는 member들의 교집합이다. 공통 point나 target이 없으면
해당 command를 닫는다. 일꾼 작업 후보는 가능한 일꾼들의 합집합이며 각 후보에 담당자
한 명을 미리 연결한다. 정책의 결정 순서와 C++의 실제 source 순서는 달라질 수 있지만
선택한 비용·인구·건설 자리·연구 예약의 집합은 같다. 전체 wire ledger로 합법성을 검증한다.
BUILD/PRODUCE/RESEARCH는 다음 정책 행의 mask에 반영되며 자동 명령은 예산을 소모하지 않는다.

## 5. 정찰 배정

일꾼 정찰은 작업 배정 행의 global MOVE를 사용한다. 아래 SCOUT slot은 기존 전투 유닛
정찰 기능으로 별도로 유지된다.

- 일반 부대 assign은 KEEP 또는 SCOUT만 허용한다.
- SCOUT는 가능한 첫 canonical member 한 명에게만 적용한다. 부대 전체에 복제하지 않는다.
- 새 정찰병은 같은 tick의 부대 command 수신자에서도 제외한다.
- SCOUT capacity는 1이다. 같은 tick에 기존 정찰병이 떠나도 자리를 재사용하지 않는다.
- 기존 재배정 cooldown 240프레임을 유지한다. SCOUT 배정 가능 대상은 C++ 전투 유닛 mask를 따른다.
- 정찰병은 MAIN으로 복귀하고 다음 관측부터 같은 종류 부대로 들어간다.
- MAIN/RAID commander는 강제 KEEP이다. SCOUT commander는 기존 7개 command
  (KEEP/MOVE/ATTACK_MOVE/PATROL/HUNT_NEUTRAL/HOLD/STOP)와 cell head를 유지한다.

기존 네 slot은 여러 유닛 종류를 섞을 수 있는 역할별 영속 부대였으며 실제 부대장 유닛은
없었다. slot의 KEEP은 저장된 목표를 유지하고, 이동 중인 slot에서 추적 명령이 끝난
유닛이나 신규 유닛을 그 목표로 자동 재유도할 수 있다. 현재 종류별 부대는 관측마다
재구성하며 별도 영속 목표를 저장하지 않는다. 새로 합류한 유닛은 다음 부대 ISSUE부터
공동 명령을 받고, 부대 KEEP은 각자의 현재 명령을 유지한다.

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

SHD3의 개인 KEEP은 기존 MAIN/RAID slot 명령에 따르겠다는 기록일 수 있다.
`_teacher_member_command`는 같은 tick의 교사 assign을 반영한 slot의 새 명령 또는
유지 중인 명령을 읽고, MOVE/ATTACK_MOVE/PATROL/HOLD를 합법인 부대/개별 ISSUE로
옮긴다. 명시적인 개인 ISSUE는 우선한다. 예를 들어 MAIN의 ATTACK_MOVE(cell 9)와
다른 개인 명령이 없는 멤버들의 KEEP은 종류별 ATTACK_MOVE(cell 9) label이 된다.

개인 KEEP에는 반복된 개인 공격을 유지한다는 의미도 있다. 현재 명령이 적용 대기/활성
상태이거나 엔진 명령의 출처를 모르는 경우, 현재 slot에서 유래한 명령임을 확인할 수
없으면 slot 명령을 합성하지 않는다. 같은 tick에 다른 slot으로 재배정된 경우에도
이전 slot과의 MATCH만으로 새 목표를 따른다고 가정하지 않는다.

서로 다른 목표를 다수결로 묶지 않는다. 공통 명령이 충돌하거나 명령·배정 정보가
불명확한 경우, 확률을 알 수 없는 부분 KEEP, 개인 명령으로 표현할 수 없는 STOP과
대상 정보가 없는 HUNT_NEUTRAL, 현재 mask가 닫힌 명령은 제외한다. KEEP으로 바꾸어
학습하지 않는다. MAIN/RAID 재배정 label 자체는 제외하고 SCOUT commander와 건물의
생산·연구 label은 보존한다. 일꾼은 단일 BUILD/global MOVE만 작업 label로 옮긴다.
HARVEST/local 후퇴/방어는 자동 조종으로 제외하고, 동시에 여러 일꾼 작업이 필요한
teacher 기록은 단일 작업 행으로 표현할 수 없어 제외한다. 작업이 없으면 작업 KEEP을
학습한다. 변환한 label의 예산 prefix와 dynamic mask도 다시 계산한다.
모델·서버·데이터 변환 코드는 [현재 RL Flow](RL_FLOW_CURRENT.md)에 정리했다.
