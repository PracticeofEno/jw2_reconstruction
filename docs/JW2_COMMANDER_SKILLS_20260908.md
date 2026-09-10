# 종족·유닛 고유 스킬의 Commander 학습 연결

2026-09-08. 기존 종족별 학습에 능동 스킬 선택을 추가했다. 원본 TRC의 초기 유닛·스킬 연결 29개를 모두 행동 후보로 제공하며, 원본의 자원·유닛 레벨·연구·대상 조건으로 마스킹한다. 효과와 시뮬레이션은 기존 게임 엔진이 처리하고, AI는 정렬된 게임플레이 패킷을 통해 주문한다.

스킬 사용 가능 여부와 숙련도는 별개다. 새 모델은 스킬을 선택할 수 있는 초기 전이 모델이며, 모든 스킬을 잘 활용한다거나 원본과의 전체 효과 동등성이 새로 입증되었다는 뜻은 아니다.

## 연결한 스킬

| 종족 | 유닛 | 초기 능동 스킬 |
|---|---|---|
| 원시인 | Chief | Fall out, Sky Fallout |
| 원시인 | Kalma | Hide |
| 엘프 | RedElf | Thunder bolt |
| 엘프 | WhiteElf | Shield, Mass temper |
| 엘프 | Ranger | Teleport, Jump portal |
| 엘프 | BlueElf | Drop Stone |
| 엘프 | Unicorn | Blessing |
| 엘프 | Pixie | Fake |
| 엘프 | ManaSpread | Meteo |
| 엘프 | AngelElf | Resurrect |
| 엘프 | GreenElf | Entangle, Hurdle |
| 엘프 | DarkElf | Hide, Interlace, Exchange |
| 데몬 | DeathEye | Corrupt |
| 데몬 | Phantom | Shadow Force |
| 데몬 | Kelpa | Stone curse |
| 데몬 | Warlock | Noxious gas, Quake |
| 데몬 | Devil | Rise death |
| 데몬 | FemmeFatale | Rebirth, Recharge, Absorb |
| 데몬 | Nightmare | Airquake |
| 데몬 | BoneFighter | Suicide |

초기 연결은 원시인 3개, 엘프 15개, 데몬 11개다. 티라노는 초기 주문 비트가 없으며 기존 합체와 함께 변신·변신 해제, Haste 등 지원되는 모드 전환을 선택한다. Rage·HolyMind·Haste·BloodGuard의 켜기/끄기, 은신 해제, 수송 탑승/하차, 자원 공유도 같은 후보 메뉴에 연결했다. 실제 소유 유닛의 변경 가능한 스킬 비트를 읽으므로 연구로 해제되는 Bonefighter 주문과 장비에서 부여된 주문도 해당 조건을 충족하면 후보에 들어간다. 패시브·자동 효과는 엔진의 기존 처리를 유지한다.

`tools/ai/ranker_commander_skills_catalog.py`가 `Jw2_09.trc`, `Jw2_11.trc`에서 `commander_skills.json`과 `include/ranker_ai_skill_catalog.h`를 생성한다. 원본 아카이브의 SHA-256을 JSON에 기록하며 `--check`로 재현성을 검증한다. 원본 실행 파일을 수정하지 않는다.

## 관측과 실행

