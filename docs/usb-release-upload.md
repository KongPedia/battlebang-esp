# Go2 Full USB: 빌드 없이 릴리즈 업로드

> **기본 선택:** 버전을 생략하면 USB discovery 포인터가 가리키는 릴리스를 받습니다.
> 2026-11-09 확인 기준 기본값은 `0.3.40`이며, main의 Go2 Full USB 릴리스가 성공하면 새 버전으로 자동 갱신됩니다.
> 이전 `0.3.39-usb-upload`는 02번에서 `loopTask` 스택 오류로 부팅에 실패한 immutable audit 기록입니다.
> 해당 버전은 사용하지 마세요.

## 지원 범위

- 이미 provision된 Go2 Full USB 2ch / classic ESP32 / 4 MiB / Arduino `huge_app` 장치만 지원합니다.
- 업로드와 런타임 baud는 **115200 고정**입니다.
- 앱 `0x10000..0x30FFFF`만 교체합니다. NVS·부트로더·파티션표·otadata·파일시스템을 쓰거나 지우지 않습니다.
- 새 blank ESP의 첫 설치에는 부트로더·파티션·NVS identity provisioning이 별도로 필요합니다. 이 app-only
  업로더는 새 장치를 초기화하거나 ID를 설정하지 않습니다. 이미 설정된 장치의 ID/NVS 설정은 일반 업데이트에서 유지됩니다.
- 펌웨어 종류·파티션 변경, Secure Boot, Flash Encryption은 별도 정비입니다.
- 공개 `0.3.40`에는 기존 wired 고정 ID 검사가 남아 있습니다. `0.3.41`과 구분합니다.

### Wired 고정 ID 연결 해제 — `0.3.41` 게시

