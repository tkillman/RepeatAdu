#include <Arduino.h>
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ArduinoJson.h>

// ==========================================
// 1. 하드웨어 핀 및 상수 정의
// ==========================================
#define SERVO_PIN 3 // SG90 PWM 제어 핀 (ESP32-C3 SuperMini GPIO3)
#define LED_PIN 8   // ESP32-C3 SuperMini 온보드 LED (GPIO8, Active-Low인 경우가 많음)

// BLE UUID 정의 (repeat_keyboard Flutter 앱과 일치)
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_CMD "beb5483e-36e1-4688-b7f5-ea07361b26a8"    // Write / Read
#define CHARACTERISTIC_UUID_STATUS "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e" // Notify / Read

#define DEVICE_NAME "RepeatAdu_Clicker"

// ==========================================
// 2. 상태 머신(FSM) 및 데이터 구조체
// ==========================================
enum ClickerState
{
  STATE_IDLE,          // 대기 상태
  STATE_WAIT_INTERVAL, // 다음 타건 주기까지 대기
  STATE_PRESSING,      // 서보가 키를 누르고 있는 상태 (holdSec 동안 유지)
  STATE_RELEASING,     // 서보 복귀 동작 대기 (물리적 이동 시간 확보)
  STATE_COMPLETED      // 총 동작 시간 만료로 완료됨
};

struct ClickerConfig
{
  float totalMinutes = 30.0f;   // 총 동작 시간 (분)
  float intervalSeconds = 5.0f; // 타건 간격 (초)
  float holdSeconds = 0.2f;     // 키 누름 유지 시간 (초)
  int pressAngle = 45;          // 타건(누름) 각도 (도)
  int releaseAngle = 0;         // 대기(복귀) 각도 (도)
};

// ==========================================
// 3. 전역 인스턴스 및 런타임 변수
// ==========================================
Servo keyServo;
BLEServer *pServer = nullptr;
BLECharacteristic *pStatusCharacteristic = nullptr;
BLECharacteristic *pCmdCharacteristic = nullptr;

bool bleConnected = false;
bool oldBleConnected = false;

ClickerState currentState = STATE_IDLE;
ClickerConfig config;

unsigned long totalStartTimeMs = 0; // 시작 시점 (ms)
unsigned long totalDurationMs = 0;  // 총 지속 시간 (ms)
unsigned long stateTimerMs = 0;     // 현재 상태 진입 시점 (ms)
unsigned long lastTelemetryMs = 0;  // 마지막 BLE/시리얼 알림 전송 시점
uint32_t executedCount = 0;         // 타건 실행 횟수

// ==========================================
// 4. 서보 제어 유틸리티 (지터 및 발열 방지)
// ==========================================
void setServoAngle(int angle, bool keepAttached = false)
{
  if (!keyServo.attached())
  {
    keyServo.attach(SERVO_PIN, 500, 2400); // 50Hz, 500us ~ 2400us (SG90 표준 범위)
  }
  keyServo.write(constrain(angle, 0, 180));

  if (!keepAttached)
  {
    // 릴리즈 위치 복귀 후 모터 지터 및 발열 방지를 위해 detach는 FSM에서 타이밍을 두고 처리
  }
}

void releaseServoToRest()
{
  setServoAngle(config.releaseAngle, true);
  delay(150); // 물리적 회전 시간 보장
  if (keyServo.attached())
  {
    keyServo.detach(); // 신호 차단으로 모터 떨림 및 코일 과열 차단
  }
}

// LED 상태 제어 (SuperMini GPIO8)
void updateLed()
{
  static unsigned long lastBlinkMs = 0;
  static bool ledState = false;

  if (currentState == STATE_PRESSING)
  {
    digitalWrite(LED_PIN, LOW); // 누르는 중 (LED 켜짐)
  }
  else if (currentState == STATE_WAIT_INTERVAL || currentState == STATE_RELEASING)
  {
    // 작동 중: 500ms 주기로 점멸
    if (millis() - lastBlinkMs >= 500)
    {
      lastBlinkMs = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }
  }
  else
  {
    // IDLE: BLE 연결 시 켜짐, 미연결 시 꺼짐
    digitalWrite(LED_PIN, bleConnected ? LOW : HIGH);
  }
}

// ==========================================
// 5. 텔레메트리 전송 함수
// ==========================================
const char *getStateString(ClickerState state)
{
  switch (state)
  {
  case STATE_IDLE:
    return "IDLE";
  case STATE_WAIT_INTERVAL:
    return "RUNNING";
  case STATE_PRESSING:
    return "PRESSING";
  case STATE_RELEASING:
    return "RUNNING";
  case STATE_COMPLETED:
    return "COMPLETED";
  default:
    return "UNKNOWN";
  }
}

