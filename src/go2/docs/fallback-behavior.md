# Go2 ESP Fallback 동작

정상 구조에서는 Command Center가 hit 판정, HP, score, down 처리를 담당합니다. ESP는 정상 경로에서 HP/down을 소유하지 않고 `ring_display` 명령만 렌더링합니다.

```text
Piezo -> ESP -> MQTT -> Command Center -> MQTT -> ESP Ring LED
```

하지만 Command Center/MQTT 응답이 없으면 ESP가 기존 로컬 HP 계산을 보험으로 사용합니다.

## 정상 경로

1. ESP가 `hit_candidate` publish
2. Command Center가 hit 수락/거절 및 damage 계산
3. Command Center가 `ring_display` command publish
4. ESP가 ring LED 표시 갱신

## Fallback 경로

1. ESP가 `hit_candidate` publish 시도
2. MQTT publish 실패 또는 timeout 발생
3. ESP가 로컬 `peakToDamage()`로 damage 계산
4. ESP가 로컬 HP와 ring LED를 갱신
5. HP가 0이면 fallback 모드에서만 로컬 down LED 상태로 전환

현재 임시 damage rule:

```text
damage = piezo_peak / piezo_damage_divisor
기본 piezo_damage_divisor = 100
예: peak=4000 -> damage=40
```

이 규칙은 최종 게임룰 확정 전 임시값입니다.
