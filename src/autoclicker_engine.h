#pragma once
#include <Arduino.h>
enum ClickerState
{
  STATE_IDLE,
  STATE_WAIT_INTERVAL,
  STATE_PRESSING,
  STATE_RELEASING,
  STATE_COMPLETED
};
struct ClickerConfig
{
  float totalMinutes = 30, intervalMinSeconds = 5, intervalMaxSeconds = 5, holdSeconds = .2;
  int pressAngle = 75, releaseAngle = 0;
};
class AutoclickerEngine
{
public:
  void begin();
  void start(float, float, float, float, int = 75, int = 0);
  void stop(const char *reason = "User Request");
  void reset();
  void calibrate(int);
  void tick();
  void updateLed(bool);
  ClickerState state() const { return state_; }
  const ClickerConfig &config() const { return config_; }
  uint32_t executedCount() const { return count_; }
  int remainingSeconds() const;
  int elapsedSeconds() const;

private:
  void setServo(int);
  void release();
  unsigned long nextInterval() const;
  ClickerConfig config_;
  ClickerState state_ = STATE_IDLE;
  unsigned long started_ = 0, duration_ = 0, stateAt_ = 0, selected_ = 5000;
  uint32_t count_ = 0;
};
const char *clickerStateName(ClickerState);
