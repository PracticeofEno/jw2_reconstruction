# 공격 이펙트·미니맵 공격·60Hz 중간 보고

작성일: 2026-08-16

## 현재 결론

- 미니맵에서 공격 지점을 지정할 때 이전 월드 화면의 유닛 타깃이 남는 차이를
  확인하고 수정했다. 원본 `0x004da08a..0x004da0b2`는 미니맵 경로에서 타깃
  레지스터를 0으로 만든 뒤 지점 명령을 전송한다.
- 렌더링 주사율의 코드 기본값과 배포 설정값을 모두 60 FPS로 되돌렸다. F11의
  60/144 FPS 전환 기능은 유지한다.
- 공격 이펙트는 170개 유닛 정의, 166개 공격 바인딩, 사용 중인 52개 공격
  프로필의 원본 자원 인덱스와 저레벨 렌더 분기를 대조했다. Giant가 사용하는
  전역 자원 엔트리 0과 프로필 4의 다음 레코드 참조를 포함해 현재 로더 및
  렌더 선택 경로가 원본과 일치한다.
- KingDemon Fire 프로필 `0x20`의 잔상만 원본과 다른 프레임 필드를 사용하고
  있었다. 원본 `0x004ec770..0x004ec778`은 수명 카운터 `+0x10`의 절반을
  스프라이트 프레임 `+0x0c`에 저장하고, `FUN_004ed940`은 항상 `+0x0c`를
  그린다. 재구성판도 잔상 렌더 프레임으로 `tick(+0x0c)`을 사용하도록 수정했다.

## 변경 파일

- `src/ranker_gameplay_input_actions.cpp`
  - 미니맵 공격 명령 전송 전에 `last_validation_unit_offset`을 0으로 초기화한다.
- `include/ranker_client_config.h`, `include/ranker_game_loop.h`
  - 렌더링 기본값을 144에서 60 FPS로 변경한다.
- `config/ranker_client.ini`, `RankerOCPV_Win/ranker_client.ini`
  - 소스 및 실제 배포 설정의 `RenderFPS`를 60으로 변경한다.
- `tools/replay_debug/build_layout_probe.ps1`
  - 현재 실행 파일과 같은 Media Foundation 라이브러리를 링크하도록 진단 빌드를
    보완한다.
- `src/ranker_unit_action.cpp`
  - KingDemon Fire 잔상이 원본의 반속 `tick(+0x0c)` 프레임을 선택하도록 수정한다.
- `tests/unit_effect_intrusive_iteration_regression.cpp`
  - 다른 회귀 항목을 실행하지 않고 잔상 프레임 선택만 검증할 수 있는
    `kingdemon_afterimage_render` 전용 실행 경로를 추가한다.

## 변경과 직접 관련된 검증

- `minimap_attack_target_regression`: 통과
  - 이전 유닛 타깃 `0x1234`가 남아 있어도 미니맵 공격은 타깃 0과 지정 좌표를
    전송함을 확인했다.
- `render_144fps_regression`: 통과
  - 기본 60 FPS, 16,666,666 ns 간격, F11 60→144→60 전환을 확인했다.
- `unit_effect_intrusive_iteration_regression.exe kingdemon_afterimage_render`: 통과
  - 수명 카운터 14에서 원본과 동일하게 반속 프레임 7과 그리기 모드 4를
    선택함을 확인했다. 이 실행에서는 다른 회귀 테스트를 수행하지 않았다.
- Release `ranker_rebuild` 빌드: 성공
  - 정식 배포 파일 `RankerOCPV_Win/ranker_rebuild.exe`에 반영했다.

## 일꾼 선택 변경 후 건설 메뉴 초기화

- 원본 `FUN_004eb063`은 새 개체를 정상 선택하는 경로의 `0x004eb124`에서
  건설 분류의 원본 상태값 `DAT_00864bb4`를 0으로 초기화한 뒤 선택 개체와
  명령 패널을 다시 구성한다.
- 재구성판은 `UiOverlayState::selected_production_category`만 0으로 만들었고,
  다음 프레임에 `GameplayInputActionState::pointer_aux_state`의 이전 값이 다시
  복사되어 일반/테크/생산 건물 하위 아이콘이 새 일꾼에도 남았다.
- 선택 변경 시 초기화 요청을 보존하고, 프레임 동기화 중에는 이전 분류가
  되살아나지 않게 했다. 런타임 변경 반영 단계에서는 원본과 같이 입력 미러와
  화면 분류를 모두 0으로 확정한다. 주 선택 유닛을 Shift로 해제하는 경로에도
  같은 원본 규칙을 적용했다.
- 관련 항목만 실행하는
  `gameplay_selection_modifier_regression.exe worker_build_category_selection_reset`
  집중 회귀 테스트가 통과했다. 선택 초기화, 중간 동기화, 입력 미러 반영,
  이후 정상 분류 동기화를 검증했으며 다른 회귀 항목은 실행하지 않았다.
- Release `ranker_rebuild` 대상의 통합 컴파일과 링크가 성공했다.

## 테스트 범위 메모

전체 공격 리플레이 묶음을 실행하려던 검증은 원본이 리플레이로 진입하지 않고
오프닝에 머물러 즉시 중단했으며 결과로 채택하지 않았다. 이후에는 `AGENTS.md`의
지침에 따라 실제 코드 변경과 직접 관련된 테스트만 실행한다.