[`0.3.41`](https://github.com/KongPedia/battlebang-esp/releases/tag/go2-full-usb-2ch-v0.3.41)은 유효한 wired 핸드셰이크를
ESP에 저장된 Go2 ID와 비교해서 거절하지 않습니다. 공용 handler 변경이므로 USB와 UART2 빌드 양쪽에 적용됩니다.
Jetson 수신부도 ESP의 고정 ID를 연결 승인 조건으로 사용하지 않는 별도 BattleBang 변경이 함께 필요합니다.
양쪽 수정본을 적용하면 논리 ID 대신 연결된 물리 포트와 현재 세션을 기준으로 통신합니다.
패킷 형식·CRC·프로토콜 버전·세션 epoch·명령 순서·기능·발사 안전 검사는 유지합니다.

Jetson의 CC·게임용 `robot_id`나 ESP NVS의 MQTT identity를 바꾸거나 자동으로 재등록하지 않습니다.
MQTT를 직접 사용하는 구성에서는 기존 MQTT 라우팅 설정을 별도로 확인해야 합니다.
`0.3.41` 게시와 ESP 쓰기 성공은 확인됐지만, Jetson 호스트 변경을 포함한 end-to-end 통신과 부팅 후 기능은 별도 검증입니다.

## 실행

runtime이 이미 중지된 상태에서 다음 repo-free 명령을 실행합니다. 로컬 `battlebang-esp` clone이나 BattleBang repo,
Git, Docker, Python, venv, PlatformIO가 필요하지 않습니다.

```bash
(
  set -e
  uploader=$(mktemp)
  trap 'rm -f "$uploader"' EXIT
  curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 \
    -o "$uploader" \
    https://raw.githubusercontent.com/KongPedia/battlebang-esp/go2-full-usb-2ch-v0.3.40/bin/upload_esp.sh
  printf '%s  %s\n' 9515e77b7a21f015ab118602e3c50bb883a02820bb0eef962f57d39820c9343a "$uploader" \
    | sha256sum -c -
  bash "$uploader"
)
```

URL과 SHA-256은 검증된 uploader **코드**를 `0.3.40`에 고정합니다. 펌웨어 버전을 생략하면 이 uploader가
USB discovery 포인터의 최신 기본 펌웨어를 받습니다. 특정 immutable 릴리스를 받으려면 마지막 줄을
`bash "$uploader" --version 0.3.41`처럼 바꿉니다. uploader 코드 버전과 선택할 펌웨어 버전은 별개입니다.
소스 수정·검토용 clone은 별도 contributor workflow이며 operator upload에는 필요하지 않습니다.

필요한 값만 명시적으로 덮어쓰려면 위 copy block의 마지막 줄을 다음처럼 바꿉니다.

```bash
bash "$uploader" \
  --version <게시된-USB-릴리즈-버전> \
  --port /dev/serial/by-id/<실제-USB-장치> \
  --esptool /absolute/path/to/esptool
```

시리얼 접속 없이 선택 결과만 준비·확인하려면 같은 마지막 줄에 `--resolve-only`를 붙입니다.

```bash
bash "$uploader" --resolve-only
```

`--resolve-only`도 릴리즈 포인터와 업로더를 내려받고 udev 정보를 읽을 수 있지만 ESP 시리얼에는 접근하지 않습니다.

## 자동 선택 규칙

- 버전을 생략하면 OTA 채널과 분리된 `go2-full-usb-2ch-upload-latest/usb-version.txt`를 읽습니다.
  main의 관련 테스트·빌드·릴리스가 성공하면 이 포인터가 새 기본 버전으로 자동 갱신됩니다.
  repo-wide `latest`나 Go2 Full OTA stable 포인터를 사용하지 않습니다.
- `--version`을 지정하면 discovery 포인터와 무관하게 해당 versioned release를 받습니다.
- 포트를 생략하면 승인된 USB 문자 장치가 정확히 하나일 때만 선택합니다.
  - CP2102: `10c4:ea60`, 드라이버 `cp210x`
  - CH340: `1a86:7523`, 드라이버 `ch341`
- 승인 장치가 없거나 둘 이상이면 실패합니다. Jetson 번호로 칩셋이나 장치 경로를 추정하지 않습니다.
- 명시한 `--port`도 문자 장치·권한·VID/PID/driver를 다시 검사합니다.

## standalone esptool

`--esptool`을 생략한 Linux AArch64 호스트는 공식 esptool v5.6.0 아카이브를 최초 한 번 내려받아
`~/.local/share/battlebang-esp/esptool-v5.6.0/esptool`에 캐시합니다.

- Linux AArch64: 자동 다운로드 또는 명시한 `--esptool`을 지원합니다.
- Linux x86_64: 자동 다운로드를 지원하지 않으며, 호환되는 esptool v5 실행 파일을 `--esptool`로 지정해야 합니다.
- Darwin 및 그 밖의 OS: 지원하지 않습니다. 현재 위 copy block은 `0.3.40` uploader 코드에 고정되어 있으므로,
  이후 브랜치의 개선된 OS 오류 문구가 포함됐다고 간주하면 안 됩니다.

- 아카이브 SHA-256을 고정합니다.
- 아카이브 전체가 아니라 `esptool` 실행 파일만 추출합니다.
- 추출한 실행 파일의 SHA-256을 설치 전과 매 실행마다 확인합니다.
- 실행 확인은 `esptool -h`로 수행합니다. `--version`을 사용하지 않습니다.
- ELF 조사 결과 필요한 glibc symbol floor는 `GLIBC_2.17`이며 `libpython` 동적 의존성은 없습니다.
  Jetson02에서는 `PATH=/nonexistent`로 시작해 실행을 확인했습니다. 다른 Jetson/JetPack 조합은 별도 확인이 필요합니다.

자동 다운로드에 사용하는 공식 아카이브 SHA-256:

```text
7723120429394b4d6d974d694e5cf378e00259381a7f8856c2c88492041b9f59
```

캐시하는 `esptool` 실행 파일 SHA-256:

```text
d2662a740a2179cc3fa28713df13b7106e1f8108700bc62a86b13564b6d01ab7
```

## 업로드 흐름과 성공 범위

1. 버전·포트·업로더를 검증합니다.
2. 버전별 immutable 릴리즈에서 앱, 4 KiB 파티션 기준, `sha256.txt`를 받습니다.
3. 파일 크기·ESP32 magic·SHA-256을 확인합니다.
4. 장치의 파티션표 4 KiB를 읽어 승인 기준과 비교합니다.
5. 앱을 `0x10000`에 115200 baud로 압축 전송합니다.

파티션표는 비교만 하고 쓰지 않습니다. 전체 flash dump, 이전 앱 백업, NVS erase, 자동 rollback은 없습니다.
쓰기 도중 중단되면 이전 앱이 자동 복구된다고 보장하지 않습니다.

`UPLOAD_OK`는 esptool의 앱 쓰기와 기본 검증 성공만 뜻합니다. 실행 버전, identity, HP, peer readiness,
릴레이 상태, 발사·주행 등 물리 동작은 증명하지 않습니다.

이 절차는 upload-only이며 runtime이나 BattleBang 애플리케이션을 변경하지 않습니다.

## 속도

동일 source의 `0.3.40-usb-upload` / build `1040` 후보는 Jetson02에서 115200 baud로 **109초**에 업로드됐습니다.
정식 `0.3.40` / build `1041` BIN 자체를 다시 flash한 측정은 아니므로 이 시간은 참고값이며 SLA가 아닙니다.

## 현재 릴리즈 상태

2026-11-09에 공개 GitHub Releases와 USB discovery 자산을 다시 확인했습니다.

| 구분 | 확인 상태 |
| --- | --- |
| 버전 생략 시 기본값 | USB discovery `usb-version.txt`가 `0.3.40`을 가리킴 |
| 명시 선택 가능한 게시 버전 | `0.3.41` / build `1042`; wired ID 일치 검사 제거 포함 |
| Go2 Full OTA stable | `go2-full-usb-2ch-latest` 릴리스 없음; USB discovery와 별도 |
| Jetson10 `0.3.41` 증거 | `UPLOAD_OK`, 1,816,032 bytes와 공개 SHA-256 일치; 쓰기 검증만 해당 |

기본값 `0.3.40`은 [Actions run 42178252153](https://github.com/KongPedia/battlebang-esp/actions/runs/42178252153)의
source commit `f81ff50c92a7fd882424d01c4aa77b4a16fb9dab`에서 게시됐습니다.

- 릴리즈: [`go2-full-usb-2ch-v0.3.40`](https://github.com/KongPedia/battlebang-esp/releases/tag/go2-full-usb-2ch-v0.3.40)
- version/build: `0.3.40` / `1041`
- 앱: 1,816,112 bytes, SHA-256 `bace4b9aaf306caf06bb7f718dbd90f9c34baf718acaff001fb03eb40e792391`
- 파티션 기준: 4,096 bytes, SHA-256 `c798c1173b1d0175c33f06340268885c4385ff5129206d90bda416ad99ea8ddd`
- USB pointer: [`usb-version.txt`](https://github.com/KongPedia/battlebang-esp/releases/download/go2-full-usb-2ch-upload-latest/usb-version.txt), 내용 `0.3.40\n`

명시 선택 가능한 `0.3.41` 릴리스는 source commit `6033f67ad6046ba4ac4e97d01c196743e565824e`,
build `1042`, 앱 1,816,032 bytes, SHA-256
`c6fdeeee341b7780c65f82594fbcd6b970feb6815d8f6adc63a912e8e15328f7`입니다. 파티션 기준은 `0.3.40`과 같은
4,096 bytes / SHA-256 `c798c1173b1d0175c33f06340268885c4385ff5129206d90bda416ad99ea8ddd`입니다.

보고된 Jetson10 `0.3.41 UPLOAD_OK`의 크기와 SHA-256은 이 공개 자산과 일치합니다. 이는 app 쓰기 검증이며
booted version·HP·peer readiness·릴레이·발사·주행 또는 모든 Jetson 호환을 증명하지 않습니다.
버전별 릴리즈 자산은 immutable이며, 게시된 버전이 자동으로 현재 기본값이나 실물 검증 완료 상태가 되는 것은 아닙니다.

기존 `0.3.38-main`에는 새 파티션 검증 자산이 없으므로 이 업로더로 배포할 수 없습니다.

동일 source의 build `1040` 후보를 Jetson02에 업로드한 뒤 별도 명령 없이 35초간 280 frame을 passive 관찰했고,
identity `nixo_go2_02`, HP `14/14`, fire `Idle`, CRC/overflow/retry `0`을 확인했습니다. 정식 build `1041`
BIN의 재flash, 물리 발사, 경로 주행은 검증하지 않았습니다.

## 개발/CI 검증

개발·CI에서는 다음 정적/가짜 장치 테스트를 사용합니다. 현장 Jetson 업로드 의존성이 아닙니다.

```bash
python -m pytest tests/python/test_usb_release_artifacts.py tests/python/test_usb_release_upload.py -q
bash -n bin/upload_esp.sh
git diff --check
```

버전별 릴리즈 자산은 교체하지 않습니다. 같은 버전이 이미 있으면 새 버전을 발행합니다.
