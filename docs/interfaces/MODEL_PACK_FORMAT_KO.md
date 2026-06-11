# 모델 팩 1파일 형식

이 프로젝트는 차량/로봇마다 **모델 팩 JSON 1개만 교체**하는 구조로 간다.

## 목적
- 프로그램 채팅방은 구조/성능/UI에 집중
- 데이터 채팅방은 실차 기준 값/주기/해석 정확도에 집중
- 둘 사이 접점을 **모델 팩 JSON 1개**로 고정

## 최상위 형식
```json
{
  "schema": "can-monitor-model-pack.v1",
  "model_key": "hym_hamt2_r13",
  "model_name": "HYM HAMT 2.0 R13",
  "model_version": "2026-03-22",
  "vendor": "HYM",
  "notes": "line2 baseline",
  "rules": [ ... ],
  "messages": [ ... ]
}
```

## rules
주기/TTL/주기 편차 기준을 담는다.

```json
{
  "id": "0x520",
  "name_en": "ADCU_BASIC_INFO3",
  "expected_period_ms": 20.0,
  "ttl_warn_ms": 60.0,
  "ttl_err_ms": 200.0,
  "period_err_warn_pct": 20.0,
  "period_err_err_pct": 50.0
}
```

## messages
값 해석에 필요한 시그널 정의를 담는다.

```json
{
  "id": "0x520",
  "name": "ADCU_BASIC_INFO3",
  "signals": [
    {
      "name": "Vehicle speed",
      "byte_index_1based": 1,
      "bit_text": "1-16",
      "length_bits": 16,
      "start_bit_lsb": 0,
      "bit_positions_lsb": [],
      "scale": 0.01,
      "offset": 0.0,
      "signed": false,
      "range_text": "0 to 30",
      "operating_text": "unit: 0.01 km/h",
      "description": "vehicle speed",
      "reserved": false
    }
  ]
}
```

## 프로그램이 기대하는 것
- `rules`와 `messages`는 **같은 ID 체계**를 사용
- `rules`만 있고 `messages`가 없어도 동작 가능
- `messages`만 있고 `rules`가 없어도 RAW/해석 일부 확인 가능
- 그러나 실제 목적성상 **둘 다 같이 채워진 모델 팩**이 기준

## 데이터 채팅방에서 수정할 것
- `expected_period_ms`, `ttl_warn_ms`, `ttl_err_ms`, `period_err_warn_pct`, `period_err_err_pct`
- `scale`, `offset`, `signed`, `start_bit_lsb`, `bit_positions_lsb`, `range_text`, `operating_text`
- `model_name`, `model_key`, `model_version`, `vendor`, `notes`

## 데이터 채팅방에서 건드리지 말 것
- 20바이트 패킷 포맷
- 앱 구조/QML/C++ 클래스 이름
- 로그/재생 포맷


## 추가 권장 필드 (v1 호환 확장)
### rule 단위
- `timing_enabled`: `true/false`
  주기/TTL 판정을 모델에서 완전히 끄고 관찰 전용으로 둘 수 있다.
- `timing_mode`: `"model" | "monitor"`
  `monitor`면 주기 탭은 gap/age를 계속 보여주되 WARN/ERR 판단은 하지 않는다.

### signal 단위
- `unit`: 표시 단위 문자열
- `alarm_mode`: `"auto" | "none" | "range" | "flag" | "enum" | "reserved"`
- `warn_min`, `warn_max`, `err_min`, `err_max`: 수치 경계
- `inactive_raw_values`: 비트/상태형 신호에서 정상으로 보는 raw 값 배열
- `inactive_labels`: enum label 기준 정상 상태 배열
- `alarm_severity`: `WARN` 또는 `ERR`
- `alarm_message`: 화면/경보에서 우선 사용할 문구
- `monitor_only`: 표시만 하고 값 경보는 만들지 않음

핵심은 **모델 교체 시 오차/경보 철학도 JSON 하나로 같이 바뀌게 하는 것**이다.
프로그램은 이 JSON의 주기/값/경보 정의를 우선 사용한다. 값 경보는 명시 필드가 없는 신호를 관찰 위주로 두는 방향이 기본이다.

## 시간 기준 철학
- live는 **첫 수신 시각 + 보드 t_us**를 합쳐 절대 시각으로 표시
- replay는 **log meta 의 `created_local` + 프레임 상대시간**으로 절대 시각 복원
- seek/정지/재생 전환 시에도 age/gap/alarm time은 **현재 분석 소스의 시계**를 따른다
