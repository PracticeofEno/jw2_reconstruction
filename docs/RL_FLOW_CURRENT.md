# 현재 RL Flow: 유닛 종류별 부대 제어

> 2026-09-05, `-AIACT3:PORT` / `RAI3` / `ENTCMD02`
> 정책 `entv6-type-squads-mlp`, 제어 스키마 `type-squads-v1`
> Wire 버전: observation 5, global 10, entity feature 3, action 5,
> semantic vocabulary 3, point geometry 1, candidate 1, outcome 3

일반 이동·전투는 **유닛 종류별 부대 하나당 한 번** 결정한다. worker, 마소스,
딜로포스, 람포스 등은 이름을 하드코딩하지 않고 실제 `type_id`로 묶는다.
채집·건설·생산·연구와 정찰처럼 특정 개체가 수행하는 일은 개별 제어한다.
구형 `-AIENTITY` / `-AIIPC`와 명시적인 random/economy probe는 기존 진단 경로다.

## 1. 전체 흐름

```text
게임: 최초 즉시, 이후 8 simulation frame마다
  └─ owner별 fog-honest snapshot: global[802], own[U], target[E], candidate[C]
       └─ ACT_REQ → Python
            ├─ own[U] → 종류별 부대 + 개별 작업/정찰/control[D]
            ├─ 부대 평균 상태와 인원·체력·산개 정보를 신경망에 입력
            ├─ SCOUT commander 선택 (다른 기존 slot은 강제 KEEP)
            ├─ D개 control의 command/argument/assign 선택
            │    └─ 경제 담당자는 원래 source 순서로 예산을 예약
            ├─ 부대 명령을 부대원에게 펼침
            └─ 개별 경제 명령으로 해당 작업자의 명령만 덮어씀
       └─ ACT_REPLY: 기존 command[U], argument[U], assign[U]
            └─ C++ source/target/mask/live 검증 → packet planning → atomic publish
       └─ OUTCOME[U] → 실제 수신자 기준으로 학습 가능 여부[D] 확정
            └─ 다음 관측/TERMINAL에서 reward·dt·next state를 붙여 PPO 학습
```

| 기호 | 의미 |
|---|---|
| `U` | 게임이 보내는 alive entity 수. 원래 id/generation 순서 |
| `S` | 일반 명령을 공유하는 유닛 종류별 부대 수 |
| `I` | 개별 경제 담당자, 정찰병, 건물, 미완성/context entity 수 |
| `D=S+I` | 신경망 own encoder와 행동 head, rollout, PPO/BC가 처리하는 row 수 |
| `E` | 현재 보이는 적/공격 가능한 중립 target 수 |
| `C=R+B+P+Q` | 자원·건설 자리·생산 종류·연구 candidate 수 |

병력 100기가 같은 3종류라면 일반 행동은 3개다. 경제 담당자는 별도로 남는다.
일꾼만 있는 작은 장면에서는 일반 부대 row와 경제 row가 공존하므로 `D`가 항상
`U`보다 작지는 않다. 전투 유닛이 늘어날 때 반복 추론과 학습량을 줄이는 구조다.

## 2. 부대와 개별 역할

| 대상 | 제어 방식 |
|---|---|
| 완성된 일반 melee/ranged 유닛 | 같은 raw `type_id`끼리 한 부대 |
| 개별 작업 중이 아닌 worker | 같은 종류의 일반 행동 공유. 가능한 HARVEST/BUILD는 별도 source row |
| 경제 order/reservation, cargo, 건설 이동, active economy candidate가 있는 유닛 | 부대 명령에서 제외. 기존 작업·방어 mask를 가진 개별 row |
| SCOUT | 개별 row와 기존 SCOUT commander |
| 건물, 미완성, transport/other context | 개별 row. 가능한 action은 C++ mask에 따름 |

일반 부대 row에는 경제 명령이 열리지 않는다. 일꾼의 별도 경제 row에는 KEEP과
경제 명령만 열린다. 일반 MOVE와 BUILD를 같은 tick에 선택하면 BUILD를 고른
일꾼만 건설로 빠지고 나머지가 MOVE를 수행한다. 이미 작업 중인 일꾼은 처음부터
부대 수신자에 포함하지 않는다.

C++의 작업 잠금과 위협 대응 규칙은 계속 적용된다. 현재 평온한 일꾼 mask는 주로
KEEP/HARVEST/BUILD이고, 위협받는 일꾼에게만 local MOVE와 가까운 적 공격이 열린다.
그룹화가 원래 불가능한 명령을 새로 허용하지 않는다.

