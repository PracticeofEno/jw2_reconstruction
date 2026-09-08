# 학습 정체 재점검과 대조 실험 — 2026-09-07

이 작업은 [앞선 관측 보완 실험](JW2_COMMANDER_CONTEXT_IMPROVEMENT_20260907.md)의 낮은 성능에서 이어진다. 목표는 현재 빌드에서 우수 기준 정책의 성능을 다시 확인하고, 그보다 나은 후보를 실제 경기로 검증하는 것이다. 앞선 20/48승 후보는 출발점으로 사용하지 않는다.

**최종 확인:** 원 PPO v8에서 γ=1.0·GAE λ=0.98로 한 번 갱신한 후보는 동일 개발 조건에서 sampling **32→35승/48경기**, argmax **43→44승/48경기**를 기록했다. 최종 경기·모델·입력 검증은 통과했다. 다만 argmax에 시간제한 종료 2회와 무음 명령 거부 1회가 남았고, 최종 200경기 sampling 및 장기 학습 수렴은 검증하지 않았다. 후보는 미승인 연구 가중치이며 배포본·기본 학습 설정을 교체하지 않았다.

## 확인한 문제와 새 기준

기준 BC version 4는 교사 508,798결정과 DAgger 332,312결정, `bc_rare_weight=8`, `bc_class_power=0.5`, cap 20으로 학습됐다. 앞선 짧은 파일럿은 교사 88,690결정만 사용하면서 희소·클래스 가중치와 DAgger를 제외하고 기존 텐서 전체를 다시 학습시켰다. 두 입력 후보 간의 대조는 같았지만, 강한 기존 정책의 보존을 검증하는 조건이 부족했다. 설정 변경 각각의 기여도는 아직 분리 입증되지 않았다.

검증된 새 실행 파일과 기존 가중치를 0열 확장한 `bc_v4_context.bin`으로 48조건을 재실행했다. 결과는 **41승·7패·시간 제한 종료 0**이다. 종족별로 11/11/9/10승이며 무효·마스크 위반은 모두 0이다. 실행 파일 SHA-256은 `7bce624df018f80b6d35fe941206e637a94a0563bfa5856e5552d5182a11f5ab`이다. [현재 빌드 기준 평가](../debug_artifacts/commander/context_recovery_20260907/baseline/evaluation.json)

## 전체 자료 복원

기존 manifest의 학습 612경기·841,110결정과 보류한 교사 15경기·20,961결정 및 DAgger 36경기·54,428결정을 유지했다. 실제 수행 행동으로 재편성 이력을 복원한 뒤 DAgger 라벨을 적용했다. 교사의 전략 변형도 원본 job 출처로 확인했다.

원본 compact RLO 약 3.01GiB는 그대로 읽고 추가 14개 특징만 float16 캐시 약 24.6MiB로 저장한다. `ContextDataset`은 필요한 minibatch만 복원하며 맵은 원본 uint8로 보관한다. 663개 기록의 actor fingerprint, 검증 seed 및 actor 중복 배제, 실제 `LazyBcBatch`의 셔플·중복 인덱스와 캐시 교체 안전성을 확인했다. [자료 사용법](../debug_artifacts/commander/context_recovery_20260907/DATA_PREPARATION.md), [검증 결과](../debug_artifacts/commander/context_recovery_20260907/loader_verification.json)

## 대조 A: 기존 정책을 고정하고 새 입력 열만 학습

기존 BC의 모든 텐서와 첫 층의 기존 528열을 고정하고 새 14열만 학습한다. 전체 teacher/DAgger 자료와 원래 희소·클래스 가중치를 사용한다. 새 Adam, lr 0.001, 3 epochs, minibatch 2,048이며 손실은 가중 모방 손실과 기준 BC에 대한 KL 계수 10이다. Critic 회귀는 하지 않는다. 매 epoch 기존 텐서와 기존 열이 정확히 보존되고 기존 열의 Adam moment가 0인지 확인한다.

KL은 데이터에 기록된 교사 행동 prefix에 조건화한 각 head의 합이다. 자유 실행 전체 행동 분포의 KL로 해석하지 않는다. 보류한 두 자료에서 macro/squad recall 저하 0.02 이내, KL 0.02 이하, H1 변경률 0.05 이하의 후보 중 가중 NLL이 가장 낮은 것을 게임 평가 대상으로 삼는다. 이 선별 자체가 기준보다 개선됐다는 뜻은 아니다. [학습 계획](../debug_artifacts/commander/context_recovery_20260907/adapter/plan.json)

