# Commander 강화 구현과 검증 현황 — 2026-09-08

현재 최선은 **경제 회복 실행기 + 학습 전 `initial_v9.bin`**이다. 같은 개발 benchmark 48조건의 argmax에서 기존 **44승·2패·시간 초과 2 → 46승·2패·시간 초과 0**을 확인했다. 새로 이긴 경기는 job 23·25이며 기존 승리를 잃은 경기는 없다. 이 정책의 context14·새 vector64·map3 가중치와 residual 출력은 모두 초기의 0 상태다. 확인된 실전 개선은 **경제 회복 실행기의 효과**이며 이번 신규 학습의 효과가 아니다.

이 문서에서 `S`는 `debug_artifacts/commander/strong_bot_20260908`, `A`는 `debug_artifacts/commander/conditional_adapter_20260908`이다. 원본 모델·RLO·실행파일·배포 파일과 각 실험의 frozen 입력은 보존했다.

| 확정된 경기 비교 | 승 / 패 / 시간 초과 | 판단과 근거 |
| --- | ---: | --- |
| 기존 최고 정책, 기존 실행기, argmax 48 | 44 / 2 / 2 | `context_recovery_20260907/selected_argmax`의 고정 기준 |
| **경제 회복 + initial_v9, argmax 48** | **46 / 2 / 0** | **현재 선정 조합**. `S/full_recovery_initial`: 새 승리 23·25, 잃은 승리 0. 남은 패배 20·38. invalid 0, 마스크 위반 0, 기존 job9의 건설 명령 거부 1건. |
| 기존 최고 정책, 기존 실행기, 같은 seed sampling 48 | 35 / 13 / 0 | `context_recovery_20260907/sampling_gamma/ppo_undiscounted_098`의 고정 기준 |
| **경제 회복 + initial_v9, 같은 seed sampling 48** | **39 / 9 / 0** | `S/paired_sampling_selected`: 새 승리 8·15·32·36, 잃은 승리 0. invalid·마스크 위반·명령 거부 모두 0. |
| 경제 회복 + fullslow epoch3, argmax 48 | 42 / 6 / 0 | `S/full_recovery_fullslow3`: 선별 9/12였지만 전체 검증에서 후퇴하여 제외 |
| adapters epoch3, 선별 12 | 7 / 4 / 1 | `S/screen_adapters3`: 같은 선별 조건의 기존 8승보다 낮아 제외 |
| 경제 회복 + residuals-only epoch3, 선별 12 | 9 / 3 / 0 | `S/screen_residuals3`: 현재 선정 조합의 같은 12조건 10승보다 낮아 제외. 20·38에서 승리를 얻었지만 25·26·32의 승리를 잃었다. |
| 전투 자위 실행기 실험, 선별 12 | 8 / 3 / 1 | `S/screen_combat_initial`: 총승수 개선 없이 개별 승리도 잃어 제외. 해당 변경은 production 소스에서 제거했고 경제 회복 소스와 일치함을 확인했다. 실패한 실험의 frozen 자료는 보존했다. |

현재 선정 조합의 같은 seed sampling 48 대조 `S/paired_sampling_selected`까지 완료했다. 정책 seed는 기존 대조와 같은 `2026090700+job index`다. 두 평가 모두 기존 승리를 유지하면서 승수가 늘었다. 이 48조건은 개발에 사용한 benchmark이며 독립 일반화 시험으로 해석하지 않는다. `S/comparison.json`에 경기별 대조와 입력 해시를 기록했다.

