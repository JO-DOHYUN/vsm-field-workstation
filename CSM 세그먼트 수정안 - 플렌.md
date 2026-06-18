# CSM CDC Backpressure / TX Ring Clear 리팩토링 계획

## Summary
- `CSM 세그먼트 수정안.md` 전체를 UTF-8로 읽고, root/board brief, `shared/docs/TRANSPORT_AND_RECORDS_KO.md`, `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md`, 현재 코드를 대조했다.
- 실제 실패 경로는 문서 판단과 일치한다: [src/main.cpp](C:/Users/JEON0295/Documents/PlatformIO/Projects/J_ArdP7_AM2_CSM/src/main.cpp:617)에서 `_SerialUSB.send_nb()`가 `actual == 0`을 반환하고 250ms 지속 시 TX ring 전체를 clear하며 code 25를 낸다. 이는 connected backpressure를 truth loss로 바꾸는 구조 결함이다.
- 1차 목표는 Bulk 전환 없이 CDC 경로를 유지하되, `CAN_RX_SEGMENT -> priority-aware uplink -> CDC drain`을 loss-accounted 구조로 재정의하는 것이다. 새 record type은 만들지 않는다.

## Key Changes
- 현재 작업 루트는 AGENTS/BRIEF 기준인 `C:/Users/JEON0295/Documents/PlatformIO/Projects/J_ArdP7_AM2_CSM`로 고정한다. 수정안 안의 `C:\WORKS\VS\csm_zip_pre_wifi`는 과거 작업공간 힌트로만 취급한다.
- [include/protocol/TypedRecords.h](C:/Users/JEON0295/Documents/PlatformIO/Projects/J_ArdP7_AM2_CSM/include/protocol/TypedRecords.h:48)의 `kCanRxSegmentMaxFrames`를 `16 -> 15`로 낮춘다. 계산은 `32 + 15*30 = 482B payload`, typed frame 전체 `482 + 11 = 493B`로 USB HS 512B packet 경계 안에 둔다.
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`를 같이 갱신한다: production max segment frame count 15, full frame 493B, code 24/25 의미, `BOARD_HEALTH v4` offset `188..191`의 `can_segment_enqueue_fail_total` 사용을 문서화한다.
- `src/main.cpp`에 남아 있는 TX ring, CDC drain, segment builder, priority 판단, health counter 조립을 작은 모듈로 분리한다. `main.cpp`는 setup/loop orchestration과 하드웨어 연결만 남긴다.

## Implementation Changes
- `protocol/TypedFrame`에 `encode_typed_frame(...)` API를 추가한다. 기존 wire format은 그대로 유지하고, CDC byte ring용 frame serialization 중복을 제거한다.
- 새 uplink 모듈을 만든다:
  - `SerialTxScheduler`: 64KB byte ring, `_SerialUSB.send_nb()`, requested/actual/zero-count/high-water/backpressure episode stats, disconnect stale clear만 담당.
  - `UplinkScheduler`: record를 byte ring에 넣기 전 priority admission을 판단하고 typed frame으로 serialize한다.
  - `UplinkPriorityPolicy`: `CriticalTruth`, `PeriodicHealth`, `Diagnostic` 등급을 정의한다.
  - `CanRxSegmentBuilder`: pending segment, count/byte/time flush, `segment_seq64`, `capture_seq64`, enqueue 실패 카운터를 담당한다.
  - `HealthCounters`: serial/can/drop/backpressure counter를 한곳에 모아 `BOARD_HEALTH` 조립부가 직접 전역 변수를 긁지 않게 한다.
- connected 상태의 CDC backpressure에서는 `clear_serial_tx_ring()`을 절대 호출하지 않는다. `EventSerialTxRingClear(code=25)`는 실제 stale clear가 발생한 경우에만 유지한다.
- `EventSerialTxBackpressure(code=24)`는 backpressure episode 진입/지속 diagnostic으로만 rate-limit 발행한다. code 25를 backpressure 진입 의미로 재정의하지 않는다.
- priority 정책은 byte ring에 들어가기 전에 적용한다. 이미 byte ring에 들어간 frame을 뒤늦게 골라 버리는 방식은 금지한다.
- 우선순위 기본값:
  - 최상위: `CAN_RX_SEGMENT`, `CONTROL_ACK`, `CAN_TX_RAW`, safety/fault transition, explicit loss accounting.
  - 중간: `BOARD_HEALTH`, `CAPABILITY`, heartbeat/session visibility.
  - 하위: repeated MCP status/error spam, profiler/debug/log성 event.
- backpressure 중에는 하위 record부터 suppress/rate-limit한다. CAN_RX truth가 결국 enqueue되지 못하면 `can_segment_enqueue_fail_total`, `serial_tx_enqueue_fail_total`, capture_seq gap, typed seq gap으로 드러나게 한다.
- `BOARD_SERIAL_TX_CHUNK_BYTES` 기본값은 128로 유지하고, production env를 깨지 않으면서 256/512 비교용 derived env를 추가한다. 512가 정답이라고 가정하지 않고 counters로 판단한다.
- serial drain에는 byte budget뿐 아니라 time budget을 둔다. 기본값은 `BOARD_SERIAL_TX_DRAIN_TIME_BUDGET_US=1000`으로 하고, CAN RX pump starvation을 막는다.

## Additional Fixed Requirements
- connected 상태에서는 어떤 경우에도 `txRing.clear()` 금지.
- typed frame은 atomic enqueue 해야 한다. typed frame은 byte ring에 전부 들어가거나 아예 안 들어가야 한다.
- 금지:
  - typed frame 앞부분 enqueue
  - 뒤쪽 enqueue 실패
- VSM parser 보호 정책:
  - encode 전 전체 frame 길이 계산
  - byte ring free space 확인
  - 충분하면 전체 enqueue
  - 부족하면 enqueue하지 않음
  - fail counter 증가
  - partial typed frame enqueue 금지
- critical reserve 공간을 둔다.
  - TX ring 64KB
  - normal limit: 56KB
  - critical reserve: 8KB
  - 하위 record는 normal limit까지만 사용한다.
- 하위 record:
  - debug
  - profiler
  - repeated MCP status/error
  - verbose log
- critical reserve를 사용할 수 있는 record:
  - CAN_RX_SEGMENT
  - CONTROL_ACK
  - CAN_TX_RAW
  - fault transition
  - safety evidence
  - loss accounting
- `capture_seq64`, `segment_seq64`, `typed_seq` 의미를 고정한다.
  - `capture_seq64`: CAN frame 수신 단위 순번, CAN frame 1개마다 증가
  - `segment_seq64`: CAN_RX_SEGMENT 생성 단위 순번, segment 1개마다 증가
  - `typed_seq`: typed record 송출 단위 순번, 모든 typed frame마다 증가
- gap 의미를 고정한다.
  - capture_seq gap → CAN 수신 후 segment 구성 전후의 누락
  - segment_seq gap → CAN_RX_SEGMENT record 단위 누락
  - typed_seq gap → typed stream 전송/파싱 단계 누락
- priority admission은 byte ring 진입 전에 적용한다.
  - 좋은 방식: record 생성 요청 → priority 판단 → enqueue 가능 여부 판단 → typed frame encode → atomic enqueue
  - 나쁜 방식: 일단 byte ring에 넣음 → 나중에 뒤져서 버림
- 이미 byte ring에 들어간 typed frame을 나중에 골라 버리면 frame 경계와 parser 안정성이 위험하므로 금지한다.

## Test Plan
- Static checks:
  - connected backpressure path에서 `clear_serial_tx_ring()` 호출이 없는지 `rg`로 확인.
  - `EventSerialTxRingClear`가 disconnect/reconnect stale clear 경로에만 남는지 확인.
  - `kCanRxSegmentMaxFrames == 15` 및 full frame `<=512B` static_assert 확인.
- Builds:
  - `platformio run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm`
  - `platformio run -e portenta_h7_m7_dual_can_basic`
  - 비교용으로 chunk 256/512 derived env도 빌드한다.
- Tooling:
  - `pc_tools/verify_typed_stream.py`에 typed seq gap, `CAN_RX_SEGMENT` capture_seq gap, final counter summary를 추가한다.
- HIL 가능 시:
  - 1000fps+1000fps 또는 가능한 최고 부하로 `CAN_RX_SEGMENT` 유입.
  - VSM/read-discard 수신 상태에서 code 25 재발 여부 확인.
  - 확인 counters: `serial_tx_ring_clear_total`, `serial_tx_backpressure_total`, `serial_tx_ring_cleared_bytes_total`, `serial_tx_high_water_bytes`, `serial_tx_enqueue_fail_total`, `can_segment_enqueue_fail_total`, `can_rx_dropped_total`, `can_fifo_overflow_total`, capture_seq gap, typed seq gap.
- HIL 불가 시 완료 보고에 “실기 미검증”을 명확히 쓰고, 위 counters를 다음 실기 테스트 관찰 항목으로 남긴다.

## Acceptance Criteria
- connected CDC backpressure에서 TX ring clear가 발생하지 않는다.
- 고부하 상태에서 `EventSerialTxRingClear(code=25)`가 재발하지 않는다.
- `EventSerialTxBackpressure(code=24)`는 rate-limited diagnostic으로만 남는다.
- `CAN_RX_SEGMENT` 전체 typed frame이 512B 이하로 유지된다.
- debug/profiler/repeated MCP event가 `CAN_RX_SEGMENT`, `CONTROL_ACK`, `CAN_TX_RAW`, fault evidence를 밀어내지 않는다.
- 모든 손실은 health counter, event, capture_seq gap, typed seq gap 중 하나로 관측 가능하다.
- record type ID와 live typed transport v1은 변경하지 않는다.

## Assumptions
- `CSM 세그먼트 수정안.md`는 입력 문서로만 사용하고, 별도 요청 없이는 수정하지 않는다.
- Bulk USB 전환은 이번 범위가 아니다. CDC 유지 상태에서도 `actual == 0` 장기 지속 또는 CDC stack 한계가 재현될 때 2차로 검토한다.
- VSM/Qt 변경은 이 저장소에서 하지 않는다. 필요한 wire-contract 변경은 `shared/docs/TRANSPORT_AND_RECORDS_KO.md`에만 반영한다.
- 새 `BOARD_HEALTH v5`는 만들지 않는다. 이번 1차 수정은 기존 v4와 기존 event type 안에서 해결한다.
