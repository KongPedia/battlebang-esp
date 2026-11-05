#!/usr/bin/env bash
set -euo pipefail
umask 077

readonly PRODUCT="go2-full-usb-2ch"
readonly APP_OFFSET="0x10000"
readonly APP_MAX_BYTES=$((0x300000))
readonly PARTITION_OFFSET="0x8000"
readonly PARTITION_LENGTH="0x1000"
readonly PARTITION_BYTES=$((PARTITION_LENGTH))
readonly BAUD="115200"

usage() {
  cat >&2 <<'EOF'
Usage: upload_esp.sh [--version <version>] [--port <character-device>] [--esptool <path>] [--resolve-only]
Defaults: latest USB-only release, single approved USB port, verified standalone uploader.
--resolve-only prepares and prints selection without accessing serial.
EOF
}

die() {
  printf 'UPLOAD_FAILED: %s\n' "$*" >&2
  exit 1
}

version=
port=
esptool=
resolve_only=false
while (($#)); do
  case "$1" in
    --version | --port | --esptool)
      (($# >= 2)) || { usage; exit 2; }
      case "$1" in
        --version) version=$2 ;;
        --port) port=$2 ;;
        --esptool) esptool=$2 ;;
      esac
      shift 2
      ;;
    --resolve-only) resolve_only=true; shift ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

for command in curl sha256sum timeout mktemp rm wc date od udevadm readlink; do
  command -v "$command" >/dev/null 2>&1 || die "required OS command not found: $command"
done

tmp_dir=
cleanup() {
  [[ -z ${tmp_dir:-} ]] || rm -rf -- "$tmp_dir"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/battlebang-esp-upload.XXXXXX")

started_at=$(date +%s)
stage() {
  printf '[%ss] %s\n' "$(( $(date +%s) - started_at ))" "$1" >&2
}

download_url() {
  curl --fail --location --silent --show-error \
    --proto '=https' --proto-redir '=https' --tlsv1.2 \
    --retry 3 --retry-delay 1 \
    --connect-timeout 10 --max-time 180 \
    --max-filesize "$3" --output "$2" "$1"
}

file_size() {
  local size
  size=$(wc -c < "$1")
  printf '%s' "${size//[[:space:]]/}"
}

hash_file() {
  local digest ignored
  read -r digest ignored < <(sha256sum "$1")
  printf '%s' "$digest"
}

# Resolve only this upload product; it is not an OTA polling channel.
if [[ -z $version ]]; then
  stage "resolving latest USB release"
  download_url "https://github.com/KongPedia/battlebang-esp/releases/download/go2-full-usb-2ch-upload-latest/usb-version.txt" \
    "$tmp_dir/usb-version.txt" 128 || die "USB version pointer unavailable"
  version=$(< "$tmp_dir/usb-version.txt")
fi
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9][A-Za-z0-9._-]*)?$ ]] || die "invalid version"

approved_usb() {
  [[ $1:$2:$3 == 10c4:ea60:cp210x || $1:$2:$3 == 1a86:7523:ch341 ]]
}

