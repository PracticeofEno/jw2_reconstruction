# 가중치 보관과 다른 컴퓨터에서 학습 이어가기

가중치와 Adam 학습 상태는 저장소 루트의 `AI_Weights` 폴더에 로컬로 보관한다.
`AI_Weights/`와 이전 보관 경로인 `training/checkpoints/`는 Git 추적 대상에서 제외한다.
Git에는 복원 도구와 이 안내만 보관한다. 이전 커밋에 올렸던 파일은 과거 Git 기록에는 남아 있다.

## 폴더 구성

현재 보관본은 `AI_Weights/commander_20260911`이다.

| 폴더 | 용도 |
| --- | --- |
| `current/` | 완료된 학습에서 이어서 학습할 모델 |
| `best/` | 종족별 최고 평가 성적 모델 |
| `candidates/` | 아직 평가가 끝나지 않은 후보 |
| `history/` | 학습 재개와 기준 정책 비교에 필요한 이전 모델 |
| `runtime/` | 해당 학습에 사용한 `ranker_rebuild.exe` |
| `records/`, `templates/` | 학습 설정, 평가 기록, 재개 상태 |

가중치 폴더 이름에는 종족과 버전을 넣는다. 예를 들어
`current/1_elf_v2021/`은 엘프의 버전 2021 학습 모델이다.
현재 모델과 최고 모델이 같으면 `current_best/`에 한 번만 보관한다.
각 모델 폴더에는 `policy.bin`(가중치), `policy.bin.json`(설명),
존재하는 경우 `policy.bin.optimizer.npz`(Adam 상태)가 함께 있다.
종족 번호는 0 원시인, 1 엘프, 2 티라노, 3 데몬이다.

| 종족 | 이어 학습할 모델 | 최고 성적 모델 | 평가 중 후보 |
| --- | ---: | ---: | ---: |
| 원시인 | 2020 | 2010 | 2030 |
| 엘프 | 2021 | 501 | 2031 |
| 티라노 | 2022 | 502 | 2032 |
| 데몬 | 2023 | 503 | 2033 |

이 보관본은 `round_001`까지 완료한 시점이다. 복원하면 완료한 두 회차를 건너뛰고
`round_002`의 경기 수집부터 시작한다. 학습률, Adam 상태, 퇴보 횟수, 최고 성적,
모델 버전과 회차별 시드 규칙을 유지한다. 후보 모델의 진행 중 평가나 경기 중간
프레임부터 재개하는 보관본은 아니다. 실행 중인 학습이 진행되어도 이 보관본은 자동 갱신되지 않는다.

## 다른 Windows 컴퓨터로 옮기기

`AI_Weights/commander_20260911` 폴더 전체를 새 컴퓨터의 같은 저장소 상대 경로로
복사한다. 이 폴더는 Git으로 내려받을 수 없으므로 USB나 별도 파일 전송을 사용한다.
원본 게임 데이터와 `Maps`가 들어 있는 `RankerOCPV_Win` 폴더도 준비한다.
가중치만 따로 복사하면 전체 학습 상태를 복원할 수 없다.

이 보관본과 호환되는 코드를 사용한다. 복원 도구는 코드 해시를 확인하며,
줄바꿈 차이는 허용하지만 코드 내용이 달라지면 복원을 거절한다.
Python 3.13 x64를 사용하고 저장소 루트의 PowerShell에서 실행한다.

```powershell
py -3.13 -m venv .venv-ai
.\.venv-ai\Scripts\python.exe -m pip install -r training/requirements.txt

.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py verify AI_Weights/commander_20260911

.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py restore AI_Weights/commander_20260911 --directory debug_artifacts/commander/resumed_20260911 --install RankerOCPV_Win

.\.venv-ai\Scripts\python.exe -u -B ranker_reconstructed_code/tools/ai/ranker_commander_full_ppo.py debug_artifacts/commander/resumed_20260911
```

복원은 새 디렉터리에만 가능하며 학습을 자동 실행하지 않는다. 마지막 명령은
같은 디렉터리에서 실행 중인 컨트롤러가 없는 상태에서 실행한다.
학습용 실행 파일은 보관본 내부에서 사용한다. 배포 실행 파일은 교체하지 않는다.
학습 중에는 복원 결과가 참조하는 보관본을 이동하거나 수정하지 않는다.
과거 평가 기록의 원래 컴퓨터 경로는 출처 기록이며, 과거 rollout을 복사할 필요는 없다.
최소 8 GiB의 여유 공간을 검사하며 새 경기 수집에 필요한 공간은 별도로 확보한다.

## 나중에 최신 학습 상태 보관하기

새 이름을 지정해 내보내면 완료된 회차의 가중치와 Adam 상태를 읽어 용도·종족·버전별로
모은다. 진행 중인 원본 학습 파일은 이동하지 않는다.

```powershell
.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py export debug_artifacts/commander/ppo_full_policy_20260911 AI_Weights/commander_NEXT
.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py verify AI_Weights/commander_NEXT
```

새 보관본 역시 Git에서 제외된다. 다른 컴퓨터로 옮길 때는 해당 폴더 전체를 복사한다.
