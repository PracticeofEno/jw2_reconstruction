# 현재 RL Flow: 유닛 종류별 부대 제어

> 2026-09-05, `-AIACT3:PORT` / `RAI3` / `ENTCMD02`
> 정책 `entv7-worker-autopilot-mlp`, 제어 스키마 `type-squads-worker-tasks-v2`
> Wire 버전: observation 5, global 10, entity feature 3, action 6,
> semantic vocabulary 3, point geometry 1, candidate 1, outcome 3

일반 이동·전투는 **유닛 종류별 부대 하나당 한 번** 결정한다. 마소스,
딜로포스, 람포스 등은 이름을 하드코딩하지 않고 실제 `type_id`로 묶는다.
전체 일꾼은 **작업 배정 행 하나**로 집계한다. 유휴 채집·긴급 후퇴·방어는 자동화하고,
정책은 정찰·건설·확장 작업을 고를 때 담당 일꾼 한 명을 배정한다. 건물의 생산·연구는
건물별로 판단한다. 일꾼별 경제/방어 head와 학습용 HARVEST 출력을 제거했다.
구형 `-AIENTITY` / `-AIIPC`와 명시적인 random/economy probe는 기존 진단 경로다.

## 1. 전체 흐름

```text
게임: 최초 즉시, 이후 8 simulation frame마다
  └─ owner별 fog-honest snapshot: global[802], own[U], target[E], candidate[C]
       └─ ACT_REQ → Python
            ├─ own[U] → 종류별 전투 부대 + 일꾼 작업 한 행 + 정찰/건물/context[D]
            ├─ 부대 평균 상태와 인원·체력·산개 정보를 신경망에 입력
            ├─ SCOUT commander 선택 (다른 기존 slot은 강제 KEEP)
            ├─ D개 control의 command/argument/assign 선택
            │    └─ BUILD/생산/연구는 정책 결정 순서로 예산 예약
            ├─ 부대 명령을 부대원에게 펼침
            └─ 자동 채집/방어 + 선택된 작업을 담당 일꾼 한 명에게 적용
       └─ ACT_REPLY: 기존 command[U], argument[U], assign[U]
            └─ C++ source/target/mask/live 검증 → packet planning → atomic publish
       └─ OUTCOME[U] → 실제 수신자 기준으로 학습 가능 여부[D] 확정
            └─ 다음 관측/TERMINAL에서 reward·dt·next state를 붙여 PPO 학습
```

| 기호 | 의미 |
|---|---|
| `U` | 게임이 보내는 alive entity 수. 원래 id/generation 순서 |
| `S` | 일반 명령을 공유하는 유닛 종류별 부대 수 |
| `I` | 전투 정찰병, 건물, 일꾼 이외의 미완성/context row 수 |
| `W` | 일꾼이 있으면 1, 없으면 0. 일꾼 작업 배정 행 |
| `D=S+I+W` | 신경망 own encoder와 행동 head, rollout, PPO/BC가 처리하는 row 수 |
| `E` | 현재 보이는 적/공격 가능한 중립 target 수 |
| `C=R+B+P+Q` | 자원·건설 자리·생산 종류·연구 candidate 수 |

병력 100기가 같은 3종류라면 일반 행동은 3개다. 일꾼은 10명이든 128명이든 작업
행 하나만 신경망과 PPO/BC로 들어간다. 원시 관측 생성·전송과 자동 배정 계산에는
여전히 일꾼 수에 따른 비용이 있지만, 일꾼 수만큼 신경망 행동을 계산하지 않는다.

## 2. 부대와 개별 역할

| 대상 | 제어 방식 |
|---|---|
| 완성된 일반 melee/ranged 유닛 | 같은 raw `type_id`끼리 한 부대 |
| 모든 worker | 상태를 한 행에 집계. KEEP / 정찰 MOVE / 건설·확장 BUILD 선택 |
| 유휴·채집 중 worker | 자동 채집을 수행하며, 새 작업 필요 시 배정 후보 |
| 위협받는 worker | 각자의 합법적인 후퇴·방어를 자동 실행. 학습 행 없음 |
| 예약·건설·정찰 수행 중 worker | 새 작업 배정에서 제외. 정찰 중에도 긴급 위협 대응 |
| SCOUT | 개별 row와 기존 SCOUT commander |
| 건물, 미완성, transport/other context | 개별 row. 가능한 action은 C++ mask에 따름 |

