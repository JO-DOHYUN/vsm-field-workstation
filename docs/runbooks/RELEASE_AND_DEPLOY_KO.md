# RELEASE_AND_DEPLOY_KO

Release/deploy 기준 문서다. build 명령은 [[BUILD_AND_VERIFY_KO]]가 우선이다.

## Portable Folder
공식 수동 반출과 field smoke의 기본 산출물은 portable deploy folder다.
`scripts\deploy_release.bat`는 release candidate folder 생성기로 취급한다.
Portable folder에는 `hil_control_smoke.py`, `analyze_typed_capture.py`, `field_latest_capture_report.py`를 같이 둔다.
Portable folder에는 `RELEASE_MANIFEST.json`도 있어야 하며 exe/model/SBOM hash, protocol version, known issues, HIL 미검증 항목을 포함한다.

## Field Smoke
실차/벤치 검증 절차는 [[FIELD_VALIDATION_KO]]를 따른다.
최신 project-local typed capture는 아래 명령으로 먼저 요약한다.

```powershell
py scripts\field_latest_capture_report.py
```

## Installer
WiX MSI는 목표 산출물이지만 현재는 hook/stub 수준으로 본다.
실제 installer acceptance는 install/uninstall 후 clean first run까지 확인해야 한다.

## Release Metadata
Release artifact에는 아래가 남아야 한다.
- app version
- commit 또는 baseline id
- build time
- model/rules baseline
- package id
- artifact hash
- SBOM 또는 third-party notice
- sign status

## Signing
코드 서명 인증서가 없으면 signing stage는 stub로 남긴다.
단, pipeline 구조에서는 signing 단계를 분리해 둔다.
