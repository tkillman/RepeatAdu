# RepeatAdu 프로젝트 지침 (Project Guidelines)

이 문서는 **RepeatAdu** 펌웨어 개발 시 AI 에이전트와 개발자가 준수해야 할 아키텍처, 하드웨어 사양, 통신 프로토콜 및 코딩 원칙을 정의합니다.

---

## 1. 프로젝트 개요 (Overview)
- **프로젝트명:** RepeatAdu (Keyboard Autoclicker Firmware)
- **연동 대상:** Flutter 컴패니언 앱 (`repeat_keyboard`, `C:\fun\repeat_keyboard`)
- **목적:** ESP32-C3 SuperMini와 SG90 서보모터를 이용하여 물리적 키보드 자판을 지정된 주기와 유지 시간으로 자동 타건(Autoclick)하는 임베디드 펌웨어 제작.
- **주요 제어 파라미터 (Flutter 앱과 동기화):**
  - 총 작동 시간 (`totalMinutes`, 기본 30분)
  - 타건 주기 (`intervalSeconds`, 기본 5.0초)
  - 키 누름 유지 시간 (`holdSeconds`, 기본 0.2초)
  - 서보 타건 각도 및 복귀 각도 (Press / Release Angle)

---

## 2. 하드웨어 사양 및 핀맵 (Hardware & Pinout)

### 2.1 메인보드: ESP32-C3 SuperMini (USB-C)
- **MCU:** ESP32-C3 (RISC-V 32-bit Single-Core, 최대 160MHz)
- **통신:** 2.4GHz Wi-Fi & BLE 5.0 내장, USB-C (Native USB CDC/JTAG)
- **동작 전압:** 3.3V Logic (보드 입력 VBUS는 USB 5V)

### 2.2 액추에이터: SG90 서보모터 (9g Micro Servo)
- **구동 전압:** 4.8V ~ 6.0V (ESP32-C3 SuperMini의 `5V/VBUS` 핀에 연결 권장)
- **제어 신호:** 50Hz PWM 신호 (주기 20ms, 펄스 폭 0.5ms ~ 2.5ms)

### 2.3 핀 맵 구성 (Pin Assignment)
| 부품 | 핀 이름 | ESP32-C3 SuperMini 핀 | 비고 |
| :--- | :--- | :--- | :--- |
| **SG90** | Signal (PWM) | **GPIO 3** (또는 GPIO 4) | 스트랩 핀(GPIO2, 8, 9)을 피한 안전한 PWM 핀 |
| **SG90** | VCC | **5V (VBUS)** | 3.3V 핀 직결 금지 (전류 부족 및 리셋 유발 방지) |
| **SG90** | GND | **GND** | 보드 공통 접지 |
| **LED** | 내장 LED | **GPIO 8** (보드마다 다를 수 있음) | 상태 표시용 (Idle / Active / BLE Connected) |

> [!WARNING]
> **전원 주의사항:** SG90 모터 구동 시 순간 피크 전류(500~800mA)로 인해 ESP32-C3에 전압 강하(Brownout)가 발생해 재부팅될 수 있습니다. 
> 5V 라인과 GND 사이에 전해 커패시터(100uF ~ 470uF)를 병렬 연결하는 것을 권장하며, 모터 동작 중에는 불필요한 과부하 토크를 피하도록 각도를 튜닝해야 합니다.

---

## 3. 통신 규격 (Communication Protocol)

### 3.1 BLE (Bluetooth Low Energy) 프로토콜 (우선순위 1)
Flutter 앱(`repeat_keyboard`의 `flutter_blue_plus`)과 무선 통신합니다.

- **디바이스 이름 (Advertising Name):** `RepeatAdu_Clicker`
- **Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b` (Custom Autoclicker Service)
- **Characteristic UUIDs:**
  1. **Command & Config Char (Write / Read):** `beb5483e-36e1-4688-b7f5-ea07361b26a8`
     - Flutter 앱에서 설정 및 제어 명령 전송
  2. **Status & Telemetry Char (Notify / Read):** `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
     - 현재 상태, 진행률, 실행 횟수(`executedCount`), 잔여 시간(`remainingSeconds`) 송출

#### 패킷 포맷 (JSON 기반 권장)
- **Flutter -> ESP32-C3 (Command):**
  ```json
  {"cmd": "START", "totalMin": 30.0, "intervalSec": 5.0, "holdSec": 0.2, "pressAngle": 45, "releaseAngle": 0}
  {"cmd": "STOP"}
  {"cmd": "CALIBRATE", "angle": 45}
  ```
- **ESP32-C3 -> Flutter (Status Notify):**
  ```json
  {"state": "RUNNING", "remainingSec": 1785, "executedCount": 3, "elapsedSec": 15}
  {"state": "IDLE"}
  ```

#### 3.1.1 연결 해제 및 재연결 동기화 (Disconnection & Re-sync Policy)
- **자율 동작 유지:** Flutter 앱에서 `START` 명령 수신 후, 스마트폰 절전(화면 꺼짐), 백그라운드 전환, 통신 거리 초과 등으로 **BLE 연결이 끊어지더라도 ESP32-C3는 멈추지 않고 지정된 시간(`totalMin`) 동안 독립적으로 오토클릭 동작을 계속 수행**합니다.
- **연결 끊김 시 즉시 재광고(Advertising):** 연결이 해제되면 동작을 멈추지 않은 상태에서 즉시 BLE Advertising을 재개하여, 언제든지 앱이 재연결할 수 있도록 대기합니다.
- **재연결 시 상태 동기화:** 앱이 다시 연결되면 현재의 동작 상태, 남은 시간(`remainingSec`), 실행 횟수(`executedCount`)를 즉시 Notify하여 앱 UI를 실제 진행 상태에 맞게 동기화합니다.

