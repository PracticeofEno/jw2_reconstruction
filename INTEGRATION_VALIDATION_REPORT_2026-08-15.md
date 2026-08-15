# origin/main 통합 및 게임플레이 수정·검증 보고서

- 작성일: 2026-08-15 (Asia/Seoul)
- 작업 브랜치: `integration/origin-main-20260815`
- 이번 수정 전 통합 브랜치 HEAD: `107966e`
- origin/main 통합 커밋: `5304790`
- 통합한 origin/main: `85eff10`
- 원본 실행 파일: `RankerOCPV_Win/ranker.exe`
- 재구축 실행 파일: `RankerOCPV_Win/ranker_rebuild.exe`
- 원본 SHA-256: `0C404D0501400ABCA680D5618AE02BA72B11EAEED1EC125CC4F7CC2DAD18DB5A`
- 최종 재구축판 SHA-256: `C5102F04E4EE1D306225C82914AB352A3EC4EC001CBB9450FE3984BAD27B4333`

## 결론

최신 `origin/main`은 전용 통합 브랜치에 병합되어 있으며 `origin/main`의 현재 커밋은 이 브랜치의 조상이다. 요청받은 다섯 가지 동작을 원본 `ranker.exe`와 비교해 수정했다.

1. 티라노 합체 버튼은 이제 결과 유닛을 명령 패킷에 싣고 실제 합체 명령을 수행한다.
2. 수송선 승객 아이콘 클릭 하차는 승객과 수송선을 원본과 같은 필드에 기록하고 같은 해제 명령을 실행한다.
3. 선택 상태에서 빈 땅을 좌클릭해도 선택·부대지정·타깃 상태가 임의로 초기화되지 않는다.
4. P2P 경기가 정상 종료되거나 항복으로 끝나면 응용 프로그램이 종료되지 않고 원본처럼 프런트엔드 루프로 돌아간다.
5. 자이언트의 공격 이펙트가 전역 자원 엔트리 0이라는 이유로 누락되던 문제를 수정했다. 모든 유닛 공격 경로의 이펙트 자원을 전수 감사했으며 누락·손상·범위 초과가 없다.

Release 빌드와 배포 파일의 SHA-256은 일치한다. 원본 `ranker.exe`는 수정하지 않았고 해시도 작업 전 기준과 동일하다.

## 1. 티라노 합체 명령

원본 선택 패널은 합체 조건이 맞으면 명령 `0xB5`와 결과 유닛 종류를 함께 만든다.

| 선택 조건 | 결과 유닛 |
|---|---|
| Velocis(34) 2기 | TwinVelocis(35) |
| Rhamphos(37) 2기 | TwinRhamphos(38) |
| Pteras(39) 2기 | TwinPteras(45) |
| Dilophos(36) + Pteras(39) + Trices(40) | Mutant(43) |

원본 `0x004DA8B9`는 클릭한 항목의 보조 값에서 요청 결과 유닛 종류를 읽고, `0x004DA8C4`에서 합체 선택기와 비교한다. 재구축판은 아이콘은 그렸지만 일반 클릭 경로가 이 보조 값을 입력 스냅샷에 복사하지 않아 명령 실행 시 결과 종류가 사라졌다.

수정 후 `0xB5` 클릭은 결과 유닛 종류를 패킷 필드 3에 보존한 다음 원본 선택기 `0x0B` 경로로 전달한다. 합체 뒤 연결 유닛 해제 명령도 원본처럼 명령 `0x24`, 대상/수송선 포인터, 수송선 잠금 필드를 사용한다.

기존 원본 대비 검증 자료에서는 네 합체 결과의 생성, 이동, 공격 대상 등급 0~4 판정이 모두 정확히 일치했다.

- 합체 생성: 4/4 exact, 325 정렬 프레임
- 이동: 4/4 exact
- 공격 대상 등급 판정: 20/20 exact
- 기존 합체 상태 산출물: `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/linked_release_parity_20260815T105705Z`

## 2. 수송선 승객 아이콘 하차

원본의 승객 패널 구성은 `0x004E4AB5..0x004E4AEC`, 클릭 처리는 `0x004DB5E1`에서 확인했다. 승객 아이콘을 클릭하면 승객의 원본 상태 `0x45`, 승객이 가리키는 수송선, 수송선의 잠금 필드를 사용해 명령 `0x24`를 만든다.

재구축판은 이 경로에서 연결 포인터와 명령 대상의 의미가 섞여 있었다. 이를 승객의 명령 대상/수송선과 수송선 잠금 필드로 분리하고 원본 패킷 배치를 그대로 사용하도록 수정했다.

