# Hit Target firmware

`src/hit_target/` is the generic circular hit-target firmware for a piezo sensor plus WS2812B-style LED ring. It is intentionally separate from `src/go2_nixo/` because this standalone target owns its local tutorial HP/effect loop instead of delegating HP/down state to the Go2 Command Center MQTT path. Device identity is not numbered; each ESP32 derives `target_id` at boot from its eFuse MAC address, for example `hit_target_AABBCCDDEEFF`.

## UX implemented

- **Ready layer:** boot/reset starts as a full green circle so the wall target reads as “alive/ready”.
- **Phase HP layer:** HP is intentionally split into large color phases rather than one tiny `1 / max_hits` circular bar. The default is **green 5 hits → yellow 5 hits → red 5 hits**, for 15 total accepted hits. Each accepted hit removes **1/5 of the current color ring**, so HP loss is readable on a 60 LED circle.
- **Next-phase backfill:** while phase 1/2 are partially damaged, the removed rear arc is filled with a dim next-phase color (green phase shows yellow/orange behind it; yellow phase shows red behind it). One black gap LED is left at both circular boundaries so the current HP edge remains readable.
- **Phase transition:** when one color phase is depleted, the dim next-phase rear arc stays visible, and the last current-color chunk reveals the next phase color behind it as the damage chip shrinks. On hit 5 this means orange grows behind the disappearing green chunk; on hit 10 red grows behind the disappearing yellow chunk. Only after that wipe finishes does the same next-phase color become the full/current HP ring, so the “HP was stripped” animation is not covered by a sudden off/on refill.
- **Recent damage chip:** the LEDs that just disappeared stay visible as a red/orange damage chip, then quickly sequentially wipe away from the old HP edge back toward the remaining HP. This keeps “HP is decreasing” readable even when players fire rapidly.
- **Independent overlay layer:** a neutral-white orbit highlight keeps travelling the full 360° ring in both ready-green and damaged phase-HP states. The tail is intentionally neutral, not green/cyan, so dark HP gaps do not show a green orbit.
- **Hit flash:** a very short full-ring white flash for `hit_flash_ms` (50 ms fallback, config-tunable). It confirms impact without hiding HP for long; the remaining HP also gets a short brightness pulse after the flash.
- **Lock term:** hits are ignored for `hit_cooldown_ms` after an accepted hit while the orbit keeps moving. This prevents rapid-fire/ISR chatter from dropping multiple HP steps at once.
- **Destroyed / defeat:** final hit keeps the short white confirmation, then the last red HP chunk still wipes away as the normal damage-chip animation. After a short `defeat_blackout_ms` HP=0 beat, a one-shot rainbow gradient sweep/fade plays for `defeat_rainbow_ms`, then both HP + orbit layers turn off. This makes “defeated” read as a reward/completion moment, not another damage blink.
- **Initialize/reset:** USB serial `r`/`2` and ESP32 BOOT/GPIO0 long-press reset the target to green ready without power cycling.
- **Non-blocking loop:** LED animation, cooldown, re-arm, capture window, and serial commands are all `millis()`-driven. No effect path uses `delay()` except the 1 ms loop yield and boot settle.

## Default wiring

Defaults are migrated from the wall-mounted target bench sketch on `origin/HeavyBlaster_HWsystem_test`; the old standalone sketch path is not kept in this branch:

| Function | Default GPIO | Notes |
| --- | ---: | --- |
| LED ring data | 18 | Migrated from the wall-mounted target sketch. |
| LED type/order | WS2812B / GRB | Override if the ring colors are swapped. |
| Piezo DO | 27 | Digital comparator input; can be `-1` to disable. |
| Piezo AO | -1 | Disabled by default; set an ADC1 GPIO such as 34 only if AO is wired. |
| BOOT reset button | 0 | ESP32 BOOT/GPIO0; hold after boot to initialize/reset. Set `-1` to disable. |
| LED count | 60 | Override for a different ring. |
| HP phases | config | Default `hp_phase_count=3`, `hits_per_phase=5`, `max_hits=15`. |
| Hit cooldown | config | Lock term after one accepted hit. |
| Hit threshold / re-arm threshold | config | AO sensitivity only. Lower `hit_threshold` is more sensitive; keep re-arm lower than hit threshold. |
| Hit flash | config | Recommended 40-60 ms so HP stays readable. |
| Damage chip | config | Recommended 300-650 ms; lower values make HP loss feel faster. Current default is 580 ms for a clearer sequential wipe. |
| Defeat rainbow | config | Final HP=0 completion beat/sweep. Default `defeat_blackout_ms=90`, `defeat_rainbow_ms=900`, `defeat_rainbow_spins=2`. |
| Phase backfill gap/scale | config | Shows dim next-phase color behind missing HP with black edge gaps. |
| HP hit pulse | config | Short brightness pulse on remaining HP after the hit flash. |
| Digital hit min edges / ISR debounce | config | DO-only sensitivity/noise filter. Keep `2` edges but lower debounce to catch faster piezo ringing. |
| Orbit step | config | Lower is faster. |