부대는 매 snapshot에서 다시 구성한다. 사망·생산·변신·소유권 변화·정찰 복귀가
다음 관측에 반영되며, 이전 snapshot의 row index를 재사용하지 않는다. 새로 합류한
유닛은 다음 부대 ISSUE부터 함께 명령을 받는다. KEEP은 각자의 현재 order를 유지한다.

## 3. 관측과 합법성

개별 상태를 먼저 집계한 후 own encoder를 호출한다. 개별 유닛 embedding을 모두
계산한 뒤 평균 내는 구조가 아니다.

| 신경망 입력 | Shape / 내용 |
|---|---|
| global | `[805]`: 기존 802 feature + 정규화한 예산 3개 |
| own categorical | `[D,13]`: 부대는 첫 canonical member의 categorical context |
| own role | `[D]` |
| own continuous | `[D,93]`: 부대는 member 평균, 개별 source는 본인 상태 |
| control summary | `[D,6]`: 인원/16, 최저 HP ratio, x/y 폭, 부대 여부, 경제 전용 여부 |
| target | categorical `[E,4]`, continuous `[E,14]` |
| candidate | categorical `[C,2]`, continuous `[C,14]` |
| intent | 기존 slot/start context. 일반 부대의 slot은 NONE |

부대 command/point/attack-pair mask는 **모든 부대원 mask의 교집합**이다.
공통 point가 없으면 MOVE/ATTACK_MOVE/PATROL을 닫고, 공통 target이 없으면
ATTACK_UNIT을 닫는다. non-KEEP 선택지가 없으면 강제 KEEP, log-probability 0이다.

같은 global point token은 같은 8×8 목표 cell을 뜻한다. 같은 local token은 각자의
위치에서 같은 방향·반경만큼 이동한다. 실제 좌표와 도달 가능성은 C++가 source별로
재검증한다. 적 target과 경제 candidate는 개별 정보가 필요하므로 축약하지 않는다.

## 4. 정찰과 기존 slot

종류별 부대 수는 기존 4개 slot 수에 제한되지 않는다. 일반 이동·공격은 부대 command
head가 담당하고 MAIN/RAID_A/RAID_B commander 출력은 강제 KEEP이다.
SCOUT commander만 기존 command/cell head를 사용한다.

부대 assign head는 KEEP 또는 SCOUT 분리만 선택한다. SCOUT 분리 시 해당 snapshot에서
배정 가능한 첫 canonical member **한 명**에게만 assign을 보낸다. 그 유닛은 같은 tick의
일반 부대 명령에서도 제외한다. SCOUT capacity 1과 기존 배정 cooldown을 유지한다.
정찰병의 MAIN 복귀는 다음 snapshot에서 같은 종류 부대로 합류시킨다.

## 5. Wire와 게임 적용

게임 인터페이스는 계속 개별 개체 단위다. Header 128 bytes, CRC32, version tuple,
owner/episode/frame/sequence/policy pinning을 검증한다.

```text
ACT_REQ bytes = 3624 + 329*U + 76*E + 64*C
                + 4*U*(ceil(E/32) + ceil(C/32))
ACT_REPLY bytes = 6*U + 20
reply = command[u8,U] + argument[i32,U] + assign[u8,U]
        + slot_command[u8,4] + slot_cell[i32,4]
```

부대 command는 자원을 예약하지 않는다. 경제 source 순서는 원래 canonical U 순서라서
부대 row가 중간에 있어도 Python과 C++의 예산·인구·건설 자리·연구 ledger가 일치한다.
C++는 각 source의 generation/control epoch, target 가시성, candidate와 도달 가능성을
재검증하고 dedupe/planning한 뒤 전체 owner batch를 atomic publish한다.
`published`는 packet queue 등록 성공이며 실제 명령 완료는 다음 관측의 tracker로 확인한다.

이번 변경은 Python 정책·학습 계층에 적용된다. 게임 시뮬레이션, P2P packet 순서,
동기화 flight recording 및 원본 실행 파일은 변경하지 않는다. IPC 전송량과 게임의
개별 유닛 simulation/packet 검증 비용은 남아 있다.

## 6. 학습 결과 집계

sample 시점의 compact dynamic mask, 예산 prefix, log-probability와 실제 수신자 목록을
보관한다. 개별 경제 명령을 받은 작업자나 새 정찰병은 부대 command 수신자에서 뺀다.