회귀 검사는 다음을 확인한다.

- 클릭한 승객이 명령 주체이다.
- 패킷 명령은 `0x24`이다.
- 패킷 대상은 해당 승객의 수송선이다.
- 수송선 잠금 상태가 원본과 같은 시점에 기록된다.

기존 수송 패리티 자료에서는 플레이 가능한 아이콘 하차 212/212와 실제 승차-하차 순환 192/192가 원본과 exact였다. 중첩 수송 7건은 원본도 명시적으로 거부하므로 `not_player_reachable`이다.

## 3. 빈 땅 좌클릭 선택 보존

원본 `0x004EB063`의 선택 판정은 좌클릭 위치에 유닛이나 건물이 없으면 즉시 반환한다. 선택 초기화는 실제 선택 대상이 검출된 뒤에만 수행된다.

재구축판의 무대상 좌클릭 경로에 있던 선택 초기화를 제거했다. 따라서 빈 땅 좌클릭은 현재 선택, 부대지정 및 타깃 지정 상태를 보존한다. 실제 대상 클릭과 수정키가 있는 선택 동작은 기존 경로를 유지한다.

## 4. P2P 한 경기 뒤 프로그램 종료

원본 `0x004D94C7..0x004D9561`은 네트워크 경기가 끝나면 바깥 프런트엔드 루프로 복귀한다. 정상 경기 종료나 항복은 응용 프로그램 종료 조건이 아니다.

재구축판은 P2P 정상 종료/항복 뒤 `WM_CLOSE`와 작업자 종료를 요청하고 있었다. 이를 제거하고 명시적인 전체 루프 종료 요청만 응용 프로그램을 닫도록 바꿨다. 회귀 테스트는 정상 종료 및 항복은 `false`, 명시적 루프 종료만 `true`가 되는 정책을 검사한다.

실제 두 피어가 참여하는 로비 복귀는 환경상 자동화하지 않았지만, 종료를 일으키던 분기와 메시지는 원본 제어 흐름으로 수정했고 단위 회귀 검사를 추가했다.

## 5. 자이언트 및 모든 유닛 공격 이펙트

### 자이언트 원인과 수정

자이언트 유닛 타입 2는 공격 프로필 2 `Throw bomb`을 사용한다. `JW2_12.TRC`의 프로필 0과 1에는 이미지가 없으므로 프로필 2의 첫 활성 이미지는 전역 리소스 저장소 엔트리 `0`이 된다.

원본 `FUN_004ED940`은 레코드의 자원 기준값에 원시 프레임 인덱스를 더하며 엔트리 0도 정상 자원으로 그린다. 재구축판은 `sprite_entry == 0`을 이미지 없음으로 해석해 자이언트 이펙트 전체를 렌더 전에 거부했다.

수정 내용은 다음과 같다.

- 이미지 없음 표지를 `0xFFFFFFFF`(`kInvalidResourceEntry`)로 통일했다.
- 전역 자원 엔트리 0을 유효한 스프라이트로 허용했다.
- 각 `JW2_12` 레코드가 이미지 할당 전의 전역 자원 기준 엔트리를 보존한다.
- 원본처럼 `resource_base + raw_index`로 프레임을 해석한다.
- 프로필 4 `Shoot sword`의 충돌 프레임 40~50이 다음 레코드의 이미지 0~10을 참조하는 원본식 교차 레코드 동작도 복원했다.

### 전체 공격 이펙트 전수 감사

`audit_attack_effect_resources.py`로 `JW2_09.TRC`의 170개 유닛 정의와 `JW2_12.TRC`의 원본 공격 이펙트 카탈로그를 직접 분석했다.

| 감사 항목 | 결과 |
|---|---:|
| 전체 유닛 정의 | 170 |
| 공격 원본 타입 | 87 |
| 유닛/공격 프로필 연결 | 166 |
| 실제 사용 공격 프로필 | 52 |
| 별도 시각 자원이 있는 프로필 | 42 |
| 별도 시각 자원이 없는 직접 공격 프로필 | 10 |
| 전체 공격 카탈로그 이미지 | 1,026 |
| 참조된 단계 프레임 | 928 |
| 참조된 고유 이미지 | 905 |
| 원본식 교차 레코드 참조 | 11 |
| RLE 해독 오류/범위 초과/완전 누락 | 0 |