The photo wiring still needs bench confirmation before upload. Keep the ring on a stable 5 V supply and share GND with the ESP32. Do not power a high-current ring only from the ESP32 3.3 V rail.

## Build

```bash
./.venv-pio/bin/pio run -e esp32dev_hit_target
```

## Upload

List ports, then upload explicitly:

```bash
./.venv-pio/bin/pio device list
./.venv-pio/bin/pio run -e esp32dev_hit_target -t upload --upload-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```

Serial commands:

- `r` or `2`: reset HP/effect state
- `h`: simulate a hit without touching the piezo
- `s`: print status with the latest analog reading

Physical initialize:

- Hold ESP32 `BOOT` for `reset_button_hold_ms` after the firmware has booted. Do not hold BOOT while power-cycling/resetting the board, because GPIO0 is also the ESP32 bootloader strap pin.

Serial events are JSON lines (`hit`, `destroyed`, `reset`, `heartbeat`) and include both `target_id` and `device_mac`, so a future game controller can discover devices without a numbered build profile. Hit/reset/status events also include `source` (`boot`, `serial`, or `piezo`) to make bench debugging obvious when the comparator input is noisy.

## Local env / provisioning

`hit_target` keeps its own local runtime env file, separate from `turret_fleet` and Go2 builds:

```bash
cp src/hit_target/.env.hit_target.example src/hit_target/.env.hit_target
# edit the gitignored file:
#   HIT_TARGET_WIFI_SSID / HIT_TARGET_WIFI_PASSWORD
#   HIT_TARGET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS
#   HIT_TARGET_MQTT_PORT=1883
#   HIT_TARGET_MQTT_ROOT=battlebang
./.venv-pio/bin/python scripts/hit_target/provision.py --print-json --no-serial
./.venv-pio/bin/python scripts/hit_target/provision.py --serial-port /dev/cu.usbserial-XXXX
```

The helper sends one `provision {json}` line over USB serial, so Wi-Fi, Command Center/MQTT host, HP phase count, hit sensitivity, OTA policy, and target metadata are stored in ESP32 NVS without rebuilding firmware. Leave `HIT_TARGET_TARGET_ID` empty to use the ESP32 MAC-derived `target_id`; set only `group`/`location` for human-readable placement.

The real env file is intentionally ignored by `.gitignore` (`src/hit_target/.env.hit_target` and generic `src/*/.env.*`). Commit only `src/hit_target/.env.hit_target.example`.

## Debugging/tuning pins and thresholds

Non-secret, commit-safe defaults live in `config.json`. For one-off bench tuning, override with shell env instead of editing code:

