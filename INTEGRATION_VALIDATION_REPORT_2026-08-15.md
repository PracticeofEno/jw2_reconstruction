# origin/main 통합 및 티라노 합체 기능 중간보고

- 작성일: 2026-08-15 (Asia/Seoul)
- 작업 브랜치: `integration/origin-main-20260815`
- 통합 커밋: `5304790` (`origin/main`의 `85eff10` 병합)
- 원본 실행 파일: `RankerOCPV_Win/ranker.exe`
- 재구축 실행 파일: `RankerOCPV_Win/ranker_rebuild.exe`
- 원본 SHA-256: `0C404D0501400ABCA680D5618AE02BA72B11EAEED1EC125CC4F7CC2DAD18DB5A`
- 현재 재구축판 SHA-256: `6F32BB3D747E0C0D34B1B95B7D9A6A099A3A0028AD60EF4A60334D63FB478027`

## 현재 결론

최신 `origin/main`은 별도 통합 브랜치에 병합됐고 빌드 및 기본 통합 테스트를 통과했다. 티라노 합체 버튼이 보이지 않던 원인도 원본 역어셈블리와 리소스 구조 비교로 특정해 수정했다.

합체 생성 4건, 합체 결과 유닛의 이동 4건, 공격 대상 등급 판정 20건은 모두 현재 배포된 `ranker_rebuild.exe`에서 원본과 정확히 일치한다. UI 수정이 반영된 최종 해시로 `error1.ply` 전체 구간도 다시 실행했으며 불일치 없이 통과했다.

## 합체 버튼 결함과 수정

원본의 선택 패널은 다음 조건에서 합체 명령 `0xB5`를 만든다.

| 선택 조건 | 결과 유닛 |
|---|---|
| Velocis(34) 2기 | TwinVelocis(35) |
| Rhamphos(37) 2기 | TwinRhamphos(38) |
| Pteras(39) 2기 | TwinPteras(45) |
| Dilophos(36) + Pteras(39) + Trices(40) | Mutant(43) |

명령 생성 조건은 재구축판에도 존재했고 원본과 같았다. 실제 결함은 아이콘 렌더 경로였다.

- 원본 `ranker.exe`의 `0x004E1134..0x004E1145`는 `0xB5` 합체 명령의 보조 값, 즉 결과 유닛 종류를 `char_small` 유닛 아이콘 테이블로 그린다.
- 일반 행동 버튼은 `action.trt`를 쓰며 프레임 수가 42개다.
- 재구축판은 합체 버튼까지 잘못 `action.trt`로 그렸다. 이 때문에 결과 ID 43과 45는 테이블 범위를 벗어나 사라지고, 35와 38도 잘못된 행동 프레임을 사용했다.
- `0xB5`만 원본과 같이 `char_small` 경로와 결과 유닛 ID 프레임을 사용하도록 수정했다. 일반 행동 버튼 경로는 유지했다.

수정 및 회귀 검사는 다음 파일에 있다.

- `ranker_reconstructed_code/include/ranker_ui_overlay.h`
- `ranker_reconstructed_code/src/ranker_ui_overlay.cpp`
- `ranker_reconstructed_code/tests/gameplay_selection_modifier_regression.cpp`

## 원본 대비 티라노 검증 결과

### 합체 생성과 상태 전이

네 합체 케이스 모두 통과했다. 원본과 재구축판 사이에서 비교한 325개 프레임의 RNG, 유닛 종류/슬롯/위치/명령/체력/이동 상태, 효과 상태와 체크섬이 정확히 일치했다.

- 산출물: `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/linked_release_parity_20260815T105705Z`

### 조작 및 이동

| 결과 유닛 | 이동/순찰 | 결과 |
|---|---:|---|
| TwinVelocis(35) | 배치 4 | 정확 일치 |
| TwinRhamphos(38) | 배치 4 | 정확 일치 |
| Mutant(43) | 배치 5 | 정확 일치 |
| TwinPteras(45) | 배치 5 | 정확 일치 |

배치 4와 배치 5에서 각각 115개 비교 프레임이 모두 일치했다.

- 배치 4: `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/move_patrol_parity_20260815T105944Z`
- 배치 5: `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/move_patrol_parity_20260815T110023Z`

### 공격 및 대상 판정

대상 렌더 등급 0~4 전체를 비교했다. 아래에서 `공격`은 원본과 재구축판 양쪽에서 실제 효과/피해가 발생했음을, `거부`는 양쪽에서 동일하게 공격 불가 판정을 했음을 뜻한다.

| 결과 유닛 | 등급 0 | 등급 1 | 등급 2 | 등급 3 | 등급 4 |
|---|---|---|---|---|---|
| TwinVelocis(35) | 공격 일치 | 공격 일치 | 공격 일치 | 거부 일치 | 거부 일치 |
| TwinRhamphos(38) | 거부 일치 | 거부 일치 | 거부 일치 | 공격 일치 | 거부 일치 |
| Mutant(43) | 공격 일치 | 공격 일치 | 공격 일치 | 공격 일치 | 공격 일치 |
| TwinPteras(45) | 공격 일치 | 공격 일치 | 공격 일치 | 거부 일치 | 거부 일치 |

20건 모두 결과가 `exact`이며, 모두 현재 재구축판 SHA-256과 연결돼 있다. 허용된 공격은 공격 효과의 발사자/대상 연결 또는 대상 체력 감소를 확인했고, 거부 대상은 원본과 재구축판 모두 효과와 피해가 없음을 확인했다.

일부 다중 유닛 배치는 요청 대상이 아닌 동반 유닛의 공격 미발동 때문에 배치 전체 종료 코드가 실패였지만, 요청 대상 유닛 레코드는 정확 일치했다. TwinVelocis 등급 1은 배치 간섭을 제거한 전용 1대1 케이스로 재검증해 실제 공격과 155프레임 정확 일치를 확인했다.

## 병합 후 통합 검증 현황

- 재구축 빌드: 통과
- CTest: 19/19 통과
- 서버 Python 단위 테스트: 107/107 통과
- Python `compileall`: 통과
- 레이아웃 프로브 생성 및 해석: 통과
- 티라노 합체 생성: 4/4 통과
- 티라노 결과 유닛 이동: 4/4 통과
- 티라노 결과 유닛 공격 대상 판정: 20/20 통과
- 병합 직후 `error1.ply` 전체 리플레이: 10,250개 원본/재구축 비교 쌍에서 불일치 없음
- 최종 UI 수정 해시의 `error1.ply` 전체 리플레이: 최종 프레임 12,414까지 8,011개 관측 상태 쌍에서 불일치 없음
- 최종 `error1.ply` 산출물: `ranker_reconstructed_code/tools/replay_debug/artifacts/runs/error1_integration_final_20260815T112100Z`
- 최종 Release 재빌드 확인: 최신 상태, 추가 빌드 작업 없음
- 최종 CTest 재검증: 19/19 통과
- 빌드 산출물과 배포 `ranker_rebuild.exe` SHA-256 일치
- 원본 `ranker.exe` SHA-256 불변

## 완료 상태

최종 소스 diff, 배포 파일명과 해시, 원본 실행 파일 불변 여부까지 재확인했다. 합체 버튼 수정과 검증 도구 보강 및 이 보고서를 통합 브랜치의 후속 커밋으로 정리한다.