## 대조 B: 같은 PPO 상태에서 GAE λ만 변경

과거 43/48승이었던 `ppo_dagger2_r1/checkpoint_00004.bin`은 내부 version 8, iteration 3이다. 첫 R2 cohort 48경기·52,186결정과 동일한 초기 Adam을 사용해 λ 0.95와 0.98을 각각 한 번씩 업데이트했다. 각 업데이트는 3 epochs·78 optimizer steps다. 동일 cohort를 다음 업데이트에 다시 사용하지 않았다.

두 후보의 새 입력 14열은 입력·가중치·Adam moment 모두 0으로 유지해 기존 528입력 학습을 유지했다. 초기 전체 출력은 이관 전후 동일하고, 기존 39개 Adam 상태 및 step을 보존했다. Native 저장 확률은 바꾸지 않았으며 Torch 재계산과의 최대 joint logp 차이는 `9.54e-6`, 초기 KL은 `2.50e-9`, clip은 0이다. 후보들은 version 9이며 원본 승인 상태는 이관하지 않았다.

두 후보는 R2의 실제 첫 업데이트 설정인 γ 0.997, lr 0.0001, KL initial/floor/decay 0.05/0.01/100, iteration 4, seed 1000을 공유한다. **출발 version 8에 저장된 KL은 0.5/0.1/100이었다.** 따라서 이 대조는 원래 R2 재개 설정을 재현한 것이며, 원래 강한 KL을 유지하는 실험은 별도로 구분해야 한다. λ별 target이 다르므로 value loss 절대값으로 우열을 판단하지 않는다. [PPO 결과](../debug_artifacts/commander/context_recovery_20260907/ppo_lambda/results.json), [이관·저장 검증](../debug_artifacts/commander/context_recovery_20260907/ppo_lambda/verification.json)

## 경기 평가 원칙

같은 실행 파일·게임 자료·고정 seed/시작점·argmax·C2·60,000프레임 상한을 사용한다. 각 게임의 포트는 `AINET=300+job_index%4`로 고정하고 슬롯별 순차 큐를 사용해 최대 4경기를 병렬 실행한다.

선별 조건은 결과를 보기 전에 `job=12*tribe+4*k+((tribe+k)%4)`, `k=0,1,2`로 정했다. 정확한 indices는 `[0,5,10,13,18,23,26,31,32,39,40,45]`이며 각 종족·슬롯에 3개씩 배정되고 12개 시작점 쌍을 포함한다. 선별 12경기는 승격 게이트가 아니다. 유망한 정책은 검증된 같은 12경기를 재사용하고 남은 36경기를 추가해 48조건을 모두 평가한다. [게임 평가 계약](../debug_artifacts/commander/context_recovery_20260907/GAMEPLAY.md)

## 오프라인 학습과 12경기 선별 결과

입력 열만 학습한 대조 A는 3 epochs를 완료했고 모든 기존 가중치와 입력 해시를 보존했다. 미리 정한 보류 자료 선택 기준은 epoch 1을 골랐다. 해당 후보의 teacher 가중 NLL은 0.745850→0.741973, DAgger 가중 NLL은 1.492277→1.482759이며, DAgger 재편성 재현율은 89.015%→90.645%다. H1 변경 비율은 teacher 0.482%, DAgger 0.252%였다. 기존 행동을 대폭 잊었던 앞선 파일럿과 달리 변화가 작지만, 이 수치만으로 경기 개선을 판단하지 않는다. [전체 epoch 결과](../debug_artifacts/commander/context_recovery_20260907/adapter/results.json)

| 같은 12조건 | 승 | 패 | 시간제한 종료 | PPO v8 대비 승패 전환 |
|---|---:|---:|---:|---|
| 기존 BC v4 | 11 | 1 | 0 | 참고 기준 |
| 기존 PPO v8 | 10 | 2 | 0 | 기준 |
| 1회 업데이트, λ 0.95 | 10 | 2 | 0 | job 23 회복, 40 상실 |
| 1회 업데이트, λ 0.98 | 11 | 1 | 0 | job 23·32 회복, 40 상실 |

