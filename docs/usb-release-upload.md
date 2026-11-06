# Go2 Full USB: 빌드 없이 릴리즈 업로드

> **현재 공개 후보는 사용 중단:** `0.3.39-usb-upload`는 02번에서 `loopTask` 스택 오류로
> 부팅에 실패했습니다. USB latest 포인터도 이 버전을 가리키므로 수정본 실기기 검증 전까지 업로드하지 마세요.
> 수정 브랜치는 Go2 Full을 기존 Arduino 2/IDF4 플랫폼에 고정하고, 4 KiB 초과 함수 프레임은 빌드 오류로 처리하며,
> 출력하지 않는 USB 부팅 상태 JSON 생성을 생략합니다. NVS·프로토콜·115200 baud는 변경하지 않습니다.

## 지원 범위

- 이미 provision된 Go2 Full USB 2ch / classic ESP32 / 4 MiB / Arduino `huge_app` 장치만 지원합니다.
- 업로드와 런타임 baud는 **115200 고정**입니다.
- 앱 `0x10000..0x30FFFF`만 교체합니다. NVS·부트로더·파티션표·otadata·파일시스템을 쓰거나 지우지 않습니다.
- 첫 설치, 펌웨어 종류·파티션 변경, Secure Boot, Flash Encryption은 별도 정비입니다.

## 실행

기본 실행은 버전·포트·업로더를 자동으로 결정합니다.

```bash
./bin/upload_esp.sh
```

필요한 값만 명시적으로 덮어쓸 수 있습니다.

```bash
./bin/upload_esp.sh \
  --version <게시된-USB-릴리즈-버전> \
  --port /dev/serial/by-id/<실제-USB-장치> \
  --esptool /absolute/path/to/esptool
```

시리얼 접속 없이 선택 결과만 준비·확인하려면 다음 명령을 사용합니다.

```bash
./bin/upload_esp.sh --resolve-only
```

`--resolve-only`도 릴리즈 포인터와 업로더를 내려받고 udev 정보를 읽을 수 있지만 ESP 시리얼에는 접근하지 않습니다.

## 자동 선택 규칙

- 버전을 생략하면 OTA 채널과 분리된 `go2-full-usb-2ch-upload-latest/usb-version.txt`를 읽습니다.
  repo-wide `latest`나 Go2 Full OTA stable 포인터를 사용하지 않습니다.
- 포트를 생략하면 승인된 USB 문자 장치가 정확히 하나일 때만 선택합니다.
  - CP2102: `10c4:ea60`, 드라이버 `cp210x`
  - CH340: `1a86:7523`, 드라이버 `ch341`
- 승인 장치가 없거나 둘 이상이면 실패합니다. Jetson 번호로 칩셋이나 장치 경로를 추정하지 않습니다.
- 명시한 `--port`도 문자 장치·권한·VID/PID/driver를 다시 검사합니다.

## standalone esptool

`--esptool`을 생략한 Linux AArch64 호스트는 공식 esptool v5.6.0 아카이브를 최초 한 번 내려받아
`~/.local/share/battlebang-esp/esptool-v5.6.0/esptool`에 캐시합니다.

- 아카이브 SHA-256을 고정합니다.
- 아카이브 전체가 아니라 `esptool` 실행 파일만 추출합니다.
- 추출한 실행 파일의 SHA-256을 설치 전과 매 실행마다 확인합니다.
- 실행 확인은 `esptool -h`로 수행합니다. `--version`을 사용하지 않습니다.
- ELF 조사 결과 필요한 glibc symbol floor는 `GLIBC_2.17`이며 `libpython` 동적 의존성은 없습니다.
  표준 Ubuntu 22.04 AArch64 ABI와 호환되는 조건이지만, 실제 Jetson/JetPack에서의 실행은 아직 검증하지 않았습니다.

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

## 속도

현재 공개 앱 2,339,440 bytes의 zlib9 압축 추정은 1,381,078 bytes입니다. 115200/8N1에서 압축 데이터
전송만 최소 약 **119.9초**이며 실제 총 시간은 더 깁니다. 10초 완료를 약속하지 않습니다.

## 현재 릴리즈 상태

[Actions run 42011454580](https://github.com/KongPedia/battlebang-esp/actions/runs/42011454580)이 성공했고,
source commit `a147ee1a714a1cdae23f5f746c948d2d1135a387`에서 USB 전용 prerelease를 게시했습니다.

- 릴리즈: [`go2-full-usb-2ch-v0.3.39-usb-upload`](https://github.com/KongPedia/battlebang-esp/releases/tag/go2-full-usb-2ch-v0.3.39-usb-upload)
- version/build: `0.3.39-usb-upload` / `1039`
- 앱: 2,339,440 bytes, SHA-256 `86f705503bfd1f678831bf5f98b82b0e744aa746477f47025b5d04a5f6cb9db4`
- 파티션 기준: 4,096 bytes, SHA-256 `c798c1173b1d0175c33f06340268885c4385ff5129206d90bda416ad99ea8ddd`
- USB pointer: [`usb-version.txt`](https://github.com/KongPedia/battlebang-esp/releases/download/go2-full-usb-2ch-upload-latest/usb-version.txt), 내용 `0.3.39-usb-upload\n`

공개 앱 다운로드의 SHA-256은 인증 없는 HTTPS로 확인했습니다. 버전별 앱·파티션·checksum은 CI artifact와
같고 immutable입니다. USB prerelease와 upload pointer만 게시했으며 OTA polling tag는 변경하지 않았습니다.

기존 `0.3.38-main`에는 새 파티션 검증 자산이 없으므로 이 업로더로 배포할 수 없습니다.

업로더 진입점은 아직 `main`에 병합되지 않았습니다. 깨끗한 checkout에서 최초 한 번 브랜치를 받습니다.

```bash
git fetch origin feat/esp-release-usb-upload
git switch --create feat/esp-release-usb-upload --track origin/feat/esp-release-usb-upload
```

이후 일반 Shell에서 `./bin/upload_esp.sh`를 실행합니다. host Python/venv/PlatformIO는 필요하지 않습니다.

이 문서 갱신에서는 장치 명령, 업로드, 재시작, 물리 동작 검증을 수행하지 않았습니다.

## 개발/CI 검증

개발·CI에서는 다음 정적/가짜 장치 테스트를 사용합니다. 현장 Jetson 업로드 의존성이 아닙니다.

```bash
python -m pytest tests/python/test_usb_release_artifacts.py tests/python/test_usb_release_upload.py -q
bash -n bin/upload_esp.sh
git diff --check
```

버전별 릴리즈 자산은 교체하지 않습니다. 같은 버전이 이미 있으면 새 버전을 발행합니다.
