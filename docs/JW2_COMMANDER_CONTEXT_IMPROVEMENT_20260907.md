2026-09-07 학습 관측 및 재개 설정 개선

이 작업은 [정체 점검](JW2_COMMANDER_LEARNING_PLATEAU_AUDIT_20260907.md)에서 확인한 입력 누락을 보완한다. 교사가 사용하는 자기 재편성 이력과 자기 전략을 모델에도 전달하고, 학습 재개 설정 복원과 장기 보상 대조에 필요한 설정을 추가했다. 기존 작업 중이던 변경은 보존했다.

구현·빌드·관련 검증은 완료했다. 다만 동일 조건의 짧은 BC 파일럿을 총 96경기 비교한 결과, 새 입력 후보는 대조군의 21/48승보다 낮은 **20/48승**이고 시간 제한 종료도 2→5회로 늘었다. 이번 후보는 채택하지 않았으며 기존 가중치와 배포 실행 파일을 보존했다. 학습 정체가 해결됐다고 판단하지 않는다.

이 결과를 출발점으로 기존 강한 정책과 전체 학습 자료를 복원하고 실제 경기 검증을 이어간 내용은 [후속 개선 실험](JW2_COMMANDER_CONTEXT_RECOVERY_20260907.md)에 기록한다.

**새 관측 계약**

기존 `vector[0:528]`의 뜻과 순서는 그대로이며 다음 14개를 추가했다. `CommanderInput`, C++ 첫 선형층, Python 모델, compact RLO가 모두 542차원을 사용한다. 새 schema CRC는 `0xDA97FD92`, RLO format은 3, 레코드는 3,550바이트다. 가중치 컨테이너 format 1은 유지하고 schema와 텐서 차원으로 구분한다.

| 인덱스 | 의미 | 인코딩 |
|---|---|---|
| 528–531 | MAIN→GUARD, GUARD→MAIN, MAIN→RAID, RAID→MAIN 이후 경과시간 | `clamp((frame-last_frame)/6000,0,1)` |
| 532–535 | 각 이동을 실제 수행한 적 있는지 | 0 또는 1 |
| 536 | 자기 교사 초기 주력 생산 수 | `opening_velocis/8` |
| 537 | 자기 교사 타워 시점 | `tower_frame/5000` |
| 538 | 자기 교사 공격 전력비 | `attack_ratio/1.5` |
| 539 | 자기 교사 확장 시점 조정 | `expansion_shift/3000` (음수 포함) |
| 540 | 자기 교사 습격 간격 | `harass_period/6000` |
| 541 | 자기 교사 목표 우선순위 | `target_priority/2` |

아직 이동하지 않았다면 `last_frame=0`, 수행 여부=0이다. 경과시간은 현재 프레임으로 계산하므로 실제 시각 0과 초기 상태를 혼동하지 않는다. 기록은 행동 실행 **전** 상태다. 이 결정에서 실제로 병력이 옮겨졌을 때 다음 관측부터 시각이 반영된다. 모든 특징은 기존과 동일하게 추론 전에 float16으로 양자화한다.

교사 전략은 자기 owner에게 지정된 `-AITEACHERVAR`의 파라미터다. 기본 정책 실행은 variant 0으로 조건화된다. 상대 owner의 교사 파라미터를 자기 입력으로 가져오지 않는다. 교사나 실행기의 공성·채집·전투 규칙을 이 변경과 섞지 않았다. 앞선 근접 공성 후보는 평가 50경기를 마쳤으나 전체 교사 성적이 41→38/48승이고 집중 조건도 절단이 남아 있어 채택하지 않았다.

**기존 자료와 가중치 변환**

`tools/ai/ranker_commander_context.py`가 별도의 출력 파일을 만든다. RLO 변환 대상은 기존 528차원 compact format 2(레코드 3,522바이트)다. 기본 로더는 오래된 schema를 거부하며, legacy 읽기는 명시적인 변환 경로에서만 허용한다. 과거 파일을 새 schema로 오인하는 묵시적 padding은 하지 않는다.

```powershell
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_context.py weights old.bin context.bin
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_context.py rollout old.rlo context.rlo --job original_job.json
```