모든 선별 실행의 무효 경기와 마스크 위반은 0이다. λ 0.98은 같은 업데이트의 λ 0.95보다 job 32를 추가로 이겼다. 이를 근거로 λ 0.98과 기존 PPO v8의 전체 48조건 비교를 진행했으며, 아래 전체 결과에서 선별의 개선이 유지되지 않았음을 확인했다. [선별 결과](../debug_artifacts/commander/context_recovery_20260907/screen/ppo_lambda_098/evaluation.json)

같은 MC 최종 보상 목표를 사용한 cohort 내 explained variance는 PPO v8 0.75583, λ 0.95 0.76529, λ 0.98 0.77859였다. 패배 경기에서는 각각 0.23050, 0.27055, 0.31803이다. 이는 학습에 쓴 자료에 대한 진단이며 새로운 경기에서의 일반화 증거가 아니다. [공통 목표·보류 자료 분석](../debug_artifacts/commander/context_recovery_20260907/ppo_lambda/post_training_diagnostics.json)

별도로 λ 0.98을 유지하고 교사 KL initial/floor만 0.05/0.01에서 원래 0.5/0.1로 복원한 후보를 한 번 업데이트했다. 교사 KL 평균은 0.051447→0.027738로 줄었지만 출발 PPO에 대한 최종 KL은 0.008200→0.012766으로 증가했다. BC 기준점에 가까워지는 것이 강한 PPO 출발 정책의 보존과 같지는 않다. 이 후보의 선별은 10승 2패였고 채택하지 않았다. [강한 KL 대조](../debug_artifacts/commander/context_recovery_20260907/ppo_anchor/results.json)

## 전체 48조건 대조: λ 0.98 후보 기각

| 정책 | 승 | 패 | 시간제한 종료 | 종족 0/1/2/3 승수 |
|---|---:|---:|---:|---|
| 기존 BC v4 | 41 | 7 | 0 | 11/11/9/10 |
| 기존 PPO v8 | 43 | 5 | 0 | 12/10/11/10 |
| 1회 업데이트, λ 0.98 | 41 | 7 | 0 | 11/11/12/7 |

각각 48개의 유효한 조건이며 무효 경기와 마스크 위반은 0이다. PPO v8과 λ 0.98은 선별의 12경기를 검증 후 재사용하고 각각 나머지 36경기를 새로 실행했다. λ 0.98은 기존 패배 job 23/32/37/43을 회복했지만 기존 승리 job 4/36/40/42/44/47을 잃어 순 2승 감소했다. 마지막 종족에서 10→7승으로 떨어졌다. 선별의 10→11승이 전체 개선을 보장하지 않았으므로 후보를 채택하지 않았다. [기준 PPO 전체](../debug_artifacts/commander/context_recovery_20260907/full/ppo_v8_reference/results.json), [λ 0.98 전체](../debug_artifacts/commander/context_recovery_20260907/full/ppo_lambda_098/results.json)

독립 검증 스크립트는 완료 상태, 정확히 한 번씩 실행한 job, 모델·실행 파일·입력 해시, argmax, 실제 RLO/JSONL/부대 슬롯 로그, 종료값과 비교 기준의 증거를 다시 확인한다. 48조건의 seed 12개 중 11개가 기존 teacher 및 DAgger 학습에 각각 등장한다. 이번 PPO 업데이트 cohort와의 seed 교집합은 0이지만 시작 조건은 모두 학습에서 등장했다. 이 평가는 **고정 개발 benchmark**이며 미관측 경기 holdout으로 표현하지 않는다. [독립 검증](../debug_artifacts/commander/context_recovery_20260907/audit_gameplay_phase.py)

승패가 바뀐 job 23/32/40은 첫 행동 차이까지 저장된 관측과 이전 명령이 정확히 일치했다. 회복한 두 경기의 첫 차이는 MASOS 생산이었고, 잃은 경기의 첫 차이는 GUARD HOLD 명령을 새로 내리지 않은 것이다. 결과를 보고 고른 사례이며 그 한 명령이 승패의 원인이라고 확정하지 않는다. [명령 차이 분석](../debug_artifacts/commander/context_recovery_20260907/diagnostics/FIRST_ACTOR_DIVERGENCES.md)

미승인 실험 가중치는 현재 게임 실행에 사용할 수 있지만 일반 production CLI로 optimizer·설정을 이어받는 학습 재개는 직접 지원되지 않는다. 향후 실험 재개에는 정확한 가중치·Adam·config를 복원하는 명시적 연구용 경로와 해당 후보로 새로 수집한 sampled cohort가 필요하다. 기존 admission을 복사하거나 argmax 평가 기록을 PPO 학습에 재사용하지 않는다. [재개 경로 점검](../debug_artifacts/commander/context_recovery_20260907/ppo_lambda/CLI_RESUME_REVIEW.md)