void sendTelemetry(bool force = false)
{
  unsigned long now = millis();
  if (!force && (now - lastTelemetryMs < 1000))
  {
    return; // 주기적 전송은 1초에 한 번만
  }
  lastTelemetryMs = now;

  int remainingSec = 0;
  int elapsedSec = 0;

  if (currentState != STATE_IDLE && currentState != STATE_COMPLETED)
  {
    unsigned long elapsedMs = now - totalStartTimeMs;
    if (elapsedMs < totalDurationMs)
    {
      remainingSec = (totalDurationMs - elapsedMs) / 1000;
      elapsedSec = elapsedMs / 1000;
    }
    else
    {
      remainingSec = 0;
      elapsedSec = totalDurationMs / 1000;
    }
  }

  // JSON 생성 (ArduinoJson v7 JsonDocument)
  JsonDocument doc;
  doc["state"] = getStateString(currentState);
  doc["remainingSec"] = remainingSec;
  doc["executedCount"] = executedCount;
  doc["elapsedSec"] = elapsedSec;
  doc["bleConnected"] = bleConnected;

  String jsonString;
  serializeJson(doc, jsonString);

  // 시리얼 출력
  // Serial.printf("[TELEMETRY] %s\n", jsonString.c_str());

  // BLE Notify 전송 (연결되어 있을 때만 전송)
  if (bleConnected && pStatusCharacteristic != nullptr)
  {
    pStatusCharacteristic->setValue(jsonString.c_str());
    pStatusCharacteristic->notify();
  }
}

// ==========================================
// 6. 오토클리커 제어 함수 (시작 / 정지)
// ==========================================
void startAutoclicker(float totalMin, float intervalSec, float holdSec, int pressAng = 45, int relAng = 0)
{
  config.totalMinutes = (totalMin > 0) ? totalMin : 30.0f;
  config.intervalSeconds = (intervalSec > 0.1f) ? intervalSec : 5.0f;
  config.holdSeconds = (holdSec > 0.05f) ? holdSec : 0.2f;
  config.pressAngle = constrain(pressAng, 0, 180);
  config.releaseAngle = constrain(relAng, 0, 180);

  totalDurationMs = (unsigned long)(config.totalMinutes * 60.0f * 1000.0f);
  totalStartTimeMs = millis();
  executedCount = 0;

  Serial.printf("\n>>> Autoclicker Started! Total: %.1fm, Interval: %.2fs, Hold: %.2fs, Angle: %d -> %d\n",
                config.totalMinutes, config.intervalSeconds, config.holdSeconds,
                config.releaseAngle, config.pressAngle);

  // 즉시 첫 타건 시작
  setServoAngle(config.pressAngle, true);
  stateTimerMs = millis();
  currentState = STATE_PRESSING;
  executedCount++;
  Serial.printf("\n👉 [CLICK #%lu] 키 누름! (각도: %d도, 유지 시간: %.2f초)\n", 
                (unsigned long)executedCount, config.pressAngle, config.holdSeconds);

  sendTelemetry(true);
}

void stopAutoclicker(const char *reason = "User Request")
{
  Serial.printf("\n⏹️ [STOP] 오토클리커 정지: %s (총 실행 횟수: %lu회)\n", reason, (unsigned long)executedCount);
  releaseServoToRest();
  currentState = STATE_IDLE;
  sendTelemetry(true);
}

// ==========================================
// 7. 명령 처리기 (JSON 파서)
// ==========================================
void processCommand(const String &cmdStr)
{
  Serial.printf("[CMD RECV] %s\n", cmdStr.c_str());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, cmdStr);

  if (err)
  {
    Serial.printf("[CMD ERROR] JSON parse failed: %s\n", err.c_str());
    return;
  }

  const char *cmd = doc["cmd"] | "";

  if (strcasecmp(cmd, "START") == 0)
  {
    float totalMin = doc["totalMin"] | doc["totalMinutes"] | config.totalMinutes;
    float intervalSec = doc["intervalSec"] | doc["intervalSeconds"] | config.intervalSeconds;
    float holdSec = doc["holdSec"] | doc["holdSeconds"] | config.holdSeconds;
    int pressAngle = doc["pressAngle"] | config.pressAngle;
    int releaseAngle = doc["releaseAngle"] | config.releaseAngle;

    startAutoclicker(totalMin, intervalSec, holdSec, pressAngle, releaseAngle);
  }
  else if (strcasecmp(cmd, "STOP") == 0)
  {
    stopAutoclicker("Remote STOP Command");
  }
  else if (strcasecmp(cmd, "CALIBRATE") == 0)
  {
    int angle = doc["angle"] | config.pressAngle;
    Serial.printf("[CALIBRATE] Move to angle: %d\n", angle);
    setServoAngle(angle, true);
    delay(400);
    releaseServoToRest();
  }
  else if (strcasecmp(cmd, "STATUS") == 0)
  {
    sendTelemetry(true);
  }
}

