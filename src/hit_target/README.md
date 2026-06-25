# Hit Target firmware

`src/hit_target/` is the generic circular hit-target firmware for a piezo sensor plus WS2812B-style LED ring. It is intentionally separate from `firmware/go2_nixo/` because this standalone target owns its local tutorial HP/effect loop instead of delegating HP/down state to the Go2 Command Center MQTT path. Device identity is not numbered; each ESP32 derives `target_id` at boot from its eFuse MAC address, for example `hit_target_AABBCCDDEEFF`.

## UX implemented

- **Ready layer:** boot/reset starts as a full green circle so the wall target reads as “alive/ready”.
- **Phase HP layer:** HP is intentionally split into large color phases rather than one tiny `1 / max_hits` circular bar. The default is **green 5 hits → yellow 5 hits → red 5 hits**, for 15 total accepted hits. Each accepted hit removes **1/5 of the current color ring**, so HP loss is readable on a 60 LED circle.
- **Next-phase backfill:** while phase 1/2 are partially damaged, the removed rear arc is filled with a dim next-phase color (green phase shows yellow/orange behind it; yellow phase shows red behind it). One black gap LED is left at both circular boundaries so the current HP edge remains readable.
- **Phase transition:** when one color phase is depleted, the dim next-phase rear arc stays visible, and the last current-color chunk reveals the next phase color behind it as the damage chip shrinks. On hit 5 this means orange grows behind the disappearing green chunk; on hit 10 red grows behind the disappearing yellow chunk. Only after that wipe finishes does the same next-phase color become the full/current HP ring, so the “HP was stripped” animation is not covered by a sudden off/on refill.
- **Recent damage chip:** the LEDs that just disappeared stay visible as a red/orange damage chip, then quickly sequentially wipe away from the old HP edge back toward the remaining HP. This keeps “HP is decreasing” readable even when players fire rapidly.
- **Compact hit feedback:** orbit, full-ring hit flash, and HP blink/pulse effects are intentionally removed. Accepted hits only decrement HP and run the fast damage-chip wipe.
- **Activation gate:** default `always_on` keeps standalone/Go2-style targets damageable all the time. `linked_device` subscribes to the linked device status topic and turns LEDs/hit vulnerability on only while that device is active; `linked_device_kind=turret` follows turret command/pattern status, while waiting/idle/dead/stale status makes the target invulnerable and turns the ring off without losing the stored HP value.
- **Linked device defeat bridge:** the firmware publishes `destroyed=true` plus `linked_device_kind`/`linked_device_id` on `{root}/hit_targets/{target_id}/status` when HP reaches 0. Command Center currently maps `kind=turret` to a turret `dead` command; the ESP hit target itself does not command linked devices directly.
- **Lock term:** hits are ignored for `hit_cooldown_ms` after an accepted hit. This prevents rapid-fire/ISR chatter from dropping multiple HP steps at once.
- **Destroyed / defeat:** final hit lets the last red HP chunk wipe away, then a short `defeat_blackout_ms` HP=0 beat lands, a one-shot rainbow gradient sweep/fade plays for `defeat_rainbow_ms`, and the ring blacks out.
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
| Activation mode | config | `always_on` or `linked_device`; kind `turret` follows `{root}/turrets/{linked_device_id}/status`, other kinds use `{root}/devices/{linked_device_kind}/{linked_device_id}/status`. |
| Damage chip | config | Current default is 240 ms for compact/faster HP loss. |
| Defeat rainbow | config | Final HP=0 completion beat/sweep. Default `defeat_blackout_ms=90`, `defeat_rainbow_ms=1900`, `defeat_rainbow_spins=2`. |
| Phase backfill gap/scale | config | Shows dim next-phase color behind missing HP with black edge gaps. |
| Digital hit min edges / ISR debounce | config | DO-only sensitivity/noise filter. Keep `2` edges but lower debounce to catch faster piezo ringing. |

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