입력 보완 epoch 1의 선별 결과는 **10승 2패**로 BC v4의 같은 12조건 11승 1패보다 낮았다. 승리 회복 없이 job 40을 잃었으며 무효·시간제한 종료·마스크 위반은 0이다. 보류 자료의 재현율 개선이 경기 개선으로 이어지지 않아 이 후보도 채택하지 않았다. [입력 보완 선별](../debug_artifacts/commander/context_recovery_20260907/screen/adapter_epoch_01/results.json)

미사용 seed를 이용한 후속 평가를 검토했지만, 엔진 확인 결과 같은 종족·시작 위치의 argmax에서 seed만 바꾸는 것은 새로운 결정론적 조건이 아니었다. `-SEED`는 슬롯·시작 위치 셔플과 정책 sampling 초기화에 쓰이며, 이번 argmax에서는 sampling을 하지 않고 gameplay RNG는 별도 상태다. `ranker_commander_eval.py`도 이를 명시한다. 따라서 새 seed 계약과 경기를 추가하지 않았고, 현재 결과는 해당 48개 개발 조건의 성적으로 한정한다.

## 후속 대조: 기준 정책 보존

강한 교사 KL에서 λ 0.95를 사용하는 후보도 같은 출발 가중치·Adam·cohort로 한 번 업데이트했다. 이로써 약한/강한 교사 KL과 λ 0.95/0.98 조합을 구분한다. 또 강한 KL·λ 0.98 설정을 유지하되 KL 참조만 기존 BC에서 동결한 출발 PPO v8로 교체하는 대조를 준비했다. 교사에 가까워지는 것이 현재 최고 정책 보존과 같지 않다는 진단을 직접 검증하려는 것이다. 새로운 cohort 없이 같은 자료를 두 번째 업데이트에 사용하지 않으며, 모든 후보는 같은 초기 상태에서 별도로 출발한다.

강한 교사 KL의 λ 0.95와 0.98은 모두 선별 10승 2패로 기존 PPO와 동률이었다. λ 0.95는 job 23을 회복하고 31을 잃었으며, λ 0.98은 23을 회복하고 26을 잃었다. 출발 PPO를 참조한 후보는 선별 11승 1패였지만 전체에서는 **36승 10패 2시간제한 종료**로 퇴행했다. job 23/32를 회복하고 4/8/27/28/35/36/40/44/47을 잃었으며 job 20/43은 상한 60,000프레임에 도달했다. 모든 경기·입력 보존 검증은 통과했다. 기존 PPO에 대한 cohort KL이 0.005782로 작아도 경기 성능을 보장하지 않았다. 이 후보도 채택하지 않는다. [전체 결과](../debug_artifacts/commander/context_recovery_20260907/full/ppo_incumbent_kl_098/results.json), [참조·모델·전체 증거 감사](../debug_artifacts/commander/context_recovery_20260907/audits/anchor__full__ppo_incumbent_kl_098.json)

## 평가 보완: 모델 버전과 독립적인 정책 난수

학습 커맨더의 기본 추론은 sampling이며 `-AIDETERMINISTIC`일 때만 argmax다. 학습 커맨더는 self-play 전용 진입이며 일반 P2P에 자동 활성화되지 않는다. 설계는 argmax와 sampling을 함께 요구하므로, 위 argmax 회귀로 sampling 성능까지 결론낼 수 없다. 기존 정책 RNG에는 `SEED*7919 + owner*31 + model.version`이 들어가 같은 게임 seed여도 모델 버전이 다르면 처음부터 다른 정책 난수열을 사용한다. [기본 추론·평가 목적 점검](../debug_artifacts/commander/context_recovery_20260907/diagnostics/INFERENCE_MODE_REVIEW.md)

이를 통제하기 위해 다음을 구현했다.