원래 job 메타데이터가 없다면 `--teacher-variant N`으로 원본 교사의 변형을 명시해야 한다. 변환기는 프레임 1에서 시작하는 기록과 `previous_action`을 확인하고, **실제 행동**으로 네 시각을 복원한 뒤 교사 라벨을 다룬다. DAgger 라벨을 수행 이력으로 사용하지 않는다. 새 기록의 첫 528개 특징·실제 행동·확률·종국 상태를 보존하고 교사 라벨 파일은 바이트 그대로 복사한다. `.context.json`에 출처와 해시를 저장한다. 대상 파일이 이미 있으면 덮어쓰지 않는다. 원본에 라벨이 없어도 대상 이름의 `.teacher.bin`이 남아 있으면 거부한다. 가중치 대상의 `.json`·`.optimizer.npz`가 남은 경우도 거부해 과거 라벨·승인·optimizer가 새 결과에 잘못 연결되지 않도록 했다.

가중치 변환은 기존 첫 층의 528열을 그대로 복사하고 새 14열을 0으로 초기화한다. 나머지 텐서는 그대로다. 따라서 추가 학습 전에는 기존 정책 함수를 수치 허용오차 안에서 유지한다. 기존 optimizer와 BC/리그 승인 상태를 이관하지 않으며 새 입력 후보는 다시 검증해야 한다. 실제 기준 BC version 4의 변환 가중치는 `debug_artifacts/commander/context_improvement_20260907/bc_v4_context.bin`에 있다.

`relabel_with_teacher`도 원본 배열을 복사하도록 수정했다. 변환 후 메모리 배열에 교사 라벨을 붙여도 원래 실제 행동·확률을 덮어쓰지 않는다. 이는 변환 기록으로 PPO와 DAgger를 함께 다룰 때 필요한 보존 규칙이다.

**학습 설정**

일반 PPO CLI 재개에서 curriculum, 교사 KL initial/floor/decay, critic warmup을 저장값으로 복원한다. CLI에서 명시한 값은 0을 포함해 우선한다. `--gamma`, `--gae-lambda`를 추가하고 각각 유한한 `(0,1]`, `[0,1]` 범위를 검사한다. 기존 기본값 0.997/0.95는 유지하며 rollout 반환값 계산과 checkpoint에 전달한다. 리그와 exploiter 경로에서도 출발 PPO의 gamma/lambda를 검증·전달·저장한다. 따라서 이후 λ만 바꾸는 실험을 코드 편집 없이 진행할 수 있다. 이번 작업에서 λ 변경에 따른 PPO 승률 향상까지 검증한 것은 아니다.

기존 shaping의 상쇄되는 부호 합은 유지하고 `discounted_abs_step_shape_mean`을 추가했다. 이 값은 실제 학습 배율이 적용된 단계별 shaping 합의 절댓값을 할인해 합산한 평균으로, 누적 상쇄와 중간 신호 크기를 구분하는 진단이다. 보상 배율 자동 조정·승패 보상·기본 PPO 손실은 변경하지 않았다.

**검증 범위**

새 native 관측 검사는 이력·전략 변경이 기존 528개 특징을 바꾸지 않음, 아직 미실행인 상태, 개별 이동 시각, 실행 전후 시각 반영을 확인한다. Python 검사는 명시적 legacy 변환, 출처 확인, DAgger 실제 행동 보존, 새 열 0 초기화와 예측 보존을 확인한다. C++/Python 모델·RLO와 관련 트레이너 검사도 새 크기에 맞춰 실행한다. 오래된 native RLO 테스트의 절단 시 포텐셜 기대값은 이미 사용 중인 말단 포텐셜 0 규칙에 맞춰 바로잡았으며 보상 동작을 새로 바꾼 것이 아니다.

실행 파일은 `build/ranker_rebuild.exe`로 빌드했다. 배포 디렉터리의 실행 파일은 교체하지 않았다. 2,049프레임의 짧은 실행 검사는 파일·특징·확률 계약을 검증하며 실제 승률 평가가 아니다.