| 변경 | 현재 계약과 목적 |
| --- | --- |
| 관측 | 벡터 **542→606**, 지도 **9→12×16×16**. 기존 542열과 9채널의 계산·순서는 유지한다. 추가 64열은 자기 일꾼 할당 기준의 미할당 자원, 포화되지 않는 소득·대기열·병력·분산, 건설·합체 진행 및 재시도, 부대 재집결·미적용 명령·명령 경과시간을 담는다. |
| 지도 | 채널 9에 실제 공개 지형 `placement_class/7` 평균, 10에 현재 가시 비율, 11에 탐사 비율을 추가한다. 기존 상수에 가까웠던 `class>0` 채널의 의미를 덮어쓰지 않는다. |
| 조건부 정책 | 기존 39개 tensor 뒤에 H1–H7별 `(256+8h)→32→행동 수` ReLU residual의 28개 tensor를 추가하여 **67개**가 된다. H0은 유지한다. 앞선 행동과 관측의 비선형 상호작용을 학습할 수 있게 한다. |
| 파일 | architecture `jw2-commander-conv-ar-residual-v3`, weight format **2**, schema CRC **0x53DD6137**. RLO format **4**, record **4,446 bytes**. 구형 파일은 명시적 이관을 거친다. |
| 동시 부대 재편성 | `-AICOORDINATEDTRANSFERS:1`에서 재편성 후 구성원을 기준으로 H2/H3 마스크와 실행기가 같은 계획을 사용한다. 방금 만든 부대에도 같은 결정에서 임무를 줄 수 있다. `0`은 기존 행동을 보존한다. |

필드 순서·정규화·결측값·관측 출처는 [관측 계약](JW2_COMMANDER_OBSERVATION_CONTRACT_20260908.md)에 정의했다. 새 수량·시간 필드는 상한 없는 `log1p` 스케일을 사용한다. 공사 진행은 HP 대신 실제 작업 비율이며, 공사가 있지만 값을 알 수 없으면 -1이다. 공개 지형·탐사 기억·자기 실행기 상태만 actor에 추가하고, privileged 32개 값은 기존처럼 critic 전용이다. 옛 RLO에는 새 실행 상태가 없으므로 0으로 채운 자료를 새 관측의 학습 자료로 취급하지 않는다.

| 완료된 검증 | 근거와 한계 |
| --- | --- |
| 실제 native 이관 | `S/smoke_baseline/parity.json`: 두 10,001-frame 진단 게임의 **973개 결정**에서 이전 실행과 frame/action/mask/event/기존 542열/기존 9채널 일치, logp·value 차이 0. 새 관측은 실제로 기록되었지만 가중치가 0이므로 초기 출력은 유지됐다. |
| 기존 기록에서 Python 이관 | `A/incumbent_upgrade_verification.json`: 기존 48게임 **58,801개 결정**에서 logits/logp/value/entropy 차이 0. 기존 tensor와 Adam 값·step을 보존했다. 과거 관측의 메모리 내 확장은 이 호환성 검사에만 사용했다. |
| 저장·재로드 | `A/published_upgrade_verification.json`: 공개된 초기 정책·BC 참조의 기존 tensor와 출력, Adam 39개 기존 상태 및 28개 새 상태의 초기값 확인. 새 벡터·지도 가중치와 residual 마지막 projection은 0이다. 기존에 추가했던 14열 역시 출발 정책에서 가중치·Adam moment가 0이다. |
| 학습된 모델의 실제 native 추론 | `S/diagnostics/fullslow3_native_parity.json`: 완료된 5게임의 4,096개 결정에서 frozen Python 대비 logp 최대 오차 **3.10×10⁻⁶**, value 최대 오차 **1.07×10⁻⁶**, 기록된 조건부 argmax 불일치 0. 허용 오차 1e-4 이내다. 수치 구현 검증이며 이 후보의 42승을 개선 성적으로 바꾸지는 않는다. |
| 재편성 동작 | `S/smoke_transfers/transfer_mask_analysis.json`: 비어 있던 목적 부대를 대상으로 재편성과 명령을 함께 선택한 **22회**를 확인했다. 그중 다음 관측이 있는 21회는 행동 이력·transfer clock으로 실행을 확인했다. 이는 기능 확인이며 승률 비교가 아니다. 기존 건설 만료 1건은 양쪽 진단에 남아 있어 무거부 달성을 주장하지 않는다. |
| 직접 회귀 | `ai_commander_observation_regression` 4개 그룹, `ai_commander_transfer_regression`, 후속 `ai_commander_economy_recovery_regression` 6개 그룹 통과. CMake의 `RANKER_BUILD_COMMANDER_TESTS=ON`으로 해당 표적을 빌드할 수 있다. |