일꾼 작업 후보는 수행 가능한 일꾼들의 합집합이며, 후보별로 담당자 한 명을 미리
연결한다. 유휴 일꾼을 우선하고 같은 상태에서는 가까운 일꾼을 선택한다. 적합한 유휴
일꾼이 없으면 채집 중인 일꾼도 선택한다. 작업 ISSUE는 담당자의 자동 명령만 덮어쓴다.
작업 KEEP이면 새로 배정하지 않고 자동 조종을 계속한다.

action v6에서 평온한 유휴·채집 일꾼의 global MOVE를 정찰 작업용으로 허용한다.
정찰 이동 중에는 새 작업·채집으로 끊지 않고, 도착 후 유휴가 되면 자동 채집한다.
기존 일꾼 정찰 이동이 끝나기 전에는 새 일꾼 정찰을 배정하지 않는다. 위협 시에는
local MOVE를 통한 후퇴를 우선하고, 이동할 수 없을 때 가까운 적을 공격한다.
건설/ACK 잠금은 그대로 적용한다. 확장은 BUILD 후보의 확장 건물과 위치로 선택한다.

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
| control summary | `[D,6]`: 인원/16, 최저 HP ratio, x/y 폭, 전투 부대 여부, 일꾼 작업 행 여부 |
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

기존 ENTCMD02는 유닛 종류와 관계없는 `MAIN`, `RAID_A`, `RAID_B`, `SCOUT` 네 역할
slot으로 부대를 관리했다. 완성된 melee/ranged 전투 유닛은 처음에 MAIN에 들어가고
AI의 개별 assign으로 옮겼다. 일꾼·건물은 이 slot에 가입하지 않는다. 실제 부대장
유닛은 없으며, commander는 slot별 명령을 고르는 신경망 head다.

기존 slot은 membership과 목표를 C++에 지속 보관한다. commander KEEP은 목표를
유지하고 STOP은 해제한다. 개인 명령이 우선하며, 이동 중인 slot의 개인 명령이 끝나거나
신규 유닛에 추적 명령이 없으면 slot 목표로 자동 재유도한다. 죽거나 재활성화된 개체의
이전 배정은 제거한다. 현재 종류별 부대는 관측마다 구성하는 정책 그룹이므로 이러한
별도 영속 목표가 없다. 신규 생산·정찰 복귀 유닛은 다음 관측부터 그룹에 포함되고
**다음 부대 ISSUE부터** 공동 명령을 받는다. KEEP은 각자의 현재 명령을 유지한다.

종류별 부대 수는 기존 4개 slot 수에 제한되지 않는다. 일반 이동·공격은 부대 command
head가 담당하고 MAIN/RAID_A/RAID_B commander 출력은 강제 KEEP이다.
SCOUT commander만 기존 command/cell head를 사용한다.

부대 assign head는 KEEP 또는 SCOUT 분리만 선택한다. SCOUT 분리 시 해당 snapshot에서
배정 가능한 첫 canonical member **한 명**에게만 assign을 보낸다. 그 유닛은 같은 tick의
일반 부대 명령에서도 제외한다. SCOUT capacity 1과 기존 배정 cooldown 240프레임을 유지한다.
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

전투 부대와 자동 명령은 자원을 예약하지 않는다. 작업 행의 BUILD와 건물의 생산·연구는
정책 순서로 예약한다. 배정된 일꾼의 실제 wire 위치가 달라도 총 비용·인구·건설 자리·연구
예약은 같다. C++는 실제 source 순서로 전체 명령 집합의 합법성을 다시 검증한다.
C++는 각 source의 generation/control epoch, target 가시성, candidate와 도달 가능성을
재검증하고 dedupe/planning한 뒤 전체 owner batch를 atomic publish한다.
`published`는 packet queue 등록 성공이며 실제 명령 완료는 다음 관측의 tracker로 확인한다.