```bash
BATTLEBANG_HIT_TARGET_LED_PIN=18 \
BATTLEBANG_HIT_TARGET_NUM_LEDS=60 \
BATTLEBANG_HIT_TARGET_LED_TYPE=WS2812B \
BATTLEBANG_HIT_TARGET_COLOR_ORDER=GRB \
BATTLEBANG_HIT_TARGET_MAX_HITS=15 \
BATTLEBANG_HIT_TARGET_HP_PHASE_COUNT=3 \
BATTLEBANG_HIT_TARGET_HITS_PER_PHASE=5 \
BATTLEBANG_HIT_TARGET_HIT_COOLDOWN_MS=200 \
BATTLEBANG_HIT_TARGET_HIT_REARM_STABLE_MS=80 \
BATTLEBANG_HIT_TARGET_DIGITAL_HIT_MIN_EDGES=2 \
BATTLEBANG_HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US=5000 \
BATTLEBANG_HIT_TARGET_HIT_FLASH_MS=50 \
BATTLEBANG_HIT_TARGET_DAMAGE_CHIP_MS=580 \
BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_GAP_LEDS=1 \
BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_SCALE=96 \
BATTLEBANG_HIT_TARGET_HP_HIT_PULSE_MS=180 \
BATTLEBANG_HIT_TARGET_DEFEAT_BLACKOUT_MS=90 \
BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_MS=900 \
BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_SPINS=2 \
BATTLEBANG_HIT_TARGET_COOLDOWN_BLINK_MS=60 \
BATTLEBANG_HIT_TARGET_ORBIT_STEP_MS=20 \
BATTLEBANG_HIT_TARGET_PIEZO_DO_PIN=27 \
BATTLEBANG_HIT_TARGET_PIEZO_AO_PIN=-1 \
BATTLEBANG_HIT_TARGET_RESET_BUTTON_PIN=0 \
BATTLEBANG_HIT_TARGET_RESET_BUTTON_HOLD_MS=1200 \
BATTLEBANG_HIT_TARGET_HIT_THRESHOLD=1400 \
BATTLEBANG_HIT_TARGET_HIT_REARM_THRESHOLD=800 \
./.venv-pio/bin/pio run -e esp32dev_hit_target
```

If the analog line is not wired yet, use `BATTLEBANG_HIT_TARGET_PIEZO_AO_PIN=-1` and validate with DO-only hits. In that DO-only mode, `hit_threshold` does **not** decide physical sensitivity; tune `digital_isr_debounce_us`, `digital_hit_min_edges`, and the comparator board's physical potentiometer. If DO chatters or the comparator threshold is poorly tuned, use AO-only by setting `BATTLEBANG_HIT_TARGET_PIEZO_DO_PIN=-1`.

### Gameplay tuning notes

- `hp_phase_count`: number of full-ring color phases. Default `3`.
- `hits_per_phase`: accepted hits needed to strip one phase. Default `5`.
- `max_hits`: must equal `hp_phase_count * hits_per_phase`; the firmware fails the build if these drift apart.
- `hit_threshold`: AO-only hit threshold. Lower is more sensitive. Default `1400`.
- `hit_rearm_threshold`: AO-only quiet threshold before re-arming. Must stay below `hit_threshold`; default `800`.
- `digital_hit_min_edges`: filters DO-only idle noise by requiring multiple comparator edges inside `capture_window_ms`. Default `2`; set to `1` only if real physical hits are still missed after debounce/potentiometer tuning.
- `digital_isr_debounce_us`: DO edge debounce. Lower catches faster piezo ringing and feels more sensitive; default `5000`. Raise it if idle noise returns.
- `phase_backfill_gap_leds`: black gap LEDs at both boundaries between current HP and the dim next-phase rear arc. Default `1`.
- `phase_backfill_scale`: brightness scale for the next-phase rear arc, 0-255. Default `96` so it is readable but weaker than current HP.
- `defeat_blackout_ms`: short all-off beat after the final HP chunk disappears and before the rainbow starts. Default `90`; set `0` for immediate rainbow, keep short so the target does not look broken.
- `defeat_rainbow_ms`: duration of the final HP=0 rainbow gradient before blackout. Default `900`; shorten for snappier arcade feedback, lengthen only if the target is used as a celebratory tutorial marker.
- `defeat_rainbow_spins`: how many hue rotations the rainbow completes during that defeat window. Default `2`; higher feels more energetic but can become noisy.
- Current default UX: hit 1-5 removes green in 12 LED chunks, hit 6-10 removes yellow in 12 LED chunks, hit 11-15 removes red in 12 LED chunks, then the last red chunk wipes away, a short HP=0 blackout beat lands, and the target plays a rainbow defeat sweep before blackout.
- To make HP loss even more dramatic, lower `hits_per_phase` (for example 3). To make the target tougher without making each hit visually tiny, increase `hp_phase_count` instead of making one single-color ring have dozens of micro-steps.

## Runtime config / NVS / MQTT / OTA model