첫 자료 수집 `S/train_round1`은 `initial_v9.bin`으로 새 native RLO **48게임·53,536개 결정**을 확보했다. 결과는 **36승·10패·시간 초과 2**, invalid 0, 마스크 위반 0, 명령 거부 4건이다. **이 단계는 coordinated transfers=0**이며, 기존 최고 정책의 행동으로 새 관측부터 확보했다. 네 종족×12 시작쌍, 최대 60,000 frame, C2, 새 학습용 정책 seed `2026090800+원래 job index`, 4개 슬롯 `index%4`를 사용했다. 이전 sampling 성적과 seed가 다르므로 이 36승을 개선 성적으로 해석하지 않는다. 드라이버의 명령·실제 시작쌍·버전·seed·결과 해시 검증과 phase 완료 검증을 통과한 자료로 학습했다.

학습 드라이버 `S/train_candidate.py`는 γ=1, λ=.98, minibatch 2,048, 최대 3 epoch로 실행했다. 첫 `full`과 `adapters`의 lr은 1e-4, 후속 `full_slow`는 2.5e-5다. `full`은 이관한 기존 Adam을 이어 쓰고, `adapters`는 의도적으로 새 Adam으로 기존 528열·9채널·기존 actor를 동결하여 나머지 입력·residual·critic을 학습한다. 두 방식은 optimizer와 동결 범위도 다르므로 단독 관측 효과를 분리한 비교로 해석하지 않는다. 각 epoch는 동일한 새 weight version으로 별도 저장하며, 전체 cohort의 KL을 확인했다. 학습된 이 세 후보는 현재 선정 조합에 사용하지 않는다.

| 완료한 학습 | 전체 53,536개 결정에서 다시 계산한 결과 |
| --- | --- |
| `S/training/round1_full/epoch_001.bin` | KL **.04003**으로 상한 .03을 넘어 1 epoch 후 중단. clip fraction **34.07%**, actor loss **+.02108**. 개선된 후보로 취급하지 않는다. |
| `S/training/round1_adapters/epoch_003.bin` | 3 epoch 완료, 기존 actor 동결 확인. KL **.002481**, clip fraction **2.219%**, actor loss **−.003455**. 이후 선별 7/12로 제외했다. |
| `S/training/round1_full_slow/epoch_003.bin` | 3 epoch 완료. KL **.003060**, clip fraction **3.482%**, actor loss **−.009161**. 이후 전체 42/48로 제외했다. |

`S/diagnose_feature_usage.py`와 `S/diagnostics/round1_feature_usage.json`은 seed `2026090801`로 48게임에서 고르게 뽑은 **2,048개 결정**의 동일 관측·기록된 앞선 행동·조건부 마스크를 사용했다. 각 부분을 따로 끄면 다음 수의 결정에서 하나 이상 조건부 head의 argmax가 변했다.

| 끈 부분 | adapters epoch3 | full epoch1 | fullslow epoch3 |
| --- | ---: | ---: | ---: |
| 기존 context14 | 27 | 43 | 12 |
| 새 vector64 | 44 | 141 | 19 |
| 새 map3 | 8 | 9 | 8 |
| H1–H7 residual | 4 | 8 | 2 |
| 위 부분 전체 | 57 | 181 | 20 |

초기 이관 모델은 모든 제거 검사에서 logits/logp 차이가 0이었다. fullslow 추가 결과는 `S/diagnostics/fullslow3_feature_usage.json`이며 표본은 정확히 동일하다. 검사한 모든 모델에서 privileged 입력을 0으로 바꾸어도 actor logits/logp가 유지됐다. 학습된 후보의 새 입력과 residual은 출력에 실제 기여했지만 실전 대조에서 채택할 개선을 만들지 못했다. 0 치환은 비정상 관측을 만들 수도 있으며, 기록된 앞선 행동을 고정한 조건부 변화는 자유 실행한 전체 행동이나 승률 변화를 뜻하지 않는다. 이 표본은 학습 자료이므로 일반화 검증도 아니다.

