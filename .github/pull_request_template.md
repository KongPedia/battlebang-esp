## Outcome and scope

- Observable result:
- Owning packages / files:
- Out of scope:
- Shared contracts / consumers affected:
- Environment / lockfile owner, or not initialized:

## Evidence

- Change type: guidance / directory scaffold / behavior / preserved-behavior refactor / runtime configuration
- Guidance/scaffold: metadata, references, provenance/licenses, placeholder integrity and new-file whitespace/diff checks
- Application (only when implemented and authorized): exact package commands and observed results
- Not checked and why:
- Fresh-session discovery / deployment / hardware evidence, or not performed:

## Risk and rollback

- Compatibility and local-adaptation impact:
- Revert/disable path:
- Independent review for meaningful instruction-routing, contract, concurrency or safety changes:

Do not infer passing applications or registered/discovered skills from this document or valid configuration.
Do not publish, deploy, install tools or access physical hardware merely to fill this template.

## Firmware (펌웨어·OTA 변경일 때)

- 연결 이슈: `Fixes KongPedia/battlebang-esp#N` (다른 저장소·실기 확인이 남았으면 `Refs`와 전체 경로)
- Target firmware/env:
- OTA channel / stable pointer affected: none / (예 go2-full-usb-2ch-latest)
- NVS / MQTT / packet contract changed: no (바뀌면 battlebang 부모 이슈 링크)
- 이미 배포된 장비와의 호환성 (신·구 혼재 시 동작):
- Python contract tests / `pio run -e <env>` / `git diff --check` 결과:
- 실기 업로드 + 시리얼 `show-status` 증거, 또는 N/A (소프트웨어 검증으로 실기 검증을 대신하지 않는다):
- 발사·모터 관련 변경의 사람 승인 기록, 또는 N/A:
- [ ] 비밀값(Wi-Fi·MQTT 비밀번호, `.env`)이 diff에 없다.
