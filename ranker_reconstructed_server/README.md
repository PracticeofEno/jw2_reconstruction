# Ranker reconstructed WizardNet server

이 폴더는 원본 `ranker.exe`의 위자드넷 제어 서버 역할을 재구성한 서버입니다.
게임 시뮬레이션 패킷을 중앙에서 중계하지 않습니다. 원본 구조처럼 서버는 로그인,
로비 채널, 사용자/채팅, 게임 광고와 호스트 주소 전달을 담당하고 실제 게임은 호스트와
참가자 사이의 P2P 연결로 진행됩니다.

## 실행

Python 3.11 이상만 필요하며 외부 패키지는 사용하지 않습니다.

```powershell
cd C:\Users\note2\Downloads\jw_resversing\ranker_reconstructed_server
.\run_server.ps1
```

기본 수신 주소는 `0.0.0.0:19777`입니다. 다른 포트를 쓰려면 `config.json`을
수정하거나 다음과 같이 실행합니다.

```powershell
.\run_server.ps1 -ListenAddress 127.0.0.1 -Port 20000
```

클라이언트는 기본적으로 `127.0.0.1:19777`을 사용합니다. 원격 서버를 사용할 때는
`ranker_rebuild.exe`를 실행하기 전에 환경 변수를 지정합니다.

```powershell
$env:RANKER_RECONSTRUCTED_SERVER_ADDRESS = "server.example.net"
$env:RANKER_RECONSTRUCTED_SERVER_PORT = "19777"
.\ranker_rebuild.exe
```

로그인 화면의 `계정 만들기`에서 만든 계정은 `data/accounts.json`에 저장됩니다.
비밀번호 원문은 저장하지 않고 PBKDF2-SHA256 해시와 salt만 저장합니다. 기존처럼 로그인
화면에서 처음 보는 ID를 자동 등록하는 동작도 허용되며, 이를 끄려면
`server.auto_register_accounts`를 `false`로 바꿀 수 있습니다.

## 현재 구현 범위

- 레거시 13바이트 헤더, 체크섬, TCP 스트림 분할/병합 처리
- 로그인과 중복 접속/비밀번호 상태 코드
- 계정 생성 화면의 안내 요청, 계정 생성, 프로필 저장
- 기본 및 사용자 생성 로비 채널, 채널 목록/이동
- 로비 사용자 페이지와 입장/퇴장 알림
- 로비 채팅 전달
- 사용자 검색과 최소 프로필 응답
- 친구/길드 UI의 기본 응답
- 게임 생성 광고, 중복 이름 검사, 게임 목록과 삭제 알림
- 원본 `sockaddr_in` 및 0x2dc 맵 설명자를 이용한 호스트 P2P 주소 전달

계정은 영속 저장되지만 사용자 생성 채널은 아직 서버 재시작 후 초기화됩니다. 인터넷을
통한 실제 P2P 접속에는 호스트의
게임 TCP/UDP 포트가 방화벽과 공유기에서 열려 있어야 합니다. 중앙 서버가 NAT를 우회해
주지는 않습니다.

## 테스트

```powershell
python -m unittest discover -s tests -v
```

테스트는 체크섬, 분할 TCP 프레임, 계정 생성/재로그인, 사용자 목록, 로비 채팅, 게임
광고와 호스트 주소 전달을 실제 로컬 소켓으로 검증합니다.