- Native `-AIPOLICYSEED:N`: 명시하면 `rng.seed(N, owner, 0)`, 생략하면 기존 난수열을 보존한다. 명시적 0과 u64 범위를 지원하고 잘못된 값·중복 옵션은 거부한다. 적용한 정책 seed, owner, 버전 salt, 게임 seed와 실제 모델 버전을 로그로 남긴다.
- Python job의 `policy_seed`와 sample CLI `--policy-seed-base N`: 명령에 전달하고 엔진 로그의 실제 적용을 대조한다. 옵션을 무시하는 예전 실행 파일은 유효한 평가로 취급하지 않는다. 같은 정책 seed·시작 위치·종족의 반복 표본을 중복 집계하지 않는다.
- 정책 seed는 초기 난수 상태만 맞춘다. 서로 다른 정책의 행동·조건부 mask가 갈라지면 난수 소비 횟수도 달라질 수 있으므로 이후 매 프레임/head의 난수까지 정렬됐다고 주장하지 않는다. 게임 RNG, 모델·RLO schema, 보상과 학습 손실은 바꾸지 않았다.

Python 관련 18개 테스트와 native launch/RNG 회귀를 통과했다. 새 통합 빌드는 `build/policy_seed/ranker_rebuild.exe`, SHA-256 `9bbe57336169714a45afa7e3ebad51bb0048352108d91b6170d7f1a7b08e6c40`이다. 기존 argmax 실행 파일과 모든 완료 기록을 보존했다.

실제 통합 검증에서는 같은 tensor를 version 8과 9로 저장하고 각각 policy seed 0으로 4,097프레임을 실행했다. **172개 결정과 종료 레코드의 행동·관측·mask·logp·value·보상 등이 바이트 단위로 일치**했다. 버전 필드와 그에 따른 CRC만 비교에서 제외했다. 짧은 상한 종료는 예상된 기능 검증이며 승률 표본으로 세지 않는다. [2게임 검증](../debug_artifacts/commander/context_recovery_20260907/policy_seed_validation/results.json)

이 빌드에서 기존 PPO v8과 약한 교사 KL·λ 0.98 후보를 같은 48조건의 sampling으로 비교한다. 각 job의 정책 seed는 `2026090700 + index`이며, 모델 버전과 독립적인 같은 초기 정책 난수 상태를 준다. 이 대조는 argmax 결과와 합산하지 않는다.

기준 PPO v8의 sampling은 **32승 15패 1시간제한 종료**로 완료됐다. 종족별 10/8/7/7승이며 job 12가 60,000프레임 상한에 도달했다. 무효·마스크 위반·무음 거부는 0이다. 독립 감사에서 엔진의 정책 seed 적용, 실제 시작점, 48경기의 증거와 2,475개 입력 보존을 확인했다. [기준 sampling 감사](../debug_artifacts/commander/context_recovery_20260907/audits/sampling__ppo_v8_reference.json)

약한 교사 KL·λ 0.98 후보는 **33승 15패 0시간제한 종료**로 완료됐다. 종족별 8/9/7/9승이며 무효·마스크 위반·무음 거부는 모두 0이다. 기준 대비 job 8/12/13/23/26/30/36/37/47을 회복하고 1/3/4/14/15/25/32/41을 잃었다. 순 1승 증가로 일반적인 우위를 입증하지 못하며, argmax의 43→41승 회귀도 남아 있어 채택하지 않는다. 독립 감사는 기준 48경기까지 다시 읽고 2,768개 입력과 paired 전이를 확인했다. [후보 sampling 감사](../debug_artifacts/commander/context_recovery_20260907/audits/sampling__ppo_lambda_098.json)

## 후속 원인: 시간 할인 보상과 승수의 불일치

실제 RLO 192경기를 보상 구현과 대조했을 때 terminal 부호나 GAE 차분 방향 오류는 발견하지 않았다. 그러나 32프레임마다 적용하는 γ 0.997의 할인 반감기는 약 7,382프레임이다. 20,000/40,000/60,000프레임의 terminal 할인 배율은 약 15.29%/2.34%/0.36%다. 빠른 승리 보너스까지 포함하면 20,000프레임 승리의 할인 terminal은 40,000프레임 승리보다 약 7.13배 크다. 늦게 패배해도 손실이 크게 줄어든다.

완료한 argmax 48경기를 같은 공식으로 다시 계산하면 기존 PPO는 **43승, 할인 terminal 평균 0.163511**, λ 0.98은 **41승, 0.171991**이다. 후보가 2승을 잃었는데 할인 terminal 평균은 약 5.19% 높았다. 할인 전 terminal 평균은 반대로 0.969236→0.883202로 낮아졌다. 빠른 승리 보너스를 빼고 승패 ±1에 같은 할인만 적용해도 순위 역전은 남았다. 이는 사후 경기 분석이며 sampled cohort의 PPO surrogate 최적화가 회귀의 원인이라고 입증하지는 않는다. 다만 현재 보상이 주요 평가 지표인 승수와 다른 정책을 선호할 수 있다는 직접적인 증거다. [보상 재계산과 한계](../debug_artifacts/commander/context_recovery_20260907/diagnostics/REWARD_HORIZON_REVIEW.md)

