# 데이터 채팅방 전달용 정리

이 프로젝트는 **프로그램 구조와 값 정확도 작업을 분리**한다.
프로그램 쪽은 이미 모델 팩 1파일 구조로 읽도록 정리했으니, 이 방에서는 **모델 팩 JSON 내용만 정확하게 만드는 것**에 집중하면 된다.

## 지금 데이터 방에 요청해야 할 것
1. `can-monitor-model-pack.v1` 형식을 기준으로 **차량별 모델 팩 JSON 1개**를 완성
2. rules와 messages를 따로 주지 말고 **한 파일에 같이** 넣기
3. 실차 로그/CAN DB 기반으로 아래를 맞추기
   - ID별 실제 주기/지터/누락 특성
   - TTL warn/err
   - 주기 편차 경계
   - scale / offset / signed / endian / bit position
   - range_text / operating_text / description
4. 결과물은 `vms_model_<차량명>.json` 형태로 주기

## 이 방에서 해주면 좋은 산출물
- 확정 / 의심 / 미확정 시그널 표
- 실차 기준으로 이상한 값 사례
- 주기 기준이 너무 타이트한 ID 목록
- model_version / notes에 남길 변경 요약

## 프로그램 쪽에서 이미 고정한 것
- 패킷 포맷과 수집부는 유지
- 모델 적용은 JSON 1파일 교체 방식
- 프로그램은 rules와 messages를 같은 파일에서 로드
- 추후 차량/로봇 변경 시 프로그램 수정 최소화가 목표
