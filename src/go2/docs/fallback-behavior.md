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

## Reset 경로

Command Center의 `POST /api/robots/{robot_id}/hit/reset`은 서버 hit runtime을 초기화한 뒤 ESP에
`ring_display` command를 내려보냅니다.

이때 payload에 `reset_hit_state=true`가 포함되면 ESP는 display override만 갱신하지 않고 로컬 fallback 상태도
같이 초기화합니다.

초기화 대상:

- fallback HP: `HP_MAX`
- fallback down flag: `false`
- pending hit fallback
- piezo sensor latch/flags
- damage blink / remote down display latch

따라서 reset 이후 Command Center/MQTT가 잠시 끊겨 ESP가 fallback 렌더링으로 돌아가도, 이전 HP 0/down LED 상태가
다시 나타나지 않아야 합니다.

현재 임시 damage rule:

```text
damage = piezo_peak / piezo_damage_divisor
기본 piezo_damage_divisor = 100
예: peak=4000 -> damage=40
```

이 규칙은 최종 게임룰 확정 전 임시값입니다.
