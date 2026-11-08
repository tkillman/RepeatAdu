#include "autoclicker_engine.h"
#include <ESP32Servo.h>
namespace { constexpr int servoPin=3,ledPin=8; constexpr unsigned long releaseMs=150; Servo servo; }
const char* clickerStateName(ClickerState s){switch(s){case STATE_IDLE:return "IDLE";case STATE_PRESSING:return "PRESSING";case STATE_COMPLETED:return "COMPLETED";default:return "RUNNING";}}
void AutoclickerEngine::begin(){pinMode(ledPin,OUTPUT); digitalWrite(ledPin,HIGH); release();}
void AutoclickerEngine::setServo(int angle){if(!servo.attached())servo.attach(servoPin,500,2400);servo.write(constrain(angle,0,180));}
void AutoclickerEngine::release(){setServo(config_.releaseAngle);delay(releaseMs);if(servo.attached())servo.detach();}
unsigned long AutoclickerEngine::nextInterval()const{auto lo=(unsigned long)(config_.intervalMinSeconds*1000);auto hi=(unsigned long)(config_.intervalMaxSeconds*1000);return lo==hi?lo:random(lo,hi+1);}
void AutoclickerEngine::start(float total,float min,float max,float hold,int press,int releaseAngle){config_.totalMinutes=total>0?total:30;config_.intervalMinSeconds=min>.1?min:5;config_.intervalMaxSeconds=max>=config_.intervalMinSeconds?max:config_.intervalMinSeconds;config_.holdSeconds=hold>.05?hold:.2;config_.pressAngle=constrain(press,0,180);config_.releaseAngle=constrain(releaseAngle,0,180);duration_=(unsigned long)(config_.totalMinutes*60000);started_=millis();count_=1;setServo(config_.pressAngle);stateAt_=millis();state_=STATE_PRESSING;}
void AutoclickerEngine::stop(const char* reason){Serial.printf("[STOP] %s\n",reason);release();state_=STATE_IDLE;}
void AutoclickerEngine::reset(){release();config_=ClickerConfig();state_=STATE_IDLE;started_=duration_=stateAt_=0;selected_=5000;count_=0;Serial.println("[RESET] Initial state restored.");}
void AutoclickerEngine::calibrate(int angle){setServo(angle);delay(400);release();}
int AutoclickerEngine::remainingSeconds()const{if(state_==STATE_IDLE||state_==STATE_COMPLETED)return 0;auto elapsed=millis()-started_;return elapsed>=duration_?0:(duration_-elapsed)/1000;}
int AutoclickerEngine::elapsedSeconds()const{return started_?(millis()-started_)/1000:0;}
void AutoclickerEngine::tick(){auto now=millis();if(state_!=STATE_IDLE&&state_!=STATE_COMPLETED&&now-started_>=duration_){release();state_=STATE_COMPLETED;return;}if(state_==STATE_PRESSING&&now-stateAt_>=(unsigned long)(config_.holdSeconds*1000)){setServo(config_.releaseAngle);stateAt_=now;state_=STATE_RELEASING;}else if(state_==STATE_RELEASING&&now-stateAt_>=releaseMs){if(servo.attached())servo.detach();selected_=nextInterval();stateAt_=now;state_=STATE_WAIT_INTERVAL;}else if(state_==STATE_WAIT_INTERVAL){auto motion=(unsigned long)(config_.holdSeconds*1000)+releaseMs;auto wait=selected_>motion?selected_-motion:50;if(now-stateAt_>=wait){setServo(config_.pressAngle);stateAt_=now;state_=STATE_PRESSING;count_++;}}}
void AutoclickerEngine::updateLed(bool connected){static unsigned long last=0;static bool on=false;if(state_==STATE_PRESSING)digitalWrite(ledPin,LOW);else if(state_==STATE_WAIT_INTERVAL||state_==STATE_RELEASING){if(millis()-last>=500){last=millis();on=!on;digitalWrite(ledPin,on?LOW:HIGH);}}else digitalWrite(ledPin,connected?LOW:HIGH);}
