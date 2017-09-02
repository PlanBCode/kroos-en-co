#include <PID_v1.h>

#define lengthof(x) (sizeof(x)/sizeof(*x))

// set relais convention
#define PUMP_ON LOW
#define PUMP_OFF HIGH

#ifndef LMIC_PRINTF_TO
#error "printfs need LMIC_PRINTF_TO, or they will crash"
#endif

//const unsigned long CYCLE_INTERVAL = 5 * 60000UL;
const unsigned long CYCLE_INTERVAL = 10000UL;

class FlowController {
public:
  int pumpPin;
  uint8_t pumpState;
  uint8_t enabled;

  int sensorPin;
  int lastSensorState;
  unsigned int flowCounter;
  const double pulsesPerM3 = 200.0;
  double currentFlow;
  double targetFlow;
  uint32_t targetCount;

  unsigned long lastPulseStart;

  // pumpPin -1 means no pump is connected, only measure flow
  FlowController(int sensorPin, int pumpPin = -1) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;
    pinMode(sensorPin, INPUT);
    
    if (pumpPin!=-1) {
      digitalWrite(pumpPin, PUMP_OFF);
      pinMode(pumpPin, OUTPUT);
    }
    enabled = false;
    
    flowCounter = 0.0;
    targetFlow = 0.0;
    targetCount = 0;
  }

  void enable() {
    enabled = true;
  }

  void disable() {
    enabled = false;
    digitalWrite(pumpPin, PUMP_OFF);
  }
  
  double getCurrentFlow() {
    return currentFlow;
  }

  void setTargetFlow(double value) {
    printf("SetTargetFlow: %d\n", (int)value);
    targetFlow = value;
  }

  double getTargetFlow() {
    return targetFlow;
  }

  double getPumpState() {
    return pumpState;
  }

  void setPumpState(bool state) {
    if (enabled) {
      printf("SetPumpState: %d\n", (int)state);
      digitalWrite(pumpPin, !state);
      pumpState = state ? 255 : 0;
    }
  }

  bool doCycle(unsigned long prevDuration, bool manual) {
    currentFlow = (flowCounter / pulsesPerM3) * (3600000/prevDuration);
    Serial.print("3600000/prevDuration: ");
    Serial.println(3600000/prevDuration);
    Serial.print("flowCounter / pulsesPerM3: ");
    Serial.println(flowCounter / pulsesPerM3);
    Serial.print("currentFlow: ");
    Serial.println(currentFlow);
    printf("Flow pulses: %d, flow*100: %d\n", flowCounter, (int)(currentFlow*100));
    flowCounter = 0;

    if (!manual) {
      if (pumpPin != -1) {
        // If we haven't reached the target by the end of the cycle,
        // doLoop won't have set pumpState
        if (digitalRead(pumpPin) == PUMP_ON) {
          printf("Did not reach flow target\n");
          pumpState = 255;
        }
        targetCount = targetFlow * pulsesPerM3 / (3600000/CYCLE_INTERVAL);
        setPumpState(targetCount >= 1 ? 1 : 0);
        printf("Flow pulses target*100: %d\n", (int)(targetCount*100));
      }
    }
    return false;
  }

  void doLoop(unsigned long durationSoFar, bool manual) {
    int sensorState = digitalRead(sensorPin);
    if (sensorState != lastSensorState) {
      if (sensorState == HIGH) lastPulseStart = millis();
      if (sensorState == LOW && millis() - lastPulseStart > 10) {
//        printf("Flow pulse\n");
        flowCounter++;
      }
      lastSensorState = sensorState;
    }

    if (!manual) {
      if (pumpPin != -1) {
        if (digitalRead(pumpPin) == PUMP_ON && targetCount >= flowCounter) {
          setPumpState(0);
          // Calculate how long the pump has been on
          pumpState = durationSoFar * 255 / CYCLE_INTERVAL;
          printf("Flow target reached, duty cycle was %d\n", pumpState);
        }
      }
    }
  }
};

class LevelController {
public:
  int sensorPin;
  // 5000mV, 1023 steps, 100Ohm, 0.15mA/cm
  double a=5000.0/1023/100/0.15;
  // Zero is 4mA
  double b=-4/0.15;
  double currentLevel;
  double targetLevel;
  double minLevel;
  double maxLevel;

  int pumpPin;
  uint8_t pumpState;
  unsigned long pumpOnDuration;
  uint8_t enabled;

  PID* pid;
  double pidOutput;
  const double Kp=10, Ki=0.001, Kd=0;

  LevelController(int sensorPin, int pumpPin) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;

    pinMode(pumpPin, OUTPUT);
    setPumpState(0);
    enabled = false;

