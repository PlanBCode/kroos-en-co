#include <PID_v1.h>

#define lengthof(x) (sizeof(x)/sizeof(*x))

const unsigned long TX_INTERVAL = 5 * 60000UL;

class FlowController {
public:
  int pumpPin;
  uint8_t pumpState;

  int sensorPin;
  int lastSensorState;
  int flowCounter;
  const double pulsesPerM2 = 1.0;
  double currentFlow;
  double targetFlow;
  double targetCount;

  FlowController(int sensorPin, int pumpPin = -1) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;
    pinMode(sensorPin, INPUT);
    if (pumpPin!=-1) {
      pinMode(pumpPin, OUTPUT);
      digitalWrite(pumpPin, LOW);
    }
    flowCounter = 0;
    targetFlow = 0;
  }

  double getCurrentFlow() {
    return currentFlow;
  }

  void setTargetFlow(double value) {
    targetFlow = value;
  }

  double getTargetFlow() {
    return targetFlow;
  }

  /* Return the pump duty cycle over the previous cycle (when called
   * directly after doCycle()). */
  double getPumpState() {
    // Pump still on? Full duty cycle then
    if (digitalRead(pumpPin) == HIGH)
      return 255;
    return pumpState;
  }

  void setPumpState(bool state) {
    digitalWrite(pumpPin, state);
  }

  bool doCycle(unsigned long /* now */, bool manual) {
    currentFlow = flowCounter / pulsesPerM2 * (3600000/TX_INTERVAL);
    flowCounter = 0;

    if (!manual) {
      if (pumpPin != -1) {
        int todo = (unsigned int)targetCount - flowCounter;
        // If we haven't reached the target by the end of the cycle,
        // reset to prevent building up backlog
        if (todo > 0)
          targetCount = flowCounter;

        // targetCount is a double, so it preserves fractional pulses
        // across cycles
        targetCount += targetFlow*(TX_INTERVAL/3600000);
        todo = (unsigned int)targetCount - flowCounter;

        // If we're leaking more than the intended flow, reset to
        // prevent building up backlog
        if (todo < 0)
          targetCount = flowCounter;

        if (todo) {
          digitalWrite(pumpPin, HIGH);
        } else {
          digitalWrite(pumpPin, LOW);
        }
      }
    }
    return false;
  }

  void doLoop(unsigned long lastCycle, unsigned long now, bool manual) {
    int sensorState = digitalRead(sensorPin);
    if (sensorState != lastSensorState) {
      if (sensorState == HIGH) flowCounter++;
      lastSensorState = sensorState;
    }

    if (!manual) {
      if (pumpPin != -1) {
        int todo = (unsigned int)targetCount - flowCounter;
        if (digitalRead(pumpPin) == HIGH && todo <= 0) {
          digitalWrite(pumpPin, LOW);
          // Calculate how long the pump has been on
          pumpState = (now - lastCycle) * 255 / TX_INTERVAL;
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

  PID* pid;
  double pidOutput;
  double Kp=10, Ki=0.001, Kd=0;

  LevelController(int sensorPin, int pumpPin) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;

    pinMode(pumpPin, OUTPUT);
    digitalWrite(pumpPin, LOW);

    pid = new PID(&currentLevel, &pidOutput, &targetLevel, Kp, Ki, Kd, DIRECT);
    // The library expects ms, so divides this by 1000 so the Ki/Kd
    // values are per second. However, 5 minutes in ms overflows an int,
    // so scale by 60 here. The Ki/Kd values become per minute from
    // this.
    pid->SetSampleTime(TX_INTERVAL / 60);
    pid->SetMode(AUTOMATIC);
    pid->SetOutputLimits(0, 1);

    targetLevel = 0;
    currentLevel = 0;
    minLevel = 0;
    maxLevel = 1.6; // Max sensor reading
  }

  double getCurrentLevel() {
    return currentLevel;
  }

  void setTargetLevel(double value) {
    targetLevel = value;
  }
  double getTargetLevel() {
    return targetLevel;
  }

  void setMinLevel(double value) {
    minLevel = value;
  }
  double getMinLevel() {
    return minLevel;
  }

  void setMaxLevel(double value) {
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
    digitalWrite(pumpPin, state);
  }

  bool doCycle(unsigned long /* now */, bool manual) {
    currentLevel = a*analogRead(sensorPin) + b;
    if (currentLevel < minLevel || currentLevel > maxLevel) {
      return true;
    }
    else if (!manual) {
      // Store pumpstate over *previous* cycle
      pumpState = pidOutput * 255;
      // Calculate pump time for next cycle
      pid->Compute();
      pumpOnDuration = TX_INTERVAL*pidOutput;
      if (pumpOnDuration) {
        digitalWrite(pumpPin, HIGH);
      } else {
        digitalWrite(pumpPin, LOW);
      }
    }
    return false;
  }

  void doLoop(unsigned long lastCycle, unsigned long now, bool manual) {
    if (!manual) {
      if (now - lastCycle > pumpOnDuration) {
        digitalWrite(pumpPin, LOW);
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
    // When called after doCycle(), this might cut the timeout a little
    // bit short (max TX_INTERVAL)
    if (timeout>0) {
      manualTimeout = timeout*60000;
    }
  }

  unsigned int getManualTimeout() {
    return manualTimeout / 60000;
  }

  bool doCycle(unsigned long lastCycle, unsigned long now) {
    manualTimeout -= min(manualTimeout, now - lastCycle);
    bool manual = (manualTimeout > 0);
    for (size_t i=0;i<lengthof(flow);i++) panic |= flow[i]->doCycle(now, manual);
    for (size_t i=0;i<lengthof(level);i++) panic |= level[i]->doCycle(now, manual);
    // TODO: Act on panic?
    return panic;
  }

  void doLoop(unsigned long lastCycle, unsigned long now) {
    bool manual = manualTimeout > 0 && manualTimeout < (now - lastCycle);
    lastCycle = now;

    for (size_t i=0;i<lengthof(flow);i++) flow[i]->doLoop(lastCycle, now, manual);
    for (size_t i=0;i<lengthof(level);i++) level[i]->doLoop(lastCycle, now, manual);
  }
};

