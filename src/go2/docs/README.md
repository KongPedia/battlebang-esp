# Go2 ESP 문서 인덱스

Go2 등에 장착되는 ESP32 피격/LED 보드용 문서입니다. 터렛 펌웨어처럼 Go2도 `src/go2/` 아래에 빌드 설정, 펌웨어 진입점, 기능 모듈, 문서를 모아둡니다.

- `build-upload-workflow.md`: 로컬 secrets 생성, robot id 선택, 빌드/업로드 흐름
- `mqtt-hit-contract.md`: Command Center와 주고받는 MQTT topic/payload 계약

주요 코드 위치:

```text
src/go2/
├─ main.cpp                         # setup/loop runtime orchestration
├─ build_config.h                   # 핀, MQTT topic, build-time macro
├─ robots.json                      # Go2별 non-secret profile
├─ local_secrets.example.h          # gitignore local secret template
├─ display/                         # bar_display=HP bar, ring_display=Nixo fire/cooldown ring
└─ mqtt/                            # MQTT hit candidate / heartbeat / display command
```

피격 scoring/down 판정은 Command Center가 소유합니다. ESP는 piezo AO ADC threshold를 넘은 입력만 `hit_candidate(hit=true, peak, threshold)`로 보내고 legacy `ring_display` 명령을 HP bar LED로 렌더링만 합니다. 기존 ring LED는 Nixo fire/cooldown 표시 전용입니다. MQTT publish 실패 시에는 hit를 RAM queue에 보관했다가 재연결 후 재전송합니다.
발사/릴레이/서보 제어는 Go2 피격 ESP에서 제거했고 `src/nIxo/` 펌웨어가 담당합니다.
