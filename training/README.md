# 다른 컴퓨터에서 Commander 학습 이어가기

`checkpoints/commander_20260911`은 2026-09-11 전체 정책 PPO 학습의 이식 가능한
스냅샷이다. 가중치, Adam 상태, 학습 설정, 종족별 학습률, 평가 결과와 최고 모델,
기준 정책, 학습에 사용한 `ranker_rebuild.exe`를 함께 보관한다.
바이너리는 Git LFS로 추적한다. 게임 원본 데이터와 대규모 rollout 파일은 포함하지 않는다.

| 종족 | 재개할 학습 모델 | 최고 성적 모델 | 평가 중인 후보 보관본 |
| --- | ---: | ---: | ---: |
| 원시인 | 2020 | 2010 | 2030 |
| 엘프 | 2021 | 501 | 2031 |
| 티라노 | 2022 | 502 | 2032 |
| 데몬 | 2023 | 503 | 2033 |

스냅샷의 완료 지점은 `round_001`이다. 복원하면 완료한 두 회차를 건너뛰고
`round_002`의 새 경기 수집부터 시작한다. 진행 중이던 `round_002`의 후보 가중치와
Adam 상태도 보존하지만, 끝나지 않은 평가를 통과한 것으로 처리하지 않는다.
학습률·퇴보 횟수·최고 성적·모델 버전·회차별 시드 규칙을 유지한다.
진행 중인 경기/rollout의 중간 프레임까지 재개하는 스냅샷은 아니다.

## 새 Windows 컴퓨터에서 복원

저장소와 Git LFS를 설치하고, 원본 게임 데이터가 있는 `RankerOCPV_Win` 폴더를 준비한다.
Python 3.13 x64를 사용한다. 아래 명령은 저장소 루트의 PowerShell에서 실행한다.

```powershell
git lfs install
git lfs pull
py -3.13 -m venv .venv-ai
.\.venv-ai\Scripts\python.exe -m pip install -r training/requirements.txt

.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py verify training/checkpoints/commander_20260911

.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py restore training/checkpoints/commander_20260911 --directory debug_artifacts/commander/resumed_20260911 --install RankerOCPV_Win

.\.venv-ai\Scripts\python.exe -u -B ranker_reconstructed_code/tools/ai/ranker_commander_full_ppo.py debug_artifacts/commander/resumed_20260911
```

복원은 새 디렉터리에만 가능하며 학습 프로세스를 자동으로 실행하지 않는다.
이미 같은 디렉터리에서 실행 중인 컨트롤러가 없는지 확인하고 마지막 명령을 실행한다.
게임 실행과 파일 잠금은 Windows 환경을 사용한다. 최소 8 GiB의 여유 공간을 검사하며,
추가 경기 수집에 필요한 저장 공간은 별도로 확보한다.

복원 도구는 새 저장소와 게임 설치 경로로 실제 학습 입력을 연결하고,
가중치와 Adam 상태의 버전·해시를 검사한다. 코드 해시는 줄바꿈 차이를 허용하지만
코드 내용의 변경은 거절한다. 이 스냅샷을 수정 없이 보관하고, 복원 결과가 참조하는
스냅샷 디렉터리를 학습 중 이동하거나 삭제하지 않는다.
과거 평가 기록 안의 원래 컴퓨터 경로는 출처 기록이며, 과거 rollout을 복사할 필요는 없다.

학습용 실행 파일은 스냅샷 내부에서 사용한다. 배포 폴더의 실행 파일을 교체하지 않는다.
평가 중 후보들은 `templates/pending_candidates.json`, 모든 파일 위치·SHA-256은
`manifest.json`에서 확인할 수 있다. CPU 환경에서 복원 확인을 수행했으며,
다른 하드웨어에서 새 학습 결과가 비트 단위로 같다는 보장은 하지 않는다.

## 나중에 갱신된 체크포인트 등록

진행 중인 학습도 완료된 회차의 불변 파일을 읽어 새 스냅샷으로 내보낼 수 있다.
기존 스냅샷을 덮어쓰지 말고 새로운 이름을 지정한다.

```powershell
.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py export debug_artifacts/commander/ppo_full_policy_20260911 training/checkpoints/commander_NEXT
.\.venv-ai\Scripts\python.exe -B ranker_reconstructed_code/tools/ai/ranker_commander_checkpoint.py verify training/checkpoints/commander_NEXT
git add training/checkpoints/commander_NEXT
git commit -m "Save Commander training checkpoint"
git push
```

과거 실험 전체나 게임마다 복제된 가중치를 무제한으로 등록하지 않고, 재개에 필요한
학습 계보와 최신 후보를 스냅샷마다 함께 보관한다.