이를 검증하기 위해 약한 교사 KL·λ 0.98 후보와 동일한 초기 PPO v8·Adam·cohort·학습 설정에서 **γ만 0.997→1.0**으로 바꾼 후보를 한 번 학습했다. 3 epochs·78 Adam steps, 최초 확률 및 optimizer 일치, 원본 입력 보존과 새 14열·moments 0을 확인했다. 모델 SHA-256은 `3cce39f51f2dee78096471d0cadf410927818c40f81748caa6581ac1b615bc81`이다. [학습 검증](../debug_artifacts/commander/context_recovery_20260907/ppo_undiscounted/verification.json)

이 후보도 같은 48조건의 sampling으로 기준 PPO 및 γ 0.997 후보와 비교했다. 기존 평가 계약은 보존하고 γ 전용 보충 계약을 사용했다. 빠른 승리 보너스와 timeout bootstrap은 유지하므로 순수한 승률 보상 실험은 아니다. 초기 critic은 γ 0.997로 학습돼 있었으므로 단 한 번의 업데이트로 γ 1.0에 충분히 적응했다고 볼 수 없다. 기본 γ는 아직 0.997이며 후보는 미승인 상태다.

γ 1.0·λ 0.98의 sampling 48경기 결과는 **35승 13패 0시간제한 종료**다. 종족별 9/10/9/7승으로 기준 PPO보다 순 3승, γ 0.997·λ 0.98보다 순 2승 많았다. 기준에서 job 12/13/23/26/29/30/35/47을 회복하고 1/15/25/32/41을 잃었다. 무효·마스크 위반·무음 거부는 0이고 모든 입력·참조 해시를 보존했다. 이 48경기의 승수 증가는 기록하되 통계적인 우위나 최종 200경기 평가 통과로 표현하지 않는다. [할인 제거 sampling 결과](../debug_artifacts/commander/context_recovery_20260907/sampling_gamma/ppo_undiscounted_098/results.json)

독립 감사는 기존 두 sampling 정책의 96경기도 다시 읽어 총 3,089개 입력과 후보의 48개 경기 증거를 검증했다. γ 0.997·λ 0.98 대비 승리 회복은 5개, 상실은 3개로 순 2승 증가와 일치했다. [할인 제거 sampling 감사](../debug_artifacts/commander/context_recovery_20260907/audits/sampling_gamma__phase.json)

## 최종 보상 전달을 분리하는 MC 대조

γ 1.0에서도 λ 0.98이면 20,000프레임 앞 advantage에 마지막 TD error가 직접 전달되는 계수는 약 `0.98^(20000/32) = 0.00000328`이다. 그보다 앞의 행동을 평가하려면 critic의 중간 예측이 중요하다. 기존 critic이 다른 γ로 학습됐다는 한계를 분리하기 위해, 같은 초기 PPO v8·Adam·cohort에서 γ 1.0을 유지하고 **λ만 0.98→1.0**으로 바꾼 후보도 별도로 한 번 학습했다. GAE return과 MC return의 최대 차이는 0이었다.

후보는 `ppo_mc/mc_lambda1000.bin`, SHA-256 `dab7d4cb65f6c61aa11b31c0c5b4b3dd5b22fdfc42e149d5ee562bfbd9ffc0e9`이다. 3 epochs·78 steps, 초기 확률·Adam 일치, 입력 보존, 추가 14열과 moments 0을 확인했다. 이 후보는 actor·critic·공유층을 함께 갱신한 MC advantage 실험이며 critic 사전 적합이 아니다. 보너스와 timeout bootstrap은 유지한다. [MC 학습 검증](../debug_artifacts/commander/context_recovery_20260907/ppo_mc/verification.json)