if [[ -z $port ]]; then
  timeout --kill-after=5s 15s udevadm info --export-db > "$tmp_dir/udev-db" || die "USB inventory failed"
  (( $(file_size "$tmp_dir/udev-db") <= 2097152 )) || die "USB inventory too large"
  candidate= links= vid= pid= driver= selected_real= count=0
  while IFS= read -r line || [[ -n $line ]]; do
    case "$line" in
      'E: DEVNAME='*) candidate=${line#*=} ;;
      'E: DEVLINKS='*) links=${line#*=} ;;
      'E: ID_VENDOR_ID='*) vid=${line#*=} ;;
      'E: ID_MODEL_ID='*) pid=${line#*=} ;;
      'E: ID_USB_DRIVER='*) driver=${line#*=} ;;
      '')
        if [[ -c $candidate ]] && approved_usb "$vid" "$pid" "$driver"; then
          real=$(readlink -f -- "$candidate") || die "cannot resolve USB device"
          if [[ $real != "$selected_real" ]]; then
            ((count += 1))
            port=$candidate
            selected_real=$real
            for link in $links; do
              if [[ $link == /dev/serial/by-id/* && -c $link ]]; then port=$link; break; fi
            done
          fi
        fi
        candidate= links= vid= pid= driver=
        ;;
    esac
  done < <(printf '%s\n\n' "$(< "$tmp_dir/udev-db")")
  (( count == 1 )) || die "expected one approved USB adapter; found $count (multiple adapters require --port)"
fi
[[ $port != *[[:cntrl:]]* && $port == /* && -c $port && -r $port && -w $port ]] || die "port must be a readable/writable character device: $port"
timeout --kill-after=5s 15s udevadm info --query=property --name="$port" > "$tmp_dir/udev-port" || die "USB device properties unavailable"
vid= pid= driver=
while IFS= read -r line; do
  case "$line" in
    ID_VENDOR_ID=*) vid=${line#*=} ;;
    ID_MODEL_ID=*) pid=${line#*=} ;;
    ID_USB_DRIVER=*) driver=${line#*=} ;;
  esac
done < "$tmp_dir/udev-port"
approved_usb "$vid" "$pid" "$driver" || die "unsupported USB VID/PID/driver: $vid:$pid:$driver"

if [[ -z $esptool ]]; then
  [[ $(uname -s) == Linux && $(uname -m) == aarch64 ]] || die "automatic uploader supports Linux AArch64 only; pass --esptool"
  for command in tar gzip chmod mkdir mv; do
    command -v "$command" >/dev/null 2>&1 || die "required OS command not found: $command"
  done
  [[ ${HOME:-} == /* ]] || die "HOME must be an absolute directory"
  cache="$HOME/.local/share/battlebang-esp/esptool-v5.6.0"
  esptool="$cache/esptool"
  readonly tool_hash="d2662a740a2179cc3fa28713df13b7106e1f8108700bc62a86b13564b6d01ab7"
  if [[ ! -e $esptool ]]; then
    stage "preparing official standalone uploader (first use only)"
    download_url "https://github.com/espressif/esptool/releases/download/v5.6.0/esptool-v5.6.0-linux-aarch64.tar.gz" \
      "$tmp_dir/esptool.tar.gz" 83886080 || die "uploader download failed"
    [[ $(hash_file "$tmp_dir/esptool.tar.gz") == 7723120429394b4d6d974d694e5cf378e00259381a7f8856c2c88492041b9f59 ]] || die "uploader archive checksum mismatch"
    tar -xzf "$tmp_dir/esptool.tar.gz" -O esptool-linux-aarch64/esptool > "$tmp_dir/esptool" || die "uploader extraction failed"
    [[ $(hash_file "$tmp_dir/esptool") == "$tool_hash" ]] || die "uploader executable checksum mismatch"
    mkdir -p -- "$cache"
    install_tmp=$(mktemp "$cache/.esptool.XXXXXX")
    # Rename on the cache filesystem so interrupted downloads never become executable cache hits.
    if ! { tar -xzf "$tmp_dir/esptool.tar.gz" -O esptool-linux-aarch64/esptool > "$install_tmp" && chmod 700 "$install_tmp" && mv -f -- "$install_tmp" "$esptool"; }; then
      rm -f -- "$install_tmp"
      die "uploader installation failed"
    fi
  fi
  [[ $(hash_file "$esptool") == "$tool_hash" ]] || die "cached uploader checksum mismatch; remove the corrupted cache file"
fi
[[ $esptool == /* && $esptool != *[[:cntrl:]]* && -x $esptool ]] || die "--esptool must be an absolute executable path"
timeout --kill-after=5s 15s "$esptool" -h > "$tmp_dir/uploader-help" 2>&1 || die "standalone uploader cannot start"
[[ $(< "$tmp_dir/uploader-help") == *'esptool v5.'* ]] || die "standalone uploader must support esptool v5 commands"

if $resolve_only; then
  printf 'version=%s\nport=%s\nesptool=%s\nvid=%s\npid=%s\ndriver=%s\n' "$version" "$port" "$esptool" "$vid" "$pid" "$driver"
  exit 0
fi

readonly stem="battlebang-go2-full-usb-2ch-${version}"
readonly firmware_name="${stem}.bin"
readonly partitions_name="${stem}.partitions.bin"
readonly release_url="https://github.com/KongPedia/battlebang-esp/releases/download/${PRODUCT}-v${version}"
readonly firmware="$tmp_dir/$firmware_name"
readonly partitions="$tmp_dir/$partitions_name"
readonly checksums="$tmp_dir/sha256.txt"
readonly device_partitions="$tmp_dir/device-partitions.bin"
download() { download_url "$release_url/$1" "$2" "$3"; }

stage "downloading release assets"
download "sha256.txt" "$checksums" 65536 || die "checksum download failed"
download "$firmware_name" "$firmware" "$APP_MAX_BYTES" || die "firmware download failed"
download "$partitions_name" "$partitions" "$PARTITION_BYTES" || die "partition metadata download failed"

firmware_size=$(file_size "$firmware")
partitions_size=$(file_size "$partitions")
(( firmware_size >= 32 && firmware_size <= APP_MAX_BYTES )) || die "firmware size is outside app0 bounds"
(( partitions_size == PARTITION_BYTES )) || die "partition metadata must be exactly 4096 bytes"
firmware_magic=$(od -An -tx1 -N1 "$firmware")
firmware_magic=${firmware_magic//[[:space:]]/}
[[ $firmware_magic == "e9" ]] || die "firmware is not an ESP32 application image"

firmware_hash=
partitions_hash=
firmware_entries=0
partitions_entries=0
while IFS= read -r line || [[ -n $line ]]; do
  [[ -n $line ]] || continue
  read -r digest path extra <<< "$line"
  [[ $digest =~ ^[0-9a-f]{64}$ && -n $path && -z ${extra:-} ]] || die "malformed checksum entry"
  case "$path" in
    "dist/$firmware_name")
      firmware_hash=$digest
      ((firmware_entries += 1))
      ;;
    "dist/$partitions_name")
      partitions_hash=$digest
      ((partitions_entries += 1))
      ;;
    *) die "unexpected checksum entry: $path" ;;
  esac
done < "$checksums"
(( firmware_entries == 1 && partitions_entries == 1 )) || die "checksum file must contain each release asset exactly once"
[[ $(hash_file "$firmware") == "$firmware_hash" ]] || die "firmware checksum mismatch"
[[ $(hash_file "$partitions") == "$partitions_hash" ]] || die "partition metadata checksum mismatch"

stage "checking provisioned partition layout"
if ! timeout --kill-after=5s 45s "$esptool" --chip esp32 --port "$port" --baud "$BAUD" \
  read-flash "$PARTITION_OFFSET" "$PARTITION_LENGTH" "$device_partitions"; then
  die "partition layout read failed or timed out"
fi
[[ $(file_size "$device_partitions") == "$PARTITION_BYTES" ]] || die "device partition read returned the wrong size"
[[ $(hash_file "$device_partitions") == "$partitions_hash" ]] || die "device partition layout does not match this release"

stage "uploading application at 115200 baud"
if ! timeout --kill-after=5s 600s "$esptool" --chip esp32 --port "$port" --baud "$BAUD" \
  write-flash --compress "$APP_OFFSET" "$firmware"; then
  die "application upload failed or timed out"
fi

printf 'UPLOAD_OK: version=%s sha256=%s size=%s\n' "$version" "$firmware_hash" "$firmware_size"
printf 'Upload only: runtime version, HP, relay, and physical behavior were not validated.\n'
