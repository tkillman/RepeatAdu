# RepeatAdu (Keyboard Autoclicker Firmware)

**ESP32-C3 SuperMini**와 **SG90 서보모터**를 이용한 물리 키보드 오토클리커 펌웨어입니다.  
Flutter 컴패니언 앱(`repeat_keyboard`)과 BLE를 통해 무선으로 연동되며, **블루투스 연결이 끊어져도 독립적으로 지정된 시간 동안 계속 타건 작업을 수행**합니다.

---

## 🛠️ 하드웨어 배선도 (Wiring)

| 부품 | 핀 이름 | 선 색상 | ESP32-C3 SuperMini 핀 | 주의사항 |
| :--- | :--- | :--- | :--- | :--- |
| **SG90 Servo** | Signal (PWM) | 주황색 (Orange / Yellow) | **GPIO 3** | PWM 제어 핀 |
| **SG90 Servo** | VCC | 붉은색 (Red) | **5V (VBUS)** | **3.3V 핀에 연결하지 마세요.** |
| **SG90 Servo** | GND | 갈색/검은색 (Brown / Black) | **GND** | 공통 접지 |

> [!TIP]
> 서보모터 기동 시 전류 부족으로 보드가 재부팅(Brownout)되는 현상이 있다면, 서보 VCC(5V)와 GND 사이에 전해 커패시터(100uF ~ 470uF)를 병렬로 연결해 주시면 안정적입니다.

---

## 🚀 빌드 및 업로드 방법

### 방법 1. PlatformIO (추천)
1. VS Code에서 `RepeatAdu` 폴더를 엽니다.
2. ESP32-C3 SuperMini를 USB-C 케이블로 PC에 연결합니다.
3. 좌측 하단 PlatformIO 아이콘에서 **Upload**를 클릭합니다. (라이브러리 자동 설치됨)

### 방법 2. Arduino IDE
1. Arduino IDE 라이브러리 관리자에서 다음 라이브러리를 설치합니다:
   - `ESP32Servo` (by Kevin Harrington)
   - `ArduinoJson` (by Benoit Blanchon, v6 또는 v7)
2. 보드 선택: **ESP32C3 Dev Module**
3. 설정 옵션:
   - **USB CDC On Boot:** `Enabled` (필수: 시리얼 모니터 출력용)
   - **Flash Size:** `4MB`
4. `RepeatAdu.ino`를 열고 **업로드**합니다.

---

## 📡 BLE 통신 규격

- **디바이스 이름:** `RepeatAdu_Clicker`
- **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Command Characteristic (Write):** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **Status Characteristic (Notify):** `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`

### 특징: 자율 백그라운드 동작
- Flutter 앱에서 `START` 전송 후 스마트폰이 절전 모드로 진입하거나 BLE 연결이 끊어져도, **ESP32-C3는 멈추지 않고 설정된 시간(`totalMinutes`) 동안 작업을 끝까지 유지**합니다.
- 백그라운드에서 상시 재광고(Re-advertising)를 수행하므로 사용자가 앱을 다시 열면 즉시 재연결되어 진행 상황이 실시간 동기화됩니다.

### 랜덤 반복 주기

앱은 `START` 명령에 `intervalMinSec`, `intervalMaxSec`를 보낼 수 있습니다. 기기는 첫 타건을 즉시 수행한 뒤, 매 타건마다 이 범위에서 다음 전체 주기를 새로 선택합니다.

```json
{"cmd":"START","totalMin":30,"intervalMinSec":5,"intervalMaxSec":10,"holdSec":0.2}
```

기존 앱이 보내는 `intervalSec`만 있는 명령과 USB-C 시리얼의 `START 30 5.0 0.2` 형식은 계속 지원하며, 이 경우 최소·최대가 같은 고정 주기로 동작합니다.

---

## 💻 USB-C 시리얼 CLI 테스트

시리얼 모니터(115200 Baud)를 열고 다음 명령어를 텍스트로 바로 입력할 수 있습니다:
- `START 30 5.0 0.2` : 30분 동안 5초 주기, 0.2초 누름 시작
- `STOP` : 즉시 중지 및 서보 원위치 복귀
- `TEST` : 1회 단독 타건 테스트
- `STATUS` : 현재 상태 JSON 출력
- `HELP` : 명령어 도움말