현재 선정 조합은 아래 두 파일이다. `S/run_recovery_phase.py`가 이 경제 회복 실행파일과 `S/recovery_runtime_contract.json`을 고정하여 실행한다.

| 파일 | SHA256 |
| --- | --- |
| `build/strong_bot_recovery/ranker_rebuild.exe` | `55aeefb77dd8df03a7c5f7220b7851c4d8404d8665fc55240a9d55a1b4c36c9c` |
| `S/initial_v9.bin` | `13ffa091f8f5260515dcecca384fc6861905c4a9334b2fdfcb51f270e26fc44f` |

workspace 루트에서 현재 조합의 평가를 재현하는 명령이다. 두 명령은 순차 실행하며 이미 있는 phase 이름을 재사용할 수 없다. 첫 명령의 조건에서 argmax 46승, 두 번째 조건에서 sampling 39승을 확인했다. 선정 실행파일은 위 build 경로에 있으며 `RankerOCPV_Win`의 배포 실행파일은 교체하지 않았다.

```powershell
$strongExperiment = 'debug_artifacts/commander/strong_bot_20260908'
& .venv-ai/Scripts/python.exe "$strongExperiment/run_recovery_phase.py" --phase reproduce_selected_argmax --weights "$strongExperiment/initial_v9.bin" --mode argmax --jobs full --coordinated-transfers 0
& .venv-ai/Scripts/python.exe "$strongExperiment/run_recovery_phase.py" --phase reproduce_selected_sampling --weights "$strongExperiment/initial_v9.bin" --mode sample --jobs full --policy-seed-base 2026090700 --coordinated-transfers 0
```

최초 관측·학습 자료 수집용 실행파일 `build/strong_bot/ranker_rebuild.exe` (SHA256 `d611a365cb7f8e343514bf238b52d18fde791b32d3f990490328a1db9b42155e`)도 보존했다. Python은 `S/frozen_source`, 최초 native 소스는 `S/native_source`, 현재 경제 회복 native 소스는 `S/recovery_native_source`에 고정했다. 각 phase의 `plan.json`, `state.json`, `evaluation.json`, `results.json`이 입력·실행·완료 증거를 연결한다. sampling 대조에서는 같은 초기 난수열을 맞출 뿐 정책이나 실행 결과가 달라진 뒤 모든 난수 사용 시점이 같다고 보장하지 않는다.

후속 경제 회복은 **별도 실행기 변경**이다. 최근 수입이 5 미만이고 완료된 모든 자기 본부 주변에 탐사된 자원 타일이 없을 때만 일꾼의 탐색 반경을 768→1,536으로 늘린다. 적 위협·최근 습격·자원 타일당 최대 3명·탐사 범위·권위 있는 행동 validator를 유지한다. 직접 회귀 6개 그룹을 통과했다. `S/freeze_recovery.py`로 `build/strong_bot_recovery/ranker_rebuild.exe`, `S/recovery_native_source`, `S/recovery_runtime_contract.json`, `S/run_recovery_phase.py`를 별도 고정했다. 최초 frozen 실행파일·자료 수집은 보존했다.

`S/recovery_stalls`는 **학습 전 `initial_v9.bin`**, argmax, coordinated transfers=0으로 기존 시간 초과 job 23·25를 같은 시작 조건·슬롯에서 다시 실행했다. 두 경기 모두 승리했고 마스크 위반·명령 거부는 0이었다. [읽기 전용 검증 스크립트](../debug_artifacts/commander/strong_bot_20260908/failure_review/verify_recovery_stalls.py)와 `S/failure_review/recovery_stalls_comparison.json`이 기존 542열·9채널 접두, 실제 저장된 채집 패킷과 수입 기록을 연결한다.

