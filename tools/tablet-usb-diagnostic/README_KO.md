# 태블릿 USB Device/Gadget 진단 APK

Android 9 산업용 태블릿에서 USB Host는 동작하지만 Windows 노트북에 연결했을 때 장치 열거가 발생하지 않는 문제를 판정하기 위한 읽기 전용 진단 앱입니다.

## 확인 항목

- 제조사, 모델, 보드, SoC, 커널, Android 빌드 정보
- USB Host/Accessory 기능 선언
- 현재 USB 상태 브로드캐스트와 MTP/PTP/ADB 구성
- `sys.usb.*`, `persist.sys.usb.*`, `vendor.usb.*` 관련 시스템 속성
- `/sys/class/udc`
- `/sys/class/usb_role`
- `/sys/class/dual_role_usb`
- `/sys/class/typec`
- `/sys/kernel/config/usb_gadget`
- `/config/usb_gadget`
- 현재 네트워크 인터페이스와 IP
- SELinux 및 root 흔적

## 사용

1. APK를 태블릿에 복사하여 설치합니다.
2. 노트북과 태블릿을 USB 케이블로 연결합니다.
3. 앱에서 `진단 실행`을 누릅니다.
4. 완료 후 `TXT 저장` 또는 `결과 공유`를 사용합니다.
5. 생성된 TXT를 ChatGPT 대화에 업로드합니다.

이 앱은 설정이나 USB 역할을 변경하지 않습니다. 일반 앱 권한으로 읽을 수 없는 항목은 `접근 불가`로 기록합니다.