### 3.2 USB-C 시리얼 CLI (보조 / 디버깅용)
- **Baudrate:** 115200 bps
- USB 케이블 연결 시 시리얼 모니터로 간단한 텍스트 명령 입력 가능:
  - `START 30 5.0 0.2` : 30분 동안 5초 주기, 0.2초 누름 시작
  - `STOP` : 즉시 중지 및 원위치 복귀
  - `TEST` : 1회 테스트 타건

---

## 4. 소프트웨어 아키텍처 및 코딩 원칙 (Software Architecture)

### 4.1 개발 환경 (Toolchain)
- **PlatformIO** 또는 **Arduino IDE (ESP32 Arduino Core 2.0.x / 3.0.x)**
- 플랫폼: `espressif32`, 보드: `esp32-c3-devkitm-1` (또는 SuperMini 매핑)

### 4.2 필수 라이브러리
- `ESP32Servo` (ESP32-C3 LEDC 하드웨어 타이머 기반 서보 제어)
- `NimBLE-Arduino` (권장: 기본 ESP32 BLE 스택 대비 RAM 50% 이상 절약, 연결 안정성 우수) 또는 `BLEDevice.h` (표준 스택)
- `ArduinoJson` (JSON 파싱 및 직렬화)

### 4.3 핵심 동작 원칙 (Core Rules)
1. **논블로킹(Non-blocking) 타이머 필수:**
   - 서보 대기 및 주기 제어에 `delay()`를 절대 길게 사용하지 말 것 (`millis()` 기반 상태 머신 구현).
   - `delay()` 사용 시 BLE 통신이 끊기거나 워치독 타이머(WDT) 리셋이 발생할 수 있음.
2. **상태 머신 (FSM) 구조:**
   - `STATE_IDLE` : 대기 상태 (모터는 Release 각도 유지 또는 detach)
   - `STATE_WAIT_INTERVAL` : 다음 타건까지 대기
   - `STATE_PRESSING` : 서보가 Press 각도로 이동하여 키를 누르고 있는 상태 (`holdSeconds` 동안 유지)
   - `STATE_RELEASING` : Release 각도로 복귀 후 다음 인터벌 대기
   - `STATE_STOPPED` : 시간 만료 또는 사용자 중지로 정지
3. **모터 보호 (Jitter & Overheat Prevention):**
   - 타건이 없는 긴 인터벌 대기 중에는 서보 신호를 `detach()` 하거나 안정 각도로 고정하여 모터 지터(떨림)와 코일 발열, 기어 마모를 방지.
4. **연결 단절 시 자율 동작 유지 (Autonomous on Disconnect) & 재연결 지원:**
   - **연결 끊김 무관 지속 동작:** BLE 연결이 끊어지더라도 오토클릭 사이클은 중단되지 않고 `totalMinutes` 만료 시까지 독립적으로 계속 구동됩니다.
   - **백그라운드 재광고:** 연결 해제 즉시 BLE Advertising을 다시 시작하여, 사용자가 언제든 앱을 켜서 재연결하고 상태를 확인하거나 제어할 수 있도록 합니다.
   - **재연결 즉시 동기화:** 재연결 이벤트 발생 시 현재 `state`, `remainingSec`, `executedCount`를 즉시 Notify하여 앱 UI를 실제 진행 상태로 복구합니다.
   - **정지 조건:** 오직 ① 설정된 총 시간(`totalMinutes`) 경과 시 완료, ② 사용자가 앱(재연결 후) 또는 시리얼 CLI로 `STOP` 명령을 명시적으로 내렸을 때, ③ 하드웨어 전원 종료/리셋 시에만 정지합니다.
   - **비상 안전 각도:** 정지(`STATE_STOPPED` 또는 완료) 시에는 항상 서보를 `Release` 각도로 복귀시키고 모터 PWM을 `detach()`하여 하드웨어를 보호합니다.

---

## 5. 단계별 개발 로드맵 (Roadmap & Todo)

- [ ] **Step 1: 환경 설정 및 기본 서보 테스트**
  - PlatformIO / Arduino 프로젝트 환경 구축
  - ESP32-C3 SuperMini 핀아웃 검증 및 SG90 단독 각도 제어/타건 테스트
- [ ] **Step 2: 비차단식(FSM) 오토클리커 엔진 구현**
  - `millis()` 기반 인터벌/누름 유지 시간 상태 머신 작성
  - 시리얼 명령(`START`, `STOP`)을 통한 로직 검증
- [ ] **Step 3: BLE 서비스 및 Characteristic 구현**
  - BLE 서버 구동 및 Advertising
  - 설정 수신(Write) 및 상태 송출(Notify) 핸들러 연결
- [ ] **Step 4: Flutter `repeat_keyboard` 앱과 연동 검증**
  - Flutter 앱의 `ble_manager.dart`와 UUID 매칭 및 실시간 제어 테스트
  - 배터리/전원 안정성 확인 및 캘리브레이션