- 관련 Python 검사 71개가 실패·오류·건너뜀 없이 통과했다. Native 관측 검사 3그룹과 기존 commander 검사 21그룹도 통과했다. [검사 기록](../debug_artifacts/commander/context_improvement_20260907/related_checks.json)
- 이후 잔여 sidecar 충돌 검사를 보완하고 context 검사 12개를 다시 실행해 모두 통과했다. 앞의 71개 중 기존 context 검사 11개를 포함한 재검증이며, 새 회귀 사례 1개는 교사 라벨·checkpoint 메타데이터·optimizer 세 종류의 잔여 파일 보존을 확인한다. [최종 context 검사](../debug_artifacts/commander/context_improvement_20260907/context_final_checks.json)
- 7개 교사 변형과 4개 시점/이력 조합, 총 28개 사례에서 C++와 Python의 14개 특징이 float32 및 float16 양자화 후 각각 정확히 일치했다. 모델 logit 최대 차이는 `5.96e-8`이다.
- 실제 DAgger 기록 705결정을 변환해 첫 528개 특징·실제 행동 등 원래 필드와 교사 라벨 바이트, 원본 해시 보존을 확인했다. [변환 검사](../debug_artifacts/commander/context_improvement_20260907/migration_real_game_check.json)
- 새 실행 파일로 variant 0/7 각각 2,049프레임·84결정을 수집했다. 실행 중 기록한 추가 특징과 복원 값이 정확히 일치했고 실제 행동 보존도 통과했다. logp 최대 차이 `6.71e-8`, value 최대 차이 `8.35e-7`이다. [실행 검사](../debug_artifacts/commander/context_improvement_20260907/smoke_results.json)

빌드 SHA-256: `7bce624df018f80b6d35fe941206e637a94a0563bfa5856e5552d5182a11f5ab`.

**동일 조건의 짧은 BC 비교**

기존 BC version 4를 명시적으로 확장한 동일 가중치에서 새 Adam으로 시작했다. 학습은 교사 변형 16개 × 종족 4개에서 한 경기씩 고른 64경기·88,690결정, 검증은 기존 manifest의 보류된 15경기·20,961결정 전체다. 두 후보 모두 seed `20260907`, 3 epochs, minibatch 2,048, lr `0.0003`, 총 132 optimizer step으로 고정했다. 차이는 추가 14개 특징을 0으로 가리는지 여부뿐이다. 학습 전 예측은 같았으며 원본·스냅샷 해시 88개가 보존되었다.

| 검증 지표 | 추가 입력 0 | 추가 입력 사용 |
|---|---:|---:|
| H1 정확도 | 92.3715% | 92.6101% |
| H1 NLL | 0.209042 | 0.204542 |
| 모든 head NLL 합 | 0.370365 | 0.366154 |
| NONE 이외 정확한 행동 수 / 정답 3,075개 | 2,177 | 2,151 |
| NONE 이외 precision / recall | 69.20% / 70.80% | 70.83% / 69.95% |
| 재편성 정확한 행동 수 / 정답 153개 | 71 | 69 |
| 재편성 precision / recall | 40.57% / 46.41% | 42.86% / 45.10% |

전체 H1은 50결정, 0.2385%p 개선됐지만 NONE 이외 정답은 26개 줄었다. 전체 정확도 상승에는 NONE 정답 증가가 포함된다. MAIN→GUARD 정답은 검증 자료에 1개뿐이어서 해당 이동을 충분히 평가할 수 없다. 이 비교는 기존 가중 BC의 클래스 가중치를 사용하지 않고 **양쪽 모두** `bc_rare_weight=0`, `bc_class_power=0`으로 맞췄다. 따라서 학습 전 71.97%에서 학습 후 92%대로 오른 변화 전체를 새 입력 효과로 해석하면 안 된다. 원래 기준 모델을 교체할 근거로도 삼지 않는다.

결과는 [offline_pilot/results.json](../debug_artifacts/commander/context_improvement_20260907/offline_pilot/results.json), 표본·설정은 [plan.json](../debug_artifacts/commander/context_improvement_20260907/offline_pilot/plan.json), 해시와 새 열 갱신 검사는 [verification.json](../debug_artifacts/commander/context_improvement_20260907/offline_pilot/verification.json)에 보존했다. 두 출력 가중치는 version 5의 미승인 BC 후보이며 기존 정책이나 배포 파일을 덮어쓰지 않았다.

같은 검증 궤적에서 `rebalance_ready=false`인 3,714결정도 따로 점검했다. 두 후보 모두 이 상태에서 MAIN↔GUARD를 예측한 경우가 0개였다. 전체 MAIN↔GUARD 예측은 94→84개, GUARD→MAIN 정답은 50/92→48/92였다. 교사에는 이 준비 조건을 우회하는 GUARD→MAIN 정답이 5개 있으므로 준비 미충족 자체를 불법으로 취급하지 않는다. 이 지표의 개선은 관찰되지 않았고, 자기 실행 궤적에서의 효과도 아니다. 입력 누락의 존재와 기존 정체의 주원인이라는 주장은 구분한다. [시점 진단](../debug_artifacts/commander/context_improvement_20260907/diagnostics/rebalance_readiness.json)

**실제 게임 비교 방법**