- 기존 64개 매크로의 번호를 유지하고 64~95에 동적 스킬 후보 32개를 추가했다. 각 후보는 시전자와 대상 또는 지점을 포함한다. 정책이 후보를 선택하면 해당 명령을 발행한다.
- 스킬별 첫 후보를 먼저 배치하고 남은 칸에 대안을 넣는다. 후보는 각 관계 집합의 가까운 대상과 전술 지점에서 만든다. 가능한 모든 유닛·좌표 조합을 직접 출력하는 구조는 아니다. 후보보다 스킬 종류가 많을 경우 종류의 시작 순서를 회전시킨다.
- 후보당 24개 관측값: 존재, 사용 가능, 행동 종류, 주문 번호, 시전자 종류·체력·마나, 체력/마나 비용, 현재/요구 레벨, 사거리, 시전자 위치, 대상 관계·종류·체력·위치, 거리, 시전자 상태 플래그, 명령 진입 잠금, 연구 충족, 모드 켜기 또는 장비 주문 여부.
- 현재 관측한 유닛만 대상으로 사용한다. 부활 계열은 현재 시야와 엔진의 대상 가시성 조건을 모두 통과한 사체만 별도로 제공한다. 과거에 본 적의 기억은 주문 대상 목록에 포함하지 않는다.
- 원본 선택자의 별도 조건도 검사한다. 예를 들어 Rebirth는 해당 종류의 사체, Absorb는 우호 관계의 해당 유닛이 필요하다. 소유하지 않은 스킬이나 부족한 자원·레벨·연구는 거부한다.
- 시전/접근/채널 상태와 패킷이 시뮬레이션에 들어가기까지의 짧은 구간을 보호해 일반 이동·공격 제어가 주문을 덮어쓰지 않게 했다. 변신 후의 특수 능력 플래그로 인해 변신 해제가 영구 차단되지 않도록 처리했다.
- `commander_metrics_1.json`, `commander_metrics_2.json`의 `ability_orders`, `ability_effect_success`, `ability_effect_failed`는 선택자별 배열이다. 주문 발행과 실제 효과 처리 결과를 구분한다. 효과 결과에는 엔진이 자동으로 실행한 보조 선택자도 포함될 수 있다. 대상 소멸이나 접근 중 사망으로 효과 실행 지점에 도달하지 못한 주문까지 이 카운터가 모두 분류하지는 않는다.

## 가중치와 기록 형식

새 계약은 관측 1410개, 지도 12×16×16, 8개 행동 헤드 `(96,16,4,8,16,3,3,3)`, 총 로짓 149개다. 스키마는 `0x7EEED592`, 가중치 형식은 2, RLO 형식은 6, 레코드는 6061바이트, 마스크는 19바이트, 교사 라벨은 `JWTL0003`이다.

이전 528/542/606/642 모델은 명시적 마이그레이션으로만 읽는다. 학습된 입력 열·행동 행·임베딩·Adam 모멘트를 보존하고 추가 입력 열과 모멘트는 0으로 초기화한다. 새 행동 바이어스는 -2로 시작한다. 새 행동을 마스킹하면 기존 정책 출력을 부동소수점 오차 범위에서 유지한다. 이전 롤아웃을 0으로 패딩해 새 학습 데이터로 사용하지 않는다.

기존 `multirace_20260908/training_run`은 체크포인트 저장 후 중단했다. 이관 시점의 완료 회차는 엘프 3, 데몬 3, 원시인 2, 티라노 2다. 종족별 버전·회차·경기 수를 유지하고, 스킬 확장 전 회차를 `pre_skill_updates`에 기록했다. 자기대전 준비도와 과거 평가 승인 상태는 이관하지 않는다.

학습용 실행 파일은 `build/commander_skills/ranker_rebuild.exe`다. 배포 폴더의 `ranker.exe`, `ranker_rebuild.exe`와 기존 선택 티라노 가중치는 변경하지 않았다.

## 검증과 학습 운영

- C++ 회귀 검증: 원본 29개 연결의 후보 생성·정책 선택·패킷 발행, 은신 해제 패킷, 사체 가시성, 자원/레벨/연구, 시전 보호, 모드 전환과 변신 해제.
- 네 종족 대 기본 AI 4경기와 종족 간 자기대전 4경기/8개 소유자 기록을 확인했다. 각 소유자의 실제 종족 원핫을 검사했다. 금지 행동 선택과 조용한 명령 거절은 없었다. 기본 AI 상대전의 엘프 Teleport 주문/성공 4회, 자기대전의 Teleport 1회와 데몬 BoneFighter Suicide 1회 발동을 확인했다. 모든 29개 스킬의 실제 효과를 이 대전에서 각각 관측한 것은 아니다.
- 네 종족 각각의 실제 기록으로 PPO 갱신을 검증했고, 자기대전에서는 각 종족의 두 플레이어 자리 기록을 함께 검사했다. native/Python 로짓 최대 오차는 약 `5.96e-8`, 실제 기록의 합산 로그확률 최대 오차는 약 `5.73e-6`이었다. 검증용 갱신 파일은 학습 캠페인 체크포인트로 채택하지 않았다.
- 관련 Python 검사 89개와 Commander C++ 검사 9개를 사용했다. 과거 606 전용 research/league 도구는 이번 캠페인의 실행 경로가 아니다.