같은 초기 첫 minibatch 2,048개에서 γ 1.0·λ 0.98→1.0으로 바꾸면 공유 actor 층의 value gradient norm은 0.20318→1.43473, actor gradient norm은 1.2800→1.1642였다. value/actor 비율은 0.1587→1.2324이며 두 gradient의 cosine은 0.00288→−0.02122다. MC가 value loss를 통해 공유 actor 표현도 더 크게 바꾸는 경로가 있음을 보여준다. 단일 minibatch의 clipping 전 개별 gradient이며 entropy·KL·Adam 상태를 포함한 실제 업데이트나 성능 회귀의 인과 증거는 아니다. [초기 gradient 진단](../debug_artifacts/commander/context_recovery_20260907/ppo_mc/initial_gradient_diagnostics.json)

MC 후보 역시 동일한 48조건·초기 정책 seed에서 평가했다. 기존 기준 경기는 다시 실행하지 않고 검증된 증거를 참조했으며, 별도 보충 계약으로 후보 및 참조 입력을 고정했다. 결과는 **35승 11패 2시간제한 종료**이고 종족별 11/7/10/7승이다. job 8/24가 60,000프레임에 도달했다. 기준 PPO 대비 7개 승리를 회복하고 4개를 잃어 순 3승 증가했다. γ 1.0·λ 0.98 대비로는 5개를 회복하고 5개를 잃어 동률이었다. 무효·마스크 위반·무음 거부는 모두 0이다. [MC sampling 결과](../debug_artifacts/commander/context_recovery_20260907/sampling_mc/ppo_mc_1000/results.json)

## 같은 초기 정책 난수에서의 sampling 최종 비교

| 정책 | 승 | 패 | 시간제한 종료 | 종족 0/1/2/3 승수 | 기준 대비 회복/상실 |
|---|---:|---:|---:|---|---|
| 기존 PPO v8 | 32 | 15 | 1 | 10/8/7/7 | 기준 |
| γ 0.997·λ 0.98 | 33 | 15 | 0 | 8/9/7/9 | 9/8 |
| γ 1.0·λ 0.98 | 35 | 13 | 0 | 9/10/9/7 | 8/5 |
| γ 1.0·λ 1.0 (MC) | 35 | 11 | 2 | 11/7/10/7 | 7/4 |

각 행은 같은 48개 개발 조건의 결과다. 두 γ 1.0 후보 모두 관측 승률은 66.67%→72.92%, 순 3승 증가했다. 새 입력 열을 실제 학습한 효과, 장기 PPO 반복의 수렴, 새로운 상대·맵에서의 일반화 또는 통계적인 우위를 입증한 결과는 아니다. 200경기 최종 sampling 평가는 시행하지 않았다.

MC 최종 감사는 기존 세 정책을 포함한 네 행의 경기·종족별 성적과 paired 전이를 검증했다. 입력 2,606개 보존을 확인했고 감사 결과 SHA-256은 `d84fb9b83a414e86cba40150cacdc30488bdf40d5097961bae32e3e605f672aa`다. [네 정책 최종 sampling 감사](../debug_artifacts/commander/context_recovery_20260907/audits/sampling_mc__phase.json)

## 선택된 후보의 argmax 회귀 확인

MC의 최종 결과를 보기 전에 후속 선택 규칙을 정했다. 두 γ 1.0 후보 중 sampling 승수가 높은 하나를 선택하고, 동률이면 cohort KL이 더 작은 λ 0.98 후보를 선택한다. 선택된 모델 하나를 기존 실행 파일 `7bce624d…`와 원래 argmax 48조건에서 평가해 기준 PPO의 43승과 비교한다. sampling의 증가만으로 argmax 회귀를 놓치지 않기 위한 검증이며 기존 기준 경기는 재사용한다.

[사전 선택 계획](../debug_artifacts/commander/context_recovery_20260907/argmax_selection_plan.json)은 MC 경기 시작 전에 기록됐고 SHA-256은 `a4de47916fbbc8f514b942d02f764712eb887ff2f413c94322c1b49e4ecc2b1e`다. 별도 사전 감사에서 원 PPO의 43승 5패를 실제 RLO·명령·슬롯·metrics로 재검증하고, 두 후보의 원 v8 텐서·Adam 이관과 설정·학습 출처를 확인했다. [선택·기준 정책 사전 감사](../debug_artifacts/commander/context_recovery_20260907/audits/selected_argmax__preflight.json)

실제 sampling은 두 후보 모두 35승으로 끝나 **γ 1.0·λ 0.98**을 선택했다. 독립 선택 계약 감사에서 2,587개 입력, 기존 argmax 실행 파일·48조건·43승 기준과 선택 모델 SHA를 확인했다. [선택 계약 감사](../debug_artifacts/commander/context_recovery_20260907/audits/selected_argmax__contract.json)