The firmware now boots with the values in `config.json` as **factory defaults**, then overlays any saved NVS runtime config. This means one generic `esp32dev_hit_target` image can be flashed once and later tuned without rebuilding. Local bench/provisioning values live in `src/hit_target/.env.hit_target`; Wi-Fi and Command Center IP/DNS are **not** hardcoded in `src/` or in GitHub Actions.

Serial provisioning commands:

- `show-config`: print current runtime config with Wi-Fi/MQTT passwords masked.
- `show-config-secrets`: print secrets too; use only on a trusted bench terminal.
- `config {json}`: apply a versioned runtime config patch.
- `provision {json}`: same as `config`, but also marks the target as configured.
- `clear-config`: erase NVS and return to factory defaults.
- `start-network` / `stop-network`: manually start or stop Wi-Fi/MQTT.
- `check-ota [url]`: fetch and validate an OTA manifest.

Example: make this target a 30-hit target without rebuilding:

```json
{
  "type": "config",
  "config_version": 2,
  "hp": {
    "phase_count": 3,
    "hits_per_phase": 10,
    "palette": ["#009600", "#BE8200", "#BE0000"]
  },
  "visual": {
    "damage_chip_ms": 420,
    "orbit_step_ms": 16
  },
  "sensor": {
    "digital_hit_min_edges": 2,
    "digital_isr_debounce_us": 4000,
    "hit_cooldown_ms": 180
  }
}
```

What each common config value changes:

| Field | Effect | Tuning warning |
| --- | --- | --- |
| `hp.phase_count` | Number of large full-ring HP phases. | More phases make a tougher target without tiny per-hit chunks. |
| `hp.hits_per_phase` | Accepted hits needed to strip one phase. | `3 phases x 10` = 30 hits; gameplay config changes reset HP to full. |
| `hp.palette` | Phase colors. Default green → yellow/orange → red. | Use readable high-contrast colors; dark colors reduce HP readability. |
| `visual.damage_chip_ms` | How long the removed HP chunk sequentially wipes away. | Lower feels faster; too low makes HP loss hard to see. |
| `visual.orbit_step_ms` | Orbit speed. Lower is faster. | Keep orbit moving even in green ready state so it reads as a target. |
| `visual.hit_flash_ms` | Full white impact flash duration. | Keep around 40-60 ms so HP remains readable under rapid fire. |
| `sensor.hit_cooldown_ms` | Lockout after one accepted hit. | Prevents one burst from dropping many HP steps. |
| `sensor.digital_hit_min_edges` | DO comparator edges required inside capture window. | `1` is more sensitive but can false-trigger; default `2` is safer. |
| `sensor.digital_isr_debounce_us` | DO edge debounce. | Lower feels more sensitive; too low can read wiring/comparator noise. |
| `ota.desired_build` | Command Center-approved build for auto OTA. | With command-center control enabled, polled OTA applies only if manifest build matches exactly. |

Hardware-profile fields `led.pin`, `led.type`, and `led.color_order` are intentionally not true remote-runtime fields in this step because FastLED binds them at compile time. They remain in config/status for visibility, but changing them requires a matching hardware-profile build.

MQTT topics use device-level discovery plus hit-target-level control:

```text
{root}/devices/{device_id}/status
{root}/devices/{device_id}/config
{root}/devices/{device_id}/ota
{root}/hit_targets/{target_id}/status
{root}/hit_targets/{target_id}/config
{root}/hit_targets/{target_id}/command
{root}/hit_targets/{target_id}/ota
{root}/hit_targets/all/ota
```

Supported MQTT commands on `{root}/hit_targets/{target_id}/command`:

```json
{"command":"reset"}
{"command":"status"}
{"command":"enable"}
{"command":"disable"}
```

`{"command":"simulate_hit"}` is rejected unless `debug_allow_simulate_hit=true` is provisioned.

OTA manifests for this firmware must use:

```json
{
  "type": "firmware",
  "app": "battlebang-hit-target",
  "hardware": "esp32dev-hit-target-ring-v1"
}
```

The default stable manifest URL is `https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json`, intentionally separate from the turret fleet `turret-fleet-latest/manifest.json` asset. Do not use repo-wide `/releases/latest/download/...` for polling in this multi-firmware release repo.