| job | 최초 새 채집 명령 | 처음 달라진 기존 관측 | 수입 증거 | 결과 |
| --- | ---: | ---: | --- | --- |
| 23 | 18,097 frame, 20개 | 18,105 | 기존은 18,097부터 끝까지 수입 0. 수정본은 **18,625**에 수입 재개. | 기존 60,000 시간 초과 → **24,621 승리** |
| 25 | 17,937 frame, 18개 | 17,945 | 17,945의 짧은 수입 재개는 양쪽 동일. **18,561**에 수정본의 추가 수입이 처음 구분된다. 기존이 영구 정체된 21,945에도 수정본은 수입이 있다. | 기존 60,000 시간 초과 → **43,064 승리** |

처음 채집 패킷이 달라질 때까지 기존 관측·행동·마스크는 각각 938/955개 결정에서 동일했고 logp 차이도 0이었다. 정책 행동은 job23에서 18,145에, job25에서는 결정 시점까지 달라진 뒤 공통 frame 18,465에서 처음 구분된다. 따라서 최초 변화는 새 실행기의 채집 명령이며 뒤의 관측·정책 행동은 달라진 게임 상태를 반영한다. 수입은 약 220프레임 누적의 양자화된 관측이라 개별 일꾼의 정확한 입금 시점을 뜻하지 않는다. 후속 전체 48조건 검증에서도 새 승리는 이 두 경기이고 잃은 승리는 없었다. 이 결과를 신규 관측 학습의 효과로 설명하지 않는다.

전체 48경기의 범위도 `S/diagnostics/full_recovery_initial_behavior_scope_20260908.json`으로 확인했다. **31경기**는 기존 542열·9채널·행동·마스크·logp·value·결정 frame·종료 결과와 frame·owner1 명령의 의미 및 순서가 전부 같다. 나머지 **17경기**는 처음 다른 명령이 모두 채집이며 그 최초 목적지는 기존 반경 768 밖, 새 반경 1,536 안이다. 같은 frame의 후속 명령에는 근거리 재배치도 있어 모든 차이를 원거리 추가 명령으로 해석하지 않는다. 10경기는 명령 frame의 관측에서 수입 5 미만·홈 자원 0을 확인했고, 7경기는 직전 8–16 frame 관측만 있으므로 명령 순간의 조건을 RLO만으로 직접 확정하지 않았다. 자료와 스크립트는 입력 해시를 보존하며 추가 게임 없이 기존 기록만 비교했다.

마지막 학습 후보 `S/training/round1_residuals_only/epoch_003.bin` (SHA256 `759e6fc2d79e4ae41df13e18d9622fc5742551e5aa0500cf4fa72aac085b6a53`)은 3 epoch·81 optimizer step을 완료했다. 기존 actor와 새 관측 가중치 0을 유지하고 residual·critic만 학습했으며, 동일한 53,536개 관측에서 H0 logits·argmax가 정확히 유지됨을 검사했다. 이는 고정 입력에서의 보존 확인이고 이후 게임 상태나 H0 행동 궤적의 동일성을 뜻하지 않는다. 실제 선별 결과는 **9승·3패**로 현재 선정 조합의 같은 조건 **10승·2패**보다 낮았다. 남은 패배 20·38은 이겼지만 기존 승리 25·26·32를 잃었으므로 채택하지 않았다. 사전에 정한 선별 조건에 따라 전체 48경기 추가 평가로 진행하지 않았다. `S/residual_selection.json`에 이 판단을 기록했다.

모든 예약된 학습·경기 평가는 종료했다. 최종 조합은 **경제 회복 실행기 + initial_v9, argmax, coordinated transfers=0**이며 sampling도 39/48로 대조를 완료했다. `S/final_selection.json`은 실행파일·가중치·결과·행동 비교의 해시와 현재 소스 검증을 연결한다. Native 357개 파일은 선택된 빌드의 frozen 소스와 일치한다. Python 8개 중 model 모듈의 오래된 차원 설명만 606/12로 바로잡았고 해당 모듈은 docstring 제외 AST 동일성을 확인한다. 나머지 7개는 바이트 해시가 같다. 변경과 직접 관련된 Python 검사 82개와 native 관측·재편성·기존 Commander·경제 회복 검사를 통과했다. 독립 일반화 및 P2P 동기화 인증을 주장하지 않으며 기존의 승률·명령 거부 승격 기준은 낮추지 않았다.