모든 시각 프로필은 하나 이상의 실제 불투명 프레임을 가진다. 프로필 10 `Redbolt.wpn`의 시작 단계 한 프레임은 원본 자원 자체가 완전히 투명한 전환 프레임이며, 같은 프로필의 나머지 프레임은 정상적으로 보인다. 이를 누락으로 오판하지 않고 정보 항목으로 분리했다.

감사 최종 결과는 `ATTACK_EFFECT_RESOURCE_AUDIT_PASS`이며, 자이언트 활성 프레임의 전역 엔트리 0 판정도 별도 `PASS`이다.

실행 명령:

```powershell
python ranker_reconstructed_code\tools\gameplay_parity\audit_attack_effect_resources.py
```

## 검증 결과

### 자동 회귀 테스트

- Release `ranker_rebuild` 빌드: 통과
- CTest: 18/18 통과
- `unit_effect_intrusive_iteration_regression`: 통과
  - 자원 엔트리 0 렌더 해석
  - 교차 카탈로그 원시 이미지 인덱스 해석
- `mixed_unit_skill_original_parity_regression`: 32 selectors / 64 cases 통과
  - 승객 아이콘 하차 패킷 포함
- `gameplay_selection_modifier_regression`: 통과
  - 무대상 좌클릭 선택 보존
  - 합체 보조 값 전달
  - P2P 종료 정책
- `render_144fps_regression`: 통과
- Python 공격 이펙트 전수 감사: 통과
- `git diff --check`: 통과

### error1.ply 동기화 확인

현재 최종 재구축판 SHA-256 `C5102F04...B4333`을 배포하고 런타임 레이아웃을 다시 생성한 뒤 원본과 정확 정지 비교를 수행했다.

| 체크포인트 | 결과 | 최초 차이 |
|---:|---|---|
| 9,000 (기존 녹셔스 문제 구간) | expanded state exact | 없음 |
| 12,414 (리플레이 종료부) | expanded state exact | 없음 |

비교 대상에는 프레임, RNG, 체크섬 구성요소, 활성/수명주기 유닛의 슬롯·종류·위치·명령·타깃·체력·이동 상태, 유닛/맵 이펙트 상태가 포함된다.

- `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/error1_probe_9000_final_20260815`
- `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/error1_probe_12414_final_20260815`

연속 감사기를 1ms 간격으로 실행한 별도 시도는 두 드라이버가 감사기의 첫 정렬 표본보다 먼저 종료돼 비교 쌍 0개를 만들었으므로 검증 결과에 포함하지 않았다. 위 두 정확 정지 검사는 각각 유효한 확장 상태 비교를 완료했다.

## 주요 변경 파일

- `ranker_reconstructed_code/include/ranker_gameplay_production_actions.h`
- `ranker_reconstructed_code/include/ranker_gameplay_session_flow.h`
- `ranker_reconstructed_code/include/ranker_runtime_resources.h`
- `ranker_reconstructed_code/include/ranker_ui_overlay.h`
- `ranker_reconstructed_code/include/ranker_unit_action.h`
- `ranker_reconstructed_code/src/ranker_gameplay_production_actions.cpp`
- `ranker_reconstructed_code/src/ranker_runtime_resources.cpp`
- `ranker_reconstructed_code/src/ranker_ui_overlay.cpp`
- `ranker_reconstructed_code/src/ranker_unit_action.cpp`
- `ranker_reconstructed_code/src/ranker_winmain.cpp`
- `ranker_reconstructed_code/tests/gameplay_selection_modifier_regression.cpp`
- `ranker_reconstructed_code/tests/mixed_unit_skill_original_parity_regression.cpp`
- `ranker_reconstructed_code/tests/unit_effect_intrusive_iteration_regression.cpp`
- `ranker_reconstructed_code/tools/gameplay_parity/audit_attack_effect_resources.py`

## 배포 상태

- 빌드 산출물: `build_note2_release/ranker_rebuild.exe`
- 배포 대상: `RankerOCPV_Win/ranker_rebuild.exe`
- 두 파일 SHA-256: `C5102F04E4EE1D306225C82914AB352A3EC4EC001CBB9450FE3984BAD27B4333`
- 원본 `RankerOCPV_Win/ranker.exe` SHA-256: `0C404D0501400ABCA680D5618AE02BA72B11EAEED1EC125CC4F7CC2DAD18DB5A`
- 원본 변경 여부: 변경 없음
- 대체 배포 파일명 생성 여부: 없음

이번 작업에서는 새 진단 PNG를 만들지 않았다.