감사 출처 보완도 별도로 기록했다. 새 MC 감사기는 baseline 산출물 해시가 보충 계약이 아닌 실행 전 phase plan에 저장된 것을 반영하도록 수정됐다. 이 때문에 과거 사전 감사에 기록된 보조 감사 스크립트 한 파일의 SHA가 달라졌다. 기존 사전 감사·선택 계약을 바꾸지 않고 새 사전 감사 및 `audit_source_bridge.json`으로 최종 MC 감사 버전을 연결했으며, 모델·훈련·경기 입력은 모두 동일함을 재검증한 뒤 argmax 실행을 진행했다. [갱신한 사전 감사](../debug_artifacts/commander/context_recovery_20260907/audits/selected_argmax__preflight_final_mc_audit.json)

최종 argmax 결과는 **44승 2패 2시간제한 종료**로 기존 PPO의 43승 5패보다 순 1승 많았다. 종족별 승수는 12/10/11/11이다. job 32/37/43을 회복하고 25/38을 잃었다. job 23/25는 60,000프레임 상한에 도달했다. 기존 기준과 같은 실행 파일 `7bce624d…`, 고정 48조건·4슬롯·argmax로 실행했으며 sampling 기록을 argmax 표본으로 재사용하지 않았다. [최종 argmax 결과](../debug_artifacts/commander/context_recovery_20260907/selected_argmax/results.json)

독립 최종 감사는 2,880개 입력, 모델·원 v8/Adam·학습 cohort 출처, 선택 규칙, 48개 실제 경기와 각 6종 산출물, paired 결과 및 summary 재계산을 검증했다. 감사 SHA-256은 `d64081f81a29469d35fc687fdfc9f49ee24f41cfad22c9e4ee28795049f4b055`다. 무효 경기와 마스크 위반은 0이지만 **무음 거부는 1회**다. 데이터 검증 통과는 승격 게이트 통과를 뜻하지 않는다. [최종 argmax 감사](../debug_artifacts/commander/context_recovery_20260907/audits/selected_argmax__phase.json)

선택 후보가 43승 미만일 경우 MC도 argmax로 확인하는 조건부 후속을 준비했지만, γ 1.0·λ 0.98이 44승을 기록해 조건이 성립하지 않았다. MC argmax 게임은 실행하지 않았다. 두 모드 모두에서 개발 승수가 증가한 γ 1.0·λ 0.98 후보와 optimizer·출처를 보존했으며, 일반 production CLI로 미승인 후보를 그대로 재개하는 제한은 앞서 설명한 대로 남는다. 기본 γ 0.997·λ 0.95와 기존 배포 가중치는 유지했다.

## 남은 무음 거부의 범위

최종 후보의 1건은 job 9(seed 10, 종족 0, 시작점 [3,0])에서 frame 3609에 기록한 `build-expired`다. 최초 행동은 frame 3553의 `[14,1,0,0,0,0,0,1]`, 타워 `0x83` 건설이며 frame 3569/3585에 재시도했다. 마지막 worker 81200과 위치 (192,2784)에서 예약이 만료됐다. 같은 frame의 새 행동 15를 원인 행동으로 오인하지 않았다. [해당 로그](../debug_artifacts/commander/context_recovery_20260907/selected_argmax/games/game_00009/output/Jw2.log)

현재 `ranker_ai_commander.cpp`의 예약 점검은 건물이 생성되지 않고 건설 상태도 확인되지 않은 채 시도 3회 및 마지막 시도 후 16프레임 초과 조건을 만족하면 `silent_rejections`를 증가시킨다. 따라서 확인된 것은 예약 만료이며, 패킷 검증 거부나 엔진의 특정 실패 분기가 확인된 것은 아니다. 경로·이동·자원·생성 실패 중 어느 분기였는지 현재 로그는 구분하지 못한다. 이 진단에서 재현 실행이나 코드 수정은 하지 않았다.

기준 PPO의 같은 job 9에서도 동일 사건이 발생했고 그 시점까지 처음 158개 결정의 frame·행동·mask·vector·map이 정확히 일치했다. 기준 전체는 4건(job 8/9/16/28), 새 후보는 1건이며 모두 `build-expired`였다. 지표는 줄었지만 0건 승격 조건은 아직 충족하지 않는다. 미승인 상태를 유지한 이유를 승수 증가와 구분한다.
