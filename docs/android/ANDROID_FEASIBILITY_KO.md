# Android / Wireless Feasibility

## 현재 결론

Android 앱 구현은 아직 시작하지 않는다. 현재 기준 제품은 Windows VSM Field RC이며, CSM 통신은 USB CDC typed stream으로 고정한다.

현재 CSM 펌웨어 repo 기준 active path는 USB CDC typed uplink/downlink다. Wi-Fi, BLE, TCP, UDP, WebSocket transport는 구현되어 있지 않다.

Portenta H7 하드웨어는 Wi-Fi/Bluetooth를 지원하므로 무선화 가능성은 있다. 단, typed stream transport와 control safety gate를 새 전송 경로에 맞게 검증하기 전까지 현장 기능으로 단정하지 않는다.

## Android v1 후보

| 후보 | 내용 | 장점 | 리스크 | 1차 판정 |
| --- | --- | --- | --- | --- |
| USB-OTG direct | Android 태블릿/폰이 CSM USB CDC에 직접 연결 | 현재 protocol 재사용, 지연/증거 경로 단순 | Android Qt SerialPort/USB 권한, 기기별 OTG 안정성 확인 필요 | feasibility spike 필요 |
| CSM Wi-Fi/BLE remote stream | CSM이 typed stream을 Wi-Fi/BLE/TCP 등으로 직접 송수신 | PC 없이 태블릿 단독 운용 가능 | 펌웨어 transport 추가, 보안/재연결/대역폭/지연/제어 safety 재검증 필요 | 별도 CSM spike 필요 |
| Windows VSM relay | Windows VSM이 CSM USB를 유지하고 Android는 네트워크 뷰어/리모컨 | 기존 field RC 보존, Android 구현 위험 낮음 | PC 의존, 제어 권한/동시성 정책 필요 | 가장 낮은 초기 위험 |

## 최소 확인 항목

- Android target Qt kit: Qt 6 Android arm64-v8a, NDK 버전 일치.
- UI 재사용성: QML pages를 그대로 쓰기보다 tablet/phone density profile을 별도 설계한다.
- Transport boundary: `TypedTransportParser`, `TypedRecords`, `TypedReplayReader`, model pack parser는 공유 후보로 유지한다.
- Platform-specific transport: Windows `QSerialPort` path와 Android USB/network path를 `TransportRuntime` 아래에서 교체 가능해야 한다.
- Control safety: 어떤 Android 경로도 `CONTROL_ACK`를 actual success로 표시하면 안 된다. actual success는 matching `CAN_TX_RAW`만 허용한다.

## 다음 결정 기준

1. CSM 무선 spike가 가능하면 Wi-Fi TCP typed stream throughput/latency를 측정한다.
2. Android USB-OTG spike가 가능하면 CAPABILITY 수신, BOARD_HEALTH freshness, 10분 logging, replay open을 먼저 본다.
3. 둘 다 불확실하면 Windows VSM relay를 Android v1으로 잡고, Android는 viewer/reporting부터 시작한다.

## 이번 Field RC에서 하지 않는 것

- Android APK 생성.
- CSM Wi-Fi/BLE 펌웨어 구현.
- Android에서 control TX 허용.
- 무선 송수신 가능하다는 성공 선언.