    pid = new PID(&currentLevel, &pidOutput, &targetLevel, Kp, Ki, Kd, DIRECT);
    // The library expects ms, so divides this by 1000 so the Ki/Kd
    // values are per second. However, 5 minutes in ms overflows an int,
    // so scale by 60 here. The Ki/Kd values become per minute from
    // this.
    pid->SetSampleTime(CYCLE_INTERVAL / 60);
    pid->SetMode(AUTOMATIC);
    pid->SetOutputLimits(0, 1);

    pidOutput = 0.0;
    targetLevel = 0;
    currentLevel = 0;
    minLevel = 0;
    maxLevel = 106; // Max sensor reading
  }

  void enable() {
    enabled = true;
  }

  void disable() {
    enabled = false;
    digitalWrite(pumpPin, PUMP_OFF);
  }
  
  double getCurrentLevel() {
    return currentLevel;
  }

  void setTargetLevel(double value) {
    printf("SetTargetLevel: %d\n", (int)value);
    targetLevel = value;
  }
  
  double getTargetLevel() {
    return targetLevel;
  }

  void setMinLevel(double value) {
    printf("SetMinLevel: %d\n", (int)value);
    minLevel = value;
  }
  
  double getMinLevel() {
    return minLevel;
  }

  void setMaxLevel(double value) {
    printf("SetMaxLevel: %d\n", (int)value);
    maxLevel = value;
  }
  
  double getMaxLevel() {
    return maxLevel;
  }

  /* Return the pump duty cycle over the previous cycle (when called
   * directly after doCycle()). */
  uint8_t getPumpState() {
    return pumpState;
  }

  void setPumpState(bool state) {
    if (enabled) {
      printf("SetPumpState: %d\n", (int)state);
      digitalWrite(pumpPin, state ? PUMP_ON : PUMP_OFF);
      pumpState = state ? 255 : 0;
    }
  }
  
  // return value true means panic
  bool doCycle(unsigned long /* duration */, bool manual) {
    uint16_t read = analogRead(sensorPin);
    currentLevel = a*read + b;
    printf("Level adc: %u, mm: %d\n", read, (int)(currentLevel * 10));
    if (currentLevel < minLevel || currentLevel > maxLevel) {
      return true;
    }
    else if (!manual) {
      // Store pumpstate over *previous* cycle
      pumpState = pidOutput * 255;
      printf("Previous pump duty cycle %d\n", pumpState);
      // Calculate pump time for next cycle
      pid->Compute();
      pumpOnDuration = CYCLE_INTERVAL*pidOutput;
      printf("PID says target: %d dc: %d%%, on: %ds\n", (int)targetLevel, (int)(pidOutput * 100), (int)(pumpOnDuration/1000));
      if (pumpOnDuration) {
        setPumpState(1);
      } else {
        setPumpState(0);
      }
    }
    return false;
  }

  void doLoop(unsigned long durationSoFar, bool manual) {
    if (!manual) {
      if (durationSoFar > pumpOnDuration) {
        setPumpState(0);
      }
    }
  }
};

class Battery {
public:
  FlowController* flow[2];
  LevelController* level[3];
  unsigned long manualTimeout;
  bool panic;

  Battery() {
    manualTimeout = 0;
    panic = false;
  }

  void attachFlowController(size_t i, int sensorPin, int pumpPin = -1) {
    flow[i] = new FlowController(sensorPin, pumpPin);
  }

  void attachLevelController(size_t i, int sensorPin, int pumpPin) {
    level[i] = new LevelController(sensorPin, pumpPin);
  }

  void setManualTimeout(unsigned int timeout) {
    printf("SetManualTimeout: %u\n", timeout);
    // When called after doCycle(), this might cut the timeout a little
    // bit short (max CYCLE_INTERVAL)
    manualTimeout = timeout*60000;
  }

  unsigned int getManualTimeout() {
    return manualTimeout / 60000;
  }

  bool doCycle(unsigned long prevDuration) {
    manualTimeout -= min(manualTimeout, prevDuration);
    bool manual = (manualTimeout > 0);
    for (size_t i=0;i<lengthof(flow);i++) panic |= flow[i]->doCycle(prevDuration, manual);
    for (size_t i=0;i<lengthof(level);i++) panic |= level[i]->doCycle(prevDuration, manual);
    printf("Manualtimeout: %lu panic: %d\n", manualTimeout, (int)panic);
    if (panic) {
      for (size_t i=0;i<lengthof(flow);i++) flow[i]->disable();
      for (size_t i=0;i<lengthof(level);i++) level[i]->disable();
    }
    return panic;
  }

  void doLoop(unsigned long durationSoFar) {
    bool manual = (manualTimeout > 0);

    for (size_t i=0;i<lengthof(flow);i++) flow[i]->doLoop(durationSoFar, manual);
    for (size_t i=0;i<lengthof(level);i++) level[i]->doLoop(durationSoFar, manual);
  }
};

