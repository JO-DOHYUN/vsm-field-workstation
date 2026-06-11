# turn77_full 런타임 실패 실제 원인

## 결론
이번 turn77_full의 실제 실행 실패 원인은 `PreciseSlider.qml` 파일 누락이 아니라,
`qml/pages/ReplayPage.qml` 내부에서 `Components.PreciseSlider`를 사용하면서
`import "../components" as Components` 선언이 빠져 있던 것이다.

## 왜 이전 진단과 달랐는가
- turn77_full의 현재 `CMakeLists.txt`에는 이미 `qml/components/PreciseSlider.qml`가 포함되어 있다.
- 따라서 현재 최신본 기준에서는 CMake 등록 누락이 본원인이 아니다.
- 실행 로그의
  `Components.PreciseSlider - Components is neither a type nor a namespace`
  문구는 파일 누락보다 **alias import 누락**과 더 정확히 일치한다.

## 실제 증상 연결
- `Main.qml`이 `Pages.ReplayPage`를 로드
- `ReplayPage.qml` 172행 부근에서 `Components.PreciseSlider` 사용
- 하지만 페이지 상단에 `Components` import가 없음
- QML 엔진이 페이지 생성 실패
- `main.cpp`의 `objectCreationFailed -> exit(-1)` 연결 때문에 앱이 바로 종료
- 그래서 VS 실행이든 out/build/x64-Release 직접 실행이든 동일하게 실패

## 수정
- `qml/pages/ReplayPage.qml`
  - 상단에 `import "../components" as Components` 추가
