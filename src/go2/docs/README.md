# Go2 ESP 문서 인덱스

Go2 등에 장착되는 ESP32 피격/LED 보드용 문서입니다. 터렛 펌웨어처럼 Go2도 `src/go2/` 아래에 빌드 설정, 펌웨어 진입점, 기능 모듈, 문서를 모아둡니다.

- `build-upload-workflow.md`: 로컬 secrets 생성, robot id 선택, 빌드/업로드 흐름
- `mqtt-hit-contract.md`: Command Center와 주고받는 MQTT topic/payload 계약
- `fallback-behavior.md`: Command Center 연결이 없을 때 ESP 로컬 fallback 동작

주요 코드 위치:

```text
src/go2/
├─ main.cpp                         # setup/loop runtime orchestration
├─ build_config.h                   # 핀, HP, MQTT topic, build-time macro
├─ robots.json                      # Go2별 non-secret profile
├─ local_secrets.example.h          # gitignore local secret template
├─ sensors/                         # piezo sampling / hit candidate detection
├─ display/                         # ring_display command renderer + fallback LED
├─ mqtt/                            # MQTT hit candidate / heartbeat / display command
└─ fallback/                        # Command Center/MQTT 미응답 시 로컬 fallback 상태
```

정상 경로의 HP/down 판정은 Command Center가 소유합니다. ESP는 `ring_display` 명령을 렌더링만 하고, 로컬 HP/down은 fallback 경로에서만 사용합니다.
발사/릴레이/서보 제어는 Go2 피격 ESP에서 제거했고 `src/nIxo/` 펌웨어가 담당합니다.
