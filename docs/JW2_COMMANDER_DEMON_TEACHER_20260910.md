# 데몬 교사 개선 — 2026-09-10

정상 속도 내장 AI 상대 데몬 규칙 기반 교사를 개선했다. 현재 교사의 동일 SCREEN12 기준은0승10패2시간 초과였고, 최종 후보는12승이었다. 고정 후보의 전체 개발 평가48은 **43승 5패 0시간 초과 (89.58%)**다. 신경망 모방학습이나 PPO 성적이 아니다.

| 상대 | 최종 승리 |
|---|---:|
| 원시인 | 12/12 |
| 엘프 | 10/12 |
| 티라노 | 11/12 |
| 데몬 | 10/12 |

비승리 경기 job: [16, 17, 25, 37, 42]. SCREEN12는 후보 선택에 사용했으며, 고정 실행 파일의 나머지36경기를 별도 검증했다. 전체48은 개발 평가이고 새 지도/시드 일반화 성적은 아니다. 현재 기준 교사를48경기 평가한 것처럼 과거 다른 실행 파일의 점수를 합치지 않았다.

남은 패배는 엘프2경기, 티라노1경기, 데몬2경기다. 초반 생산이 정상이어도 중후반 원거리 교전 손실이 누적되는 조건이 남아 있다. 패배 경기의 실제 관측 스냅샷과 사망 로그를 보존했으며 이번 검증 도중 해당 시드에 맞춰 후보를 다시 바꾸지 않았다.

## 구현과 근거

- 원본 TRC에서 Skeleton은 비용150·HP170·사거리205의 기본 원거리 유닛, Kelpa는 HP330·공격40·사거리160의 지상 지원 유닛, Nightmare는 HP320·공격75·사거리275의 원거리 유닛임을 확인했다. DeathEye·Phantom은 일반 공격력이0이므로 주력 화력에 포함하지 않는다. 원본 이름 Skeleton을 사용한다.
- 완료+대기 Insect를14명까지 제한하고, 완료14명에서 채집 연구31을 시작한다. 연구 접수 후 생산 예산을 풀어 일꾼16→20, 확장 후28명으로 운영한다. 14는 이번 전투 전략의 문턱이며 독립 경제 최적점을 증명한 값이 아니다.
- Tomb으로 인구를 보충하고 DeathDen3개·MagicDen1개를 갖춘다. Skeleton을 주력으로 생산하고 Kelpa를 보조한다. 기본 병력12기와 일꾼20명을 확보하면 EvilPortal을 준비해 Nightmare를 생산한다. Kelpa·Nightmare는 MagicDen 대기열 예산을 공유한다.
- 전투 판단에는 실제 지상 공격 유닛만 사용한다. 주력20기, 편입85%, 집결75%, 체력65% 등의 조건으로 공격에 진입하고, 본진 위기·국지 열세를 보고 공격을 유지한다. 확장은 실제 DemonDen 비용1200과 공개된 도달 가능·점유 정보를 따른다.
- Kelpa Stone curse와 Nightmare Airquake는 원본에서 체력을 소모한다. 액션 메뉴는 유지하며 이번 기본 교사는 이 스킬을 자동 사용하지 않는다. 원본 기술/피해/종료 규칙은 변경하지 않았다.

## 코호트

| 코호트 | 단계 | 승 | 패 | 시간 초과 |
|---|---|---:|---:|---:|
| 205 | update_demon_baseline_20260910 / screen12 | 0 | 10 | 2 |
| 206 | update_demon_foundation_v1_20260910 / screen12 | 12 | 0 | 0 |
| 207 | update_demon_foundation_v1_20260910 / primitive9 | 9 | 0 | 0 |
| 208 | update_demon_foundation_v1_20260910 / elf9 | 7 | 2 | 0 |
| 209 | update_demon_foundation_v1_20260910 / tyrano9 | 8 | 1 | 0 |
| 210 | update_demon_foundation_v1_20260910 / demon9 | 7 | 2 | 0 |

모든 코호트는 채팅 보고 후 ACK했다. Windows 알림은 사용하지 않았다.

## 검증과 보존

소유 종족3, 실제 상대 슬롯, seed/start_pair, curriculum2, 최대60000프레임, 상대 slowdown0, 실행 파일 SHA, 실제 명령·로그·RLO를 감사했다. teacher weight_version0과8개 행동 헤드의 합법 마스크를 검사했다. 마스크 위반0, 실행 실패/예약 만료 집계5건이며 전체 이벤트는 `execution_events.json`에 기록했다. 트랩을 종료 조건의 건물로 세지 않는 기존 규칙도 확인했다.

집계5건은 모두 DemonDen 또는 Tomb 건설 예약 만료다. 이후 같은 종류 건물 생성과 해당 경기 승리를 확인했다. 만료 카운터를0으로 바꾸거나 평가에서 해당 경기를 제외하지 않았다.

관련 CTest7개와 실제 평가 후보/유지 소스 행동 비교1400개를 통과했다. 소유 종족 원시인·엘프·티라노1050개 상황의 행동은 이전 실행 파일과 같았다. 기존 원시인42/48·엘프40/48 교사 등록과 네 종족 신경망 가중치 SHA를 보존했다. 관측 벡터1410/RLO6/CRC2129581458 및 신경망 액션 인덱스는 그대로다.

소스: `ranker_reconstructed_code/src/ranker_ai_commander_demon_foundation.inc`. 회귀 테스트: `ranker_reconstructed_code/tests/ai_commander_demon_foundation_regression.cpp`.

검증 실행 파일: `build/demon_foundation_v1_20260910/ranker_rebuild.exe`

SHA256: `745b522259b691d7f1c3048d3428e0a1960e001394790b06255d6e0010b275b1`

최종 등록: `debug_artifacts/commander/race_strength_20260909/training_run/demon/best_teacher.json`. 기존 신경망v25가 이 교사 성능에 도달했다는 의미는 아니다. 후속 모방학습은 검증된 교사 승리 자료와 실패 상태 교정 자료를 별도로 구성해야 한다.

루트 `play_last_replay.cmd`는 검증된 최신 데몬 교사 경기의 저장된 명령을 재생한다. `--dry-run`으로 실제 데몬 경기와 `rule_teacher` 선택을 확인했다. 배포 실행 파일과 고정 리플레이 뷰어는 교체하지 않았다.

증거: `debug_artifacts/commander/race_strength_20260909/training_run/demon/improvement_20260910/`의 `original_data.json`, 각 코호트 `*_audit.json`, 최종 `full48_validation.json`, `execution_events.json`, `latest_replay.json` 및 `build/demon_source_verify_20260910/verification.json`.
