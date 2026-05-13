# Turret build presets

이전처럼 `turret_1.h` ~ `turret_6.h`를 각각 두지 않고, 지금은 한 파일에서 관리합니다.

```text
src/turret/turrets.json
```

장점:

- 터렛 1~6 좌표를 한 곳에서 관리
- `platformio.ini`에는 env 이름만 유지
- 비어 있는 터렛은 `configured: false`로 두어 실수 빌드를 막음

현재 상태:

| Env | Turret ID | 상태 |
|---|---|---|
| `esp32dev_turret_1` | `turret_1` | 미설정 |
| `esp32dev_turret_2` | `turret_2` | 미설정 |
| `esp32dev_turret_3` | `turret_3` | 미설정 |
| `esp32dev_turret_4` | `turret_4` | 미설정 |
| `esp32dev_turret_5` | `turret_5` | `(-170, 190, 134.5) cm`, target z `70 cm` |
| `esp32dev_turret_6` | `turret_6` | 미설정 |

좌표를 채울 때 예:

```json
"turret_1": {
  "configured": true,
  "x_cm": -300.0,
  "y_cm": 470.0,
  "z_cm": 134.5,
  "default_target_z_cm": 70.0
}
```

pitch가 일정하게 높거나 낮으면 선택적으로 보정값을 추가할 수 있습니다.

```json
"pitch_offset_deg": -3.0
```