이번 변경은 Python 정책·학습 계층에 적용된다. 게임 시뮬레이션, P2P packet 순서,
동기화 flight recording 및 원본 실행 파일은 변경하지 않는다. IPC 전송량과 게임의
개별 유닛 simulation/packet 검증 비용은 남아 있다.

## 6. 학습 결과 집계

sample 시점의 compact dynamic mask, 예산 prefix, log-probability와 실제 수신자 목록을
보관한다. 새 전투 정찰병은 부대 command 수신자에서 뺀다. 일꾼 작업 ISSUE는 실제
배정된 한 명의 결과로 판정한다. 작업 KEEP은 새 작업을 배정하지 않은 정책 결정이다.
자동 채집·후퇴·방어의 결과를 별도의 actor loss나 churn 비용으로 만들지 않는다.

- 모든 실제 수신자가 `kept/deduped/published`이고 trainable일 때 부대 학습 bit를 연다.
  수신자가 없거나 일부가 reject되면 그 부대 command는 제외한다.
- 같은 부대 명령을 100명에게 보냈어도 log-probability와 PPO loss는 **한 번** 계산한다.
- 개별 작업과 정찰 assign은 실제 담당자의 결과를 사용한다.
- `--issue-cost`도 published 정책 결정 기준이다. 부대 복제 수만큼 비용을 부과하지 않는다.
- OUTCOME 전 disconnect한 미확정 action은 학습에 넣지 않는다.
- reward, 8-frame 기준 GAE, terminal/truncation bootstrap, cohort별 update는 기존 방식이다.

SHD3 BC는 원래 wire ledger 검증 후 부대 label로 투영한다. 기존 교사 데이터는
MAIN/RAID의 공격 이동 등을 slot ISSUE로 기록하고 개인 명령에는 KEEP을 남길 수 있다.
`_teacher_member_command`는 같은 tick의 교사 assign까지 반영한 목적 slot에서 새 명령
또는 유지 중인 명령을 읽어 MOVE/ATTACK_MOVE/PATROL/HOLD를 해당 control의 ISSUE로
옮긴다. 명시적인 개인 ISSUE는 slot 명령보다 우선한다.

개인 KEEP은 반복된 개인 공격을 유지하는 기록일 수도 있다. 현재 명령이 적용 대기/활성
상태이거나 엔진 명령의 출처를 모르는 경우, 현재 slot에서 유래한 명령임을 확인할 수
없으면 변환에서 제외한다. 이전 slot의 MATCH는 같은 tick에 재배정된 새 slot의
명령을 따른다는 증거로 쓰지 않는다.

같은 부대의 실제 수신자들이 같은 명령·argument에 동의하고 현재 mask에서도 합법일
때만 공유 label을 사용한다. 목표 충돌, 알 수 없는 명령·배정, 확률을 알 수 없는 부분
KEEP 표본, STOP/HUNT_NEUTRAL처럼 개인 vocabulary나 대상 정보로 표현할 수 없는
slot 명령, 닫힌 mask는 학습에서 제외한다. 이들을 KEEP으로 바꾸어 학습하지 않는다.
지원하지 않는 MAIN/RAID 재배정 label은 제외하되, 그 배정에 따른 교사 명령은 위의
조건으로 옮길 수 있다. SCOUT commander와 건물의 생산·연구 label은 보존한다.
일꾼의 HARVEST/local 후퇴/방어 label은 자동 조종으로 제외하고, 단일 BUILD/global MOVE를
작업 label로 옮긴다. 동시에 여러 일꾼 작업을 요구하는 기록은 제외한다. 투영한 prefix의
dynamic mask를 다시 저장한다.

## 7. 체크포인트와 실행

기존 `ranker_entity2_train.py`로 실행하며 `--policy`를 생략하면 부대 모델로 초기화한다.
기존 `entv5-slot-hunt-mlp` 또는 `entv6-type-squads-mlp` 가중치는 명시적으로 변환한다.
HARVEST 출력 행을 제거하고 변경된 제어 feature를 초기화한다. 아래 명령의 작업 디렉터리는
`ranker_reconstructed_code/tools/ai`다.