같은 새 빌드에서 두 BC 후보를 각각 고정 48조건으로 실행했다. 조건은 기존 `bc_reference_argmax/evaluation.json`의 종족 4개 × 서로 다른 시작점 쌍 12개를 그대로 사용했으며 새 seed 탐색은 하지 않았다. 양쪽 모두 가중치 version 5, argmax, C2, 최대 60,000프레임, 상대 감속 0, 자기 교사 전략 0, DAgger 기록 활성화다. 게임별 `AINET=300+job_index%4`도 맞추고 4개씩 실행했다. 입력·실행 파일·설치 자원은 해시로 고정하고 게임별 결과와 기록 파일을 보존했다. 무효나 마스크 위반이 나오면 진행 중인 묶음까지만 마치고 이후 작업을 중단하는 규칙을 두었으며, 해당 중단 사유는 발생하지 않았다.

게임 중에는 두 모델 모두 같은 542차원 관측을 받는다. 대조군의 입력 가림은 오프라인 학습 때 적용했고, 그 결과 첫 층의 새 14열이 모두 0으로 유지됨을 확인했다. 따라서 게임 중 별도 가림 분기 없이도 해당 정보는 대조군의 출력에 영향을 주지 않는다. 새 입력 후보의 14열 중에는 3,131개 가중치가 0이 아닌 값으로 학습됐다.

이 대조의 직접적인 비교 대상은 **이번에 같은 조건으로 학습한 두 최종 BC 후보**다. 기존 BC의 과거 41/48승은 참고 성적이며, 이번 실행에서 기존 가중치를 다시 평가한 결과는 아니다. 학습 자료량·가중치 설정·실행 파일 등이 다른 결과와 입력 효과를 섞지 않는다. 고정 조건 48개의 승수 차이로 미지의 맵·사람 상대 성능이나 통계적으로 확정된 일반화 향상을 주장하지 않는다. 실행 계획은 [gameplay_comparison/plan.json](../debug_artifacts/commander/context_improvement_20260907/gameplay_comparison/plan.json)에 있다.

**완료된 게임 결과와 판단**

| 지표 | 추가 입력을 사용하지 않는 대조군 | 새 입력 후보 |
|---|---:|---:|
| 유효 경기 | 48 | 48 |
| 승 / 패 / 시간 제한 종료 | 21 / 25 / 2 | 20 / 23 / 5 |
| 승률 | 43.75% | 41.67% |
| 원시인 승 / 12 | 7 | 4 |
| 엘프 승 / 12 | 9 | 9 |
| 티라노 승 / 12 | 2 | 3 |
| 데몬 승 / 12 | 3 | 4 |
| 무효 / 마스크 위반 | 0 / 0 | 0 / 0 |
| silent rejection 합계 | 3 | 3 |

승리로 바뀐 조건은 7개, 승리를 잃은 조건은 8개로 순 −1승이다. 잃은 8승 중 2개는 시간 제한 종료로 바뀌었다. 원시인의 감소를 티라노·데몬의 소폭 증가가 만회하지 못했다. 전체 게임 비교는 37분 19.8초에 정상 종료했고, 입력 1,299개의 해시가 모두 보존됐다. 종료 후 경기 산출물 384개의 해시와 두 후보의 실행 명령이 가중치·출력 경로 외에는 같음도 확인했다. [완료 결과와 조건별 증감](../debug_artifacts/commander/context_improvement_20260907/gameplay_comparison/results.json), [종료 검산](../debug_artifacts/commander/context_improvement_20260907/gameplay_comparison/closure_verification.json)

이번 짧은 BC 대조에서는 새 입력의 경기 성능 개선을 확인하지 못했다. 오프라인 H1 정확도 상승으로 학습 정체 해결이나 정책 승격을 판단하면 안 된다. 검증된 관측·변환·재개 설정 코드는 보존하되, 두 BC 파일럿은 미승인 실험 산출물로 남기고 배포·기존 모델 교체·자동 장기 PPO 학습은 하지 않았다.

후속 성능 실험에서는 먼저 기준 BC의 클래스 가중치와 DAgger 자료 구성을 유지한 대조가 필요하다. 기존 정책의 성능을 보존하면서 새 입력을 학습시키는 조건부터 평가해야 한다. γ/λ 설정은 이번에 실험 가능하게 만들었지만 λ 변경의 승률 효과는 아직 미검증이며, 같은 출발 가중치와 코호트에서 λ만 바꾸는 별도 대조 대상으로 남는다.
