# 원시인 교사 개선 — 2026-09-10

정상 속도 내장 AI 상대 원시인 **규칙 기반 교사**를 개선했다. 현재 교사의 동일 SCREEN12 기준은 0승 7패 5시간 초과였고, 최종 후보는 12승으로 개선됐다. 고정 후보의 전체 개발 평가48은 **42승 6패 0시간 초과 (87.50%)**다. 신경망 모방학습/PPO 성적이 아니다.

| 상대 | 최종 승리 |
|---|---:|
| 원시인 | 12/12 |
| 엘프 | 12/12 |
| 티라노 | 6/12 |
| 데몬 | 12/12 |

최종 패배6경기는 모두 티라노전(job24,27,29,30,34,35)이다. 경제 운영과 초반 병력 확보 이후 중후반 교전 손실이 컸다. 다음 교사 개선에서는 이 패배의 조합·교전 운용을 우선 조사한다. 이번에는 고정 후보 검증 중 해당 시드에 맞춰 전략을 다시 바꾸지 않았다.

## 변경과 근거

- 원본 Jw2 TRC 데이터를 선택적으로 읽어 원시인 실제 비용·생산 건물·공격 프로필을 확인했다. Soldier는 사거리240·공격25, PowerMan은 근접50·공격30, Silvan은 사거리315·공격100이다. Silvan은 치료사가 아니다. BowMachine은 별도 PowerHouse에서 생산되는 사거리500·공격100 지상 범위 지원이다.
- 완료+대기 BuildMan14에서 채집 연구를 시작하고, 완료 후 일꾼16→20, 확장 후28을 운영한다. 인구 여유가 부족하면 House를 짓고 SoldierHouse를3개까지 늘린다. 14명은 이번 전투 전략에서 검증한 문턱이며 원시인 경제 최적점을 독립적으로 증명한 값이 아니다.
- 연구 완료는 선별 평가에서 기존4689프레임에서 대체로3385프레임으로 빨라졌다. 연구 ID0과 일꾼 타입0을 생산 대기열의 빈 값과 혼동하지 않도록 기존 상태 조건과 실제 명령을 점검했다.
- Soldier·PowerMan·Silvan은 같은 SoldierHouse의 생산 대기열 예산을 공유한다. 다른 유형마다 대기열을 별도로 누적하지 않는다.
- 원시인/엘프 상대는 기본 병력을 확보한 뒤 PowerHouse→Blacksmith→Silvan으로 전환한다. 티라노전에서는 Silvan 전환이 선별 평가의1승을 잃었으므로 기본 Soldier·PowerMan 조합을 유지한다.
- 데몬전은 기술 진입을 앞당기고 별도 생산 건물에서 BowMachine2~4기를 보탠다. 앞선 Silvan 후보의 데몬1/3승이 최종 후보에서3/3승으로 바뀌었다. 직접 관측한 손실에서도 Fighter 피해가 감소했다. BowMachine의 원본 범위/아군 피해 규칙은 변경하지 않았다.
- 지상 공격이 가능한 실제 병력20기, 주력 편입·집결·체력 조건을 만족하면 공격한다. 주력을 불필요하게 분산하지 않고, 홈 위협·국지 열세를 보며 공격을 유지한다. 확장에는 실제 Sanctuary 비용1200과 공개된 도달 가능·점유 정보를 사용한다.
- 게임 규칙, 관측 벡터1410/RLO6/스키마CRC2129581458 및 신경망 액션 인덱스는 변경하지 않았다. 게임 종료 시 트랩을 건물로 세지 않는 기존 규칙을 감사했다.

## 코호트 기록