- 모든 실제 수신자가 `kept/deduped/published`이고 trainable일 때 부대 학습 bit를 연다.
  수신자가 없거나 일부가 reject되면 그 부대 command는 제외한다.
- 같은 부대 명령을 100명에게 보냈어도 log-probability와 PPO loss는 **한 번** 계산한다.
- 개별 작업과 정찰 assign은 실제 담당자의 결과를 사용한다.
- `--issue-cost`도 published 정책 결정 기준이다. 부대 복제 수만큼 비용을 부과하지 않는다.
- OUTCOME 전 disconnect한 미확정 action은 학습에 넣지 않는다.
- reward, 8-frame 기준 GAE, terminal/truncation bootstrap, cohort별 update는 기존 방식이다.

SHD3 BC는 원래 wire ledger 검증 후 부대 label로 투영한다. 같은 부대의 호환 가능한
일반 명령이 모두 같을 때만 공유 label을 사용한다. 서로 다른 target/point 명령,
확률을 알 수 없는 부분 KEEP 표본, 지원하지 않는 기존 RAID 배정은 제외한다.
경제 label은 개별 source에 남기고 투영한 prefix의 dynamic mask를 다시 저장한다.

## 7. 체크포인트와 실행

기존 `ranker_entity2_train.py`로 실행하며 `--policy`를 생략하면 부대 모델로 초기화한다.
기존 `entv5-slot-hunt-mlp` 가중치는 명시적으로 변환한다. 아래 명령의 작업 디렉터리는
`ranker_reconstructed_code/tools/ai`다.

```powershell
python ranker_entity2_bc.py --convert-squads-from old_entity2.pt --out entity2_squads_init.pt
python ranker_entity2_train.py --install-dir <RankerOCPV_Win 경로> --policy entity2_squads_init.pt --out entity2_squads_online.pt --games 20 --workers 4
```

변환은 호환 가중치를 복사하고 새 summary 입력 가중치는 0으로 시작한다.
optimizer·rollout·기존 log-probability·학습 lineage는 이어받지 않는다. 원본 checkpoint를
보존하고 출처 SHA-256과 변환 ID를 새 파일에 기록한다. 이미 실행 중인 학습 프로세스에는
소스 수정이 자동 반영되지 않으므로 새 경로로 실행해야 한다.

## 8. 검증과 성능 관측

```powershell
python -m unittest test_ranker_entity2_squads -v
python ranker_entity2_ppo.py --selftest
python ranker_entity2_bc.py --selftest
python ranker_entity2_server.py --selftest
python test_ranker_entity2_squads.py --benchmark
```

2026-09-05 synthetic CPU 측정: 일꾼 12 + 건물 4 + 전투 128(4종), hidden 128,
Torch 1 thread, warmup 뒤 10회 중앙값. 추론에는 관측 변환과 부대 확장을 포함한다.

| 항목 | 개별 유닛 기준 | 종류별 부대 |
|---|---:|---:|
| 정책 row 수 | 144 | 21 |
| 추론 | 33.350 ms | 13.030 ms |
| 확률 재계산 + backward | 92.798 ms | 17.301 ms |

[벤치마크 기록](../debug_artifacts/rl_squad_benchmark.json)은 실제 게임의
IPC·simulation·PPO 전체 update 시간을 측정한 값이 아니다. 서버 cohort log의 `rows`는
U 누계, `control_rows`는 D 누계, `squad_rows`는 일반 부대 row 누계다.
`act_mean_ms`로 실제 응답 지연을 확인할 수 있다.

## 9. 코드 기준점

| 내용 | 파일 |
|---|---|
| 부대 구성·관측 집계·명령 확장·결과 축약 | [ranker_entity2_squads.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_squads.py) |
| compact network·sampling·PPO | [ranker_entity2_ppo.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_ppo.py) |
| BC 투영·checkpoint 변환 | [ranker_entity2_bc.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_bc.py) |
| wire 응답·OUTCOME join·통계 | [ranker_entity2_server.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_server.py) |
| 학습 실행 | [ranker_entity2_train.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_train.py) |
| 회귀 테스트·benchmark | [test_ranker_entity2_squads.py](../ranker_reconstructed_code/tools/ai/test_ranker_entity2_squads.py) |
| Command HEAD 상세 | [RL_COMMAND_HEAD_ROW_BEHAVIOR.md](RL_COMMAND_HEAD_ROW_BEHAVIOR.md) |
