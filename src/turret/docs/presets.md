# Turret Presets

터렛별 빌드 프리셋 헤더입니다.

현재 값 기준:
- `turret_3`, `turret_4`는 사용자가 예시로 준 `(-170, 190)` 좌표를 반영
- 나머지는 현재 v2 기준 기본값 `(-300, 470, 134.5, 70.0)`를 넣어둠

즉시 현장 사용 전 반드시 실제 설치 좌표로 갱신하세요.

각 파일은 아래 값만 override 합니다.
- `TURRET_ID`
- `TURRET_X_CM`
- `TURRET_Y_CM`
- `TURRET_Z_CM`
- `TURRET_DEFAULT_TARGET_Z_CM`

## PlatformIO env 매핑

- `esp32dev_turret_1` -> `turret_1.h`
- `esp32dev_turret_2` -> `turret_2.h`
- `esp32dev_turret_3` -> `turret_3.h`
- `esp32dev_turret_4` -> `turret_4.h`
- `esp32dev_turret_5` -> `turret_5.h`
- `esp32dev_turret_6` -> `turret_6.h`