The helper sends one `provision {json}` line over USB serial, so Wi-Fi, Command Center/MQTT host, HP phase count, activation mode, compact visual timings, LED/reset metadata, hit sensitivity, OTA policy, and target metadata are stored in ESP32 NVS without rebuilding firmware. Leave `HIT_TARGET_TARGET_ID` empty to use the ESP32 MAC-derived `target_id`; set only `group`/`location` for human-readable placement.

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
BATTLEBANG_HIT_TARGET_DAMAGE_CHIP_MS=240 \
BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_GAP_LEDS=1 \
BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_SCALE=96 \
BATTLEBANG_HIT_TARGET_DEFEAT_BLACKOUT_MS=90 \
BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_MS=1900 \
BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_SPINS=2 \
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
- `defeat_rainbow_ms`: duration of the final HP=0 rainbow gradient before blackout. Default `1900`.
- `defeat_rainbow_spins`: how many hue rotations the rainbow completes during that defeat window. Default `2`; higher feels more energetic but can become noisy.
- `activation.mode`: `always_on` ignores linked device state and stays damageable whenever enabled; use this for standalone/Go2-style targets. `linked_device` gates LEDs/hits from a linked status topic.
- `activation.linked_device_kind`: device kind to follow. Current production value is `turret`; future/generic devices use the `devices/{kind}/{id}/status` extension point.
- `activation.linked_device_id`: linked device id to follow in `linked_device` mode, for example `turret_4`.
- `activation.stale_ms`: linked device status timeout. If no fresh linked status arrives within this window, LEDs turn off and piezo hits are ignored.
- Command Center bridge: `destroyed=true` plus `linked_device_kind=turret` and `linked_device_id` is the only coupling needed for HP0 -> turret `dead`; repeated destroyed status with the same hit `sequence` is deduped by Command Center.
- Current default UX: hit 1-5 removes green in 12 LED chunks, hit 6-10 removes yellow in 12 LED chunks, hit 11-15 removes red in 12 LED chunks, then the last red chunk wipes away, a short HP=0 blackout beat lands, and the target plays a longer rainbow defeat sweep before blackout.
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
    "damage_chip_ms": 180,
    "defeat_rainbow_ms": 1900
  },
  "activation": {
    "mode": "linked_device",
    "linked_device_kind": "turret",
    "linked_device_id": "turret_4",
    "stale_ms": 3000
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
| `activation.mode` | `always_on` or `linked_device`. | Use `linked_device` only when a linked status topic exists. |
| `activation.linked_device_kind` | Linked device kind. | `turret` maps to turret status; other kinds use typed generic device status. |
| `activation.linked_device_id` | Device id to follow. | Required for linked mode. |
| `activation.stale_ms` | Linked status freshness window. | Too high can keep vulnerability after a disconnected turret; too low can flicker if status heartbeat is slow. |
| `sensor.hit_cooldown_ms` | Lockout after one accepted hit. | Prevents one burst from dropping many HP steps. |
| `sensor.digital_hit_min_edges` | DO comparator edges required inside capture window. | `1` is more sensitive but can false-trigger; default `2` is safer. |
| `sensor.digital_isr_debounce_us` | DO edge debounce. | Lower feels more sensitive; too low can read wiring/comparator noise. |
| `ota.desired_build` | Command Center-approved build for auto OTA. | With command-center control enabled, polled OTA applies only if manifest build matches exactly. |

All operational config is represented in runtime config and persisted in NVS after provisioning/config updates. Hardware-profile fields such as `led.pin`, `led.type`, and `led.color_order` are still saved/reported through NVS, but the firmware validates them against the compiled FastLED profile; changing those physical hardware bindings requires a matching hardware-profile build.

MQTT topics use device-level discovery plus hit-target-level control:

```text
{root}/devices/hit_target/{device_id}/status
{root}/devices/hit_target/{device_id}/config
{root}/devices/hit_target/{device_id}/ota
{root}/hit_targets/{target_id}/status
{root}/hit_targets/{target_id}/config
{root}/hit_targets/{target_id}/command
{root}/hit_targets/{target_id}/ota
{root}/hit_targets/all/ota
{root}/turrets/{linked_device_id}/status        # subscribed when linked_device_kind=turret
{root}/devices/{linked_device_kind}/{linked_device_id}/status  # extension point for non-turret linked devices
```

Supported MQTT commands on `{root}/hit_targets/{target_id}/command`:

```json
{"command":"reset"}
{"command":"status"}
{"command":"enable"}
{"command":"disable"}
```

`{"command":"simulate_hit"}` is rejected unless `debug_allow_simulate_hit=true` is provisioned.

LED smoothness note: linked device status is consumed as a lightweight filtered MQTT
message. The firmware deliberately does not print every linked status payload to
USB serial because 115200-bps JSON logging can visibly stall WS2812 updates.

Local helper examples using `src/hit_target/.env.hit_target`:

```bash
# Turret-free physical bench test: open the target as always-on, then hit the piezo.
./bin/turret hit-target-mqtt bench-open \
  --host MQTT_BROKER_IP --root battlebang_btb731 \
  --target-id hit_target_AABBCCDDEEFF

# Restore normal turret-linked activation after the bench test.
./bin/turret hit-target-mqtt bench-close \
  --host MQTT_BROKER_IP --root battlebang_btb731 \
  --target-id hit_target_AABBCCDDEEFF \
  --linked-device-kind turret --linked-device-id turret_4

# Turret-free linked-mode test: keep the target configured as linked_device,
# then publish fake turret status to the exact topic the target subscribes to.
# `active` uses PATTERN/busy and should turn LED/vulnerability on while it runs.
./bin/turret hit-target-mqtt linked-device-status turret_4 active --device-kind turret \
  --host MQTT_BROKER_IP --root battlebang_btb731 \
  --duration-s 30 --interval-s 0.5

# End the fake command window. idle/WAIT_COMMAND should turn LED/vulnerability off.
./bin/turret hit-target-mqtt linked-device-status turret_4 idle --device-kind turret \
  --host MQTT_BROKER_IP --root battlebang_btb731 \
  --duration-s 3

# Make the ring 3 hits per phase, save to NVS, and reset HP to full.
./.venv-pio/bin/python scripts/hit_target/mqtt_command.py config \
  --target-id hit_target_AABBCCDDEEFF \
  --phase-count 3 --hits-per-phase 3 \
  --damage-chip-ms 240 \
  --debug-allow-simulate-hit true

# Reset, then restore the default 5 hits per phase.
./.venv-pio/bin/python scripts/hit_target/mqtt_command.py command --target-id hit_target_AABBCCDDEEFF reset
./.venv-pio/bin/python scripts/hit_target/mqtt_command.py config \
  --target-id hit_target_AABBCCDDEEFF \
  --phase-count 3 --hits-per-phase 5 --debug-allow-simulate-hit false

# Publish the current stable OTA manifest to one target.
./.venv-pio/bin/python scripts/hit_target/mqtt_command.py ota --target-id hit_target_AABBCCDDEEFF
```

OTA manifests for this firmware must use:

```json
{
  "type": "firmware",
  "app": "battlebang-hit-target",
  "hardware": "esp32dev-hit-target-ring-v1"
}
```

The default stable manifest URL is `https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json`, intentionally separate from the turret fleet `turret-fleet-latest/manifest.json` asset. Do not use repo-wide `/releases/latest/download/...` for polling in this multi-firmware release repo.