```powershell
python ranker_entity2_bc.py --convert-squads-from old_entity2.pt --out entity2_squads_init.pt
python ranker_entity2_train.py --install-dir <RankerOCPV_Win 경로> --policy entity2_squads_init.pt --out entity2_squads_online.pt --games 20 --workers 4
```

변환은 호환 가중치를 복사하고 새 summary 입력 가중치는 0으로 시작한다.
optimizer·rollout·기존 log-probability·학습 lineage는 이어받지 않는다. 원본 checkpoint를
보존하고 출처 SHA-256과 변환 ID를 새 파일에 기록한다. 이미 실행 중인 학습 프로세스에는
소스 수정이 자동 반영되지 않으므로 새 경로로 실행해야 한다.

action v6 게임 빌드가 필요하다. v5 게임과의 실시간 연결은 버전 검사에서 거절한다.
기존 v5 SHD3 파일은 오프라인 BC 읽기에 한해 허용하며, 기존 wire mask를 보존한 채
새 작업 label로 변환한다. 기존 설치 폴더의 실행 파일을 자동 교체하지 않는다.

## 8. 검증과 성능 관측

```powershell
python -m unittest test_ranker_entity2_squads test_ranker_entity2_worker_defense test_ranker_entity2_squad_bc test_ranker_entity2_worker_tasks -v
python ranker_entity2_ppo.py --selftest
python ranker_entity2_bc.py --selftest
python ranker_entity2_server.py --selftest
python test_ranker_entity2_squads.py --benchmark
python test_ranker_entity2_squads.py --worker-benchmark
```

2026-09-05 synthetic CPU 측정: 일꾼 12 + 건물 4 + 전투 128(4종), hidden 128,
Torch 1 thread, warmup 뒤 10회 중앙값. 같은 현재 모델을 raw/compact 행으로 비교한다.
추론에는 관측 변환·자동 일꾼 명령 계산·부대 확장을 포함한다.

| 항목 | 개별 유닛 기준 | 종류별 부대 |
|---|---:|---:|
| 정책 row 수 | 144 | 9 |
| 추론 | 33.450 ms | 6.787 ms |
| 확률 재계산 + backward | 80.843 ms | 7.207 ms |

일꾼 128명 + 건물 4개 장면에서는 정책 행이 132 → 5(일꾼 작업 1 + 건물 4)로 줄었다.
같은 측정에서 추론은 24.196 → 7.400 ms, 확률 재계산 + backward는 54.847 → 4.451 ms였다.

[벤치마크 기록](../debug_artifacts/rl_squad_benchmark.json)은 실제 게임의
IPC·simulation·PPO 전체 update 시간을 측정한 값이 아니다. 서버 cohort log의 `rows`는
U 누계, `control_rows`는 D 누계, `squad_rows`는 일반 부대 row 누계다.
`worker_task_rows`는 일꾼 작업 행, `autopilot_workers`는 자동 관리 일꾼 수의 누계다.
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
| 일꾼 위협 대응 회귀 테스트 | [test_ranker_entity2_worker_defense.py](../ranker_reconstructed_code/tools/ai/test_ranker_entity2_worker_defense.py) |
| 일꾼 자동 조종·작업 배정 | [ranker_entity2_workers.py](../ranker_reconstructed_code/tools/ai/ranker_entity2_workers.py) |
| 작업 배정·학습 경계 회귀 테스트 | [test_ranker_entity2_worker_tasks.py](../ranker_reconstructed_code/tools/ai/test_ranker_entity2_worker_tasks.py) |
| 기존 slot 교사 명령 변환 회귀 테스트 | [test_ranker_entity2_squad_bc.py](../ranker_reconstructed_code/tools/ai/test_ranker_entity2_squad_bc.py) |
| Command HEAD 상세 | [RL_COMMAND_HEAD_ROW_BEHAVIOR.md](RL_COMMAND_HEAD_ROW_BEHAVIOR.md) |
| 세 층의 역할·베리 채집 상태 전이·새 봇 설계와 현재 구현의 차이 | [강한 AI 봇 설계서 2.2절](JW2_STRONG_AI_BOT_DESIGN.md#22-세-개의-층) |