| 코호트 | 후보 / 구간 | 승 | 패 | 시간 초과 |
|---|---|---:|---:|---:|
| 197 | baseline / screen12 | 0 | 7 | 5 |
| 198 | foundation_v1 / screen12 | 8 | 3 | 1 |
| 199 | silvan_v2 / screen12 | 9 | 3 | 0 |
| 200 | matchup_v3 / screen12 | 12 | 0 | 0 |
| 201 | matchup_v3 / primitive9 | 9 | 0 | 0 |
| 202 | matchup_v3 / elf9 | 9 | 0 | 0 |
| 203 | matchup_v3 / tyrano9 | 3 | 6 | 0 |
| 204 | matchup_v3 / demon9 | 9 | 0 | 0 |

각 코호트는 채팅에 보고한 뒤 ACK했다. SCREEN12는 후보 선택용으로 재사용했으며, 같은 실행 파일을 고정한 뒤 나머지36을 검증했다. 전체48은 개발 평가이고 완전히 새로운 지도/시드의 일반화 성능은 아니다. 현재 기준 교사를48경기 평가한 것처럼 과거 다른 실행 파일의 기록을 합치지 않았다.

## 데이터와 소스 검증

소유 종족0, 상대 실제 슬롯, seed/start_pair, curriculum2, 최대60000프레임, opponent slowdown0, 실행 파일·명령·로그·RLO SHA를 확인했다. RLO teacher weight_version0과 각 의사결정의8개 행동 헤드 합법 마스크를 검사했다. 최종48의 마스크 위반은0이며 실행 거절/건설 예약 만료 집계는3건이다. 원시인 상대 job8에서는 Sanctuary 건설 예약이10305프레임에 만료되고10369에 다시 발행돼11089에 건물이 생성됐다. 유효하지 않은 행동 선택과 구분했으며 해당 경기는23554프레임에 승리했다.

나머지는 job22의 House 예약(4201 만료→4209 생성), job36의 Blacksmith 예약(6921 만료→6945 생성)이다. 세 건 모두 후속 동일 종류 건물 생성과 해당 경기 승리를 확인했다. 실제 실행 이벤트 전부를 `matchup_v3/execution_events.json`에 기록했다.

유지 소스의 원시인 회귀 검사와 기존 엘프 회귀 검사를 포함한 CTest6개를 통과했다. 행동 비교1400개에서 유지 소스와 실제 평가 후보가 같았고, 소유 종족 엘프/티라노/데몬은 이전 Elf40 실행 파일과 같았다. 기존 Elf40 교사 등록과 네 종족 신경망 가중치 SHA를 보존했다.

원시인 소스: `ranker_reconstructed_code/src/ranker_ai_commander_primitive_foundation.inc` 및 dispatcher. 테스트: `ranker_reconstructed_code/tests/ai_commander_primitive_foundation_regression.cpp`.

실제 평가 실행 파일: `build/primitive_matchup_v3_20260910/ranker_rebuild.exe`

SHA256: `b51345306995a31b1a8fdf592a4f3319ac895d6a9998492926aa6b2d552fd894`

최종 등록: `debug_artifacts/commander/race_strength_20260909/training_run/primitive/best_teacher.json`. 해당 실행 파일에서 원시인 교사와 공개 상대 종족을 지정한다. 기존 신경망 v20은 이 교사 성능에 도달했다는 뜻이 아니다. 다음 학습은 검증된 교사 승리 자료와 실패 상태 교정 자료를 별도로 구성한 후 진행해야 한다.

최신 경기 재생: 루트 `play_last_replay.cmd`. 검증된 일반 교사 경기를 선택하도록 viewer 도구를 확장했고 관련 Python6개 테스트를 통과했다. `.cmd --dry-run`은 실제 최신 원시인 경기와 `rule_teacher`를 표시한다. 녹화된 원시인 측 명령을 재생하고 내장 상대 AI는 정상적으로 시뮬레이션한다. 원본/배포 실행 파일과 고정 리플레이 뷰어는 교체하지 않았다.

증거는 `debug_artifacts/commander/race_strength_20260909/training_run/primitive/improvement_20260910/`의 각 `*_audit.json`, `original_data.json`, `matchup_v3/full48_validation.json`, `latest_replay.json` 및 `build/primitive_source_verify_20260910/verification.json`에 보존했다.
