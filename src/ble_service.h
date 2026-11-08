#pragma once
#include <Arduino.h>
class AutoclickerEngine;
class BleService { public: void begin(AutoclickerEngine&);void tick();void processCommand(const String&);bool isConnected()const{return connected_;}void setConnected(bool connected){connected_=connected;} private:void telemetry(bool=false);AutoclickerEngine* engine_=nullptr;bool connected_=false;unsigned long last_=0;};
