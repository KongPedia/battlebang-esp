# Go2 ESP 문서 인덱스

Go2 등에 장착되는 ESP32 피격/LED/Nixo 통합 fallback 문서입니다. 현재 active 2-ESP split은 `src/go2/` hit/LED와 `src/nIxo/` relay fire를 사용하며, `src/go2_nixo/`는 한 ESP에 모두 넣어야 할 때만 사용합니다.

- `build-upload-workflow.md`: 로컬 secrets 생성, robot id 선택, 빌드/업로드 흐름
- `mqtt-hit-contract.md`: Command Center와 주고받는 MQTT topic/payload 계약
- `nixo-fire.md`: 통합 Go2 firmware 안의 Nixo/game blaster MQTT fire 계약과 릴레이 핀

주요 코드 위치:

```text
src/go2_nixo/
├─ main.cpp                         # setup/loop runtime orchestration
├─ build_config.h                   # 핀, MQTT topic, build-time macro
├─ robots.json                      # Go2별 non-secret profile
├─ local_secrets.example.h          # gitignore local secret template
├─ ring_led/                        # bar_display=HP bar, ring_display=Nixo fire-state ring
├─ mqtt/                            # MQTT hit candidate / heartbeat / display command
└─ nixo/                            # MQTT Nixo fire command / relay-only sequence
```

피격 scoring/down 판정은 Command Center가 소유합니다. ESP는 `hit=true` 이벤트를 보내고 legacy `ring_display` 명령을 HP bar LED로 렌더링만 합니다. 기존 ring LED는 Nixo fire 상태 표시 전용입니다. MQTT publish 실패 시에는 hit를 RAM queue에 보관했다가 재연결 후 재전송합니다.
이 integrated fallback에서는 Nixo/game blaster fire도 같은 firmware가 `battlebang/nixo/{nixo_id}/command`를 구독해서 처리합니다.
