#include <Arduino.h>
#include "autoclicker_engine.h"
#include "ble_service.h"
AutoclickerEngine engine;BleService ble;
void processSerialCli(){if(!Serial.available())return;String line=Serial.readStringUntil('\n');line.trim();if(line.isEmpty())return;if(line.startsWith("{")){ble.processCommand(line);return;}if(line.equalsIgnoreCase("HELP"))Serial.println("START [total interval hold], STOP, RESET, TEST, STATUS");else if(line.startsWith("START")||line.startsWith("start")){float t=engine.config().totalMinutes,i=engine.config().intervalMinSeconds,h=engine.config().holdSeconds;sscanf(line.c_str(),"%*s %f %f %f",&t,&i,&h);engine.start(t,i,i,h,engine.config().pressAngle,engine.config().releaseAngle);}else if(line.equalsIgnoreCase("STOP"))engine.stop("Serial STOP Command");else if(line.equalsIgnoreCase("RESET"))engine.reset();else if(line.equalsIgnoreCase("TEST"))engine.calibrate(engine.config().pressAngle);else if(line.equalsIgnoreCase("STATUS"))ble.processCommand("STATUS");}
void setup(){Serial.begin(115200);randomSeed(esp_random());delay(1000);engine.begin();ble.begin(engine);Serial.println("RepeatAdu ready.");}
void loop(){engine.tick();ble.tick();engine.updateLed(ble.isConnected());processSerialCli();delay(5);}