// ==========================================
// 8. BLE 콜백 정의
// ==========================================
class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServerInstance) override
  {
    bleConnected = true;
    Serial.println("\n[BLE] >>> Client Connected! <<<");
    // 재연결 즉시 현재 상태 텔레메트리 전송 (동기화)
    sendTelemetry(true);
  }

  void onDisconnect(BLEServer *pServerInstance) override
  {
    bleConnected = false;
    Serial.println("\n[BLE] >>> Client Disconnected! <<<");
    Serial.println("[BLE] NOTE: Autoclicker operation continues autonomously in background!");

    // 중요: 연결이 끊어져도 백그라운드 재광고 시작 -> 언제든 재연결 가능!
    pServerInstance->startAdvertising();
    Serial.println("[BLE] Advertising restarted.");
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic) override
  {
    String rxValue = pCharacteristic->getValue().c_str();
    Serial.printf("\n[BLE RX RAW] %s (len: %d)\n", rxValue.c_str(), rxValue.length());
    if (rxValue.length() > 0)
    {
      processCommand(rxValue);
    }
  }
};

// ==========================================
// 9. BLE 초기화
// ==========================================
void initBLE()
{
  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 1. 명령 수신 Characteristic (Write / Read / Write_NR)
  pCmdCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_CMD,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  pCmdCharacteristic->setCallbacks(new CommandCallbacks());

  // 2. 상태 알림 Characteristic (Read / Notify)
  // ESP32 Arduino 3.x에서는 PROPERTY_NOTIFY 시 0x2902 디스크립터가 자동 추가됩니다.
  pStatusCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_STATUS,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY);

  pService->start();

  // Advertising 설정
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // iPhone 연결 도움
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] BLE Service Started. Advertising as: " DEVICE_NAME);
}

// ==========================================
// 10. USB-C 시리얼 CLI 처리 (디버깅 편의 기능)
// ==========================================
void processSerialCli()
{
  if (!Serial.available())
    return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0)
    return;

  Serial.printf("[SERIAL IN] %s\n", line.c_str());

  if (line.startsWith("{"))
  {
    processCommand(line);
    return;
  }

  if (line.equalsIgnoreCase("HELP"))
  {
    Serial.println("\n--- [RepeatAdu Serial CLI Help] ---");
    Serial.println("  START [totalMin] [intervalSec] [holdSec]");
    Serial.println("  STOP");
    Serial.println("  TEST");
    Serial.println("  STATUS");
    Serial.println("------------------------------------");
  }
  else if (line.startsWith("START") || line.startsWith("start"))
  {
    float t = config.totalMinutes, i = config.intervalSeconds, h = config.holdSeconds;
    int firstSpace = line.indexOf(' ');
    if (firstSpace > 0)
    {
      String params = line.substring(firstSpace + 1);
      int p1 = params.indexOf(' ');
      if (p1 > 0)
      {
        t = params.substring(0, p1).toFloat();
        int p2 = params.indexOf(' ', p1 + 1);
        if (p2 > 0)
        {
          i = params.substring(p1 + 1, p2).toFloat();
          h = params.substring(p2 + 1).toFloat();
        }
        else
        {
          i = params.substring(p1 + 1).toFloat();
        }
      }
      else
      {
        t = params.toFloat();
      }
    }
    startAutoclicker(t, i, h, config.pressAngle, config.releaseAngle);
  }
  else if (line.equalsIgnoreCase("STOP"))
  {
    stopAutoclicker("Serial CLI STOP");
  }
  else if (line.equalsIgnoreCase("TEST"))
  {
    Serial.println("Testing single keystroke click...");
    setServoAngle(config.pressAngle, true);
    delay(200);
    releaseServoToRest();
    Serial.println("Test complete.");
  }
  else if (line.equalsIgnoreCase("STATUS"))
  {
    sendTelemetry(true);
  }
}