검증 산출물은 `debug_artifacts/commander/skills_20260908`의 `smoke_reports.json`, `ppo_probe.json`, `crossrace_verified_reports.json`, `crossrace_verified_ppo_probe.json`이다. 마지막 검증은 총 4859개 의사결정 기록으로 네 종족 각각의 두 플레이어 자리를 확인했다. 앞선 시험의 잘못된 상대 종족 인자 때문에 발생한 기록은 `crossrace_pilot_note.json`에 설명했으며 종족별 검증 근거에서 제외했다.

현재 실행 상태는 `debug_artifacts/commander/skills_20260908/training_run/state.json`에 저장한다. 기본 설정은 작업자 4개, 종족당 회차별 12경기, 학습률 `2.5e-5`, 3 epoch, 24회 순회다. 종료 시 체크포인트를 보존하며 아래 `train` 명령으로 이어갈 수 있다.

```powershell
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py status
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py stop
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py train
```

스킬 확장 이후 모든 종족이 최소 8회차를 학습하고, 균등한 상대 종족 평가에서 argmax 승률 60%, 샘플링 승률 50%, 상대 종족별 합산 승률 25% 이상을 충족하면 과거 모델 상대 80%·기본 AI 상대 20%의 자기대전으로 자동 전환한다. 이 기준은 스킬 숙련도 전수 인증이 아니라 대전 성능의 전환 기준이다. 반복 평가의 승률과 별개로 새 시드·지도에서 일반화 검증이 필요하다.

## 코호트별 요약 보고

`ranker_commander_cohort_report.py`는 학습 상태와 완료 로그를 읽는 별도 관찰자다. 학습기·가중치·실행 파일은 변경하지 않는다. 저장된 가중치 해시와 실제 채택 경기 기록을 확인한 뒤 종족, 누적 코호트 번호, 스킬 확장 후 갱신 수, 승·패·시간 제한 미결, 스킬/변신 명령 수, 금지 행동/명령 거절을 보고한다. 자기대전에서는 실제 학습 소유자의 결과와 계수를 사용한다.

```powershell
.venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_cohort_report.py --watch --notify-windows
```

학습 작업 폴더의 `reports/cohort_summary.md`에 한국어 요약, `reports/events.json`에 상세 수치를 누적한다. 5초마다 새 완료 기록을 확인하며, 기존 보고는 중복 생성하지 않는다. 별도 평가는 해당 가중치 버전의 평가가 끝났을 때 따로 보고한다. 초기 과거 기록 수집 시에는 알림을 몰아서 띄우지 않는다.

`--notify-windows`는 새 보고마다 Windows 알림을 표시한다. `reports/notifications.jsonl`은 알림 API 호출 결과와 표시 이벤트 수신 여부를 기록한다. Windows 알림 설정에 따라 화면 표시가 제한될 수 있으며, 알림 표시 여부와 관계없이 파일 보고는 보존한다. `reports/observer.json`에는 관찰자 상태와 현재 경기 완료 수를 기록한다. 채팅 자동 예약 기능은 없으며, 채팅 보고는 에이전트가 응답을 진행하는 동안 가능하다.

관찰자는 학습이 중지·오류·설정 순회 횟수 도달로 종료되면 종료 보고 후 끝난다. 학습을 다시 시작할 때 관찰자도 위 명령으로 다시 실행한다. `reports/STOP` 파일을 만들면 관찰자만 종료하며, 학습기의 `STOP`과는 별개다.