// ==========================================
// 11. 메인 FSM 루프 (논블로킹 제어)
// ==========================================
void loopFSM()
{
  unsigned long now = millis();

  // 총 구동 시간 체크 (진행 중일 때)
  if (currentState != STATE_IDLE && currentState != STATE_COMPLETED)
  {
    if (now - totalStartTimeMs >= totalDurationMs)
    {
      Serial.printf("\n🏁 [COMPLETE] 총 작동 시간 만료! 총 %lu회 타건 완료.\n", (unsigned long)executedCount);
      releaseServoToRest();
      currentState = STATE_COMPLETED;
      sendTelemetry(true);
      return;
    }
  }

  // 상태 전이 로직
  switch (currentState)
  {
  case STATE_IDLE:
  case STATE_COMPLETED:
    // 아무것도 하지 않음 (명령 대기)
    break;

  case STATE_PRESSING:
    // 누름 유지 시간(holdSeconds) 확인
    if (now - stateTimerMs >= (unsigned long)(config.holdSeconds * 1000.0f))
    {
      // 복귀 각도로 이동
      setServoAngle(config.releaseAngle, true);
      stateTimerMs = now;
      currentState = STATE_RELEASING;
      Serial.printf("   └─ [RELEASE] 키 뗌 (복귀 각도: %d도)\n", config.releaseAngle);
    }
    break;

  case STATE_RELEASING:
    // 서보가 복귀 각도까지 회전할 물리적 시간(150ms) 확보 후 detach
    if (now - stateTimerMs >= 150)
    {
      if (keyServo.attached())
      {
        keyServo.detach(); // 신호 차단으로 지터 및 발열 완벽 방지
      }
      stateTimerMs = now;
      currentState = STATE_WAIT_INTERVAL;
    }
    break;

  case STATE_WAIT_INTERVAL:
  {
    // 다음 타건 주기(intervalSeconds) 대기
    // 주기 계산: 누름 시작부터 다음 누름 시작까지의 간격
    unsigned long elapsedInInterval = now - stateTimerMs;
    unsigned long waitTargetMs = (unsigned long)(config.intervalSeconds * 1000.0f);

    // 누름 유지 시간 및 릴리즈 지연 시간을 차감한 나머지 시간 대기
    if (waitTargetMs > (unsigned long)((config.holdSeconds * 1000.0f) + 150))
    {
      waitTargetMs -= (unsigned long)((config.holdSeconds * 1000.0f) + 150);
    }
    else
    {
      waitTargetMs = 50; // 최소 대기
    }

    if (elapsedInInterval >= waitTargetMs)
    {
      // 다시 누름 상태로 진입
      setServoAngle(config.pressAngle, true);
      stateTimerMs = now;
      currentState = STATE_PRESSING;
      executedCount++;
      unsigned long elapsedSec = (now - totalStartTimeMs) / 1000;
      unsigned long remainSec = (totalDurationMs > (now - totalStartTimeMs)) ? (totalDurationMs - (now - totalStartTimeMs)) / 1000 : 0;
      Serial.printf("\n👉 [CLICK #%lu] 키 누름! (주기: %.2f초, 경과: %lu초, 잔여: %lu초)\n", 
                    (unsigned long)executedCount, config.intervalSeconds, elapsedSec, remainSec);
    }
    break;
  }
  }
}

// ==========================================
// 12. setup() 및 loop()
// ==========================================
void setup()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // 초기 LED OFF (Active Low 기준)

  Serial.begin(115200);
  delay(1000); // SuperMini Native USB CDC 포트 안정화 대기

  Serial.println("\n=============================================");
  Serial.println("   RepeatAdu - Keyboard Autoclicker Firmware");
  Serial.println("   MCU: ESP32-C3 SuperMini USB-C");
  Serial.println("   Actuator: SG90 Servo on GPIO " + String(SERVO_PIN));
  Serial.println("=============================================");

  // 서보 초기 각도 설정 및 detach
  releaseServoToRest();

  // BLE 시작
  initBLE();

  Serial.println("[SETUP] System ready! Enter 'HELP' in Serial Monitor or connect via Flutter App.\n");
}

void loop()
{
  // 1. 비차단식 FSM 타이머 구동
  loopFSM();

  // 2. 주기적 텔레메트리 송출 (1초 간격)
  sendTelemetry(false);

  // 3. LED 상태 갱신
  updateLed();

  // 4. USB-C 시리얼 CLI 명령 감지
  processSerialCli();

  // 작은 딜레이로 WDT(워치독) 리셋 방지 및 CPU 부하 경감
  delay(5);
}
