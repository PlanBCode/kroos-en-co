#include <PID_v1.h>

const unsigned long TX_INTERVAL = 5 * 60000UL;

class FlowController {
public:
  int pumpPin;
  double pumpState;
  int pumpOffTime;

  int sensorPin;
  double a=0.16, b=-3;
  int lastSensorState;
  int flowCounter;
  double currentFlow;
  double targetFlow;

  FlowController(int sensorPin, int pumpPin = -1) {
    pinMode(sensorPin, INPUT);
    if (pumpPin!=-1) {
      pinMode(pumpPin, OUTPUT);
      digitalWrite(pumpPin, LOW);
    }
    flowCounter = 0;
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

  double getPumpState() {
    return pumpState;
  }

  void setPumpState(bool state) {
    digitalWrite(pumpPin, state);
  }

  bool doCycle(unsigned long now, bool manual) {
    currentFlow = a*flowCounter + b;
    flowCounter = 0;

    if (!manual) {
      if (pumpPin!=-1) {
        int pumpOnDuration = targetFlow*TX_INTERVAL/3600000;
        if (pumpOnDuration) {
          digitalWrite(pumpPin, HIGH);
          pumpOffTime = now + pumpOnDuration;
          pumpState = pumpOnDuration/TX_INTERVAL;
        }
      }
    }
    return false;
  }

  void doLoop(unsigned long now, bool manual) {
    int sensorState = digitalRead(sensorPin);
    if (sensorState!=lastSensorState) {
      if(sensorState==HIGH) flowCounter++;
      lastSensorState = sensorState;
    }

    if (!manual) {
      if (pumpPin!=-1) {
        if (digitalRead(pumpPin) && now > pumpOffTime) {
          digitalWrite(pumpPin, LOW);
        }
      }
    }
  }
};

class LevelController {
public:
  int sensorPin;
  double a=0.16, b=-3;
  double currentLevel;
  double targetLevel;
  double minLevel;
  double maxLevel;

  int pumpPin;
  int pumpState;
  int pumpOffTime;

  PID* pid;
  double pidOutput;
  double Kp=0.1, Ki=0.1, Kd=0;

  LevelController(int sensorPin, int pumpPin) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;

    pinMode(pumpPin, OUTPUT);

    pid = new PID(&currentLevel, &pidOutput, &targetLevel, Kp, Ki, Kd, DIRECT);
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

  double getPumpState() {
    return pumpState;
  }

  void setPumpState(bool state) {
    digitalWrite(pumpPin, state);
  }

  bool doCycle(unsigned long now, bool manual) {
    currentLevel = a*analogRead(sensorPin) + b;
    if (currentLevel < minLevel || currentLevel > maxLevel) {
      return true;
    }
    else if (!manual) {
      pid->Compute();
      unsigned long pumpOnDuration = TX_INTERVAL*pidOutput/255;
      if (pumpOnDuration) {
        digitalWrite(pumpPin, HIGH);
        pumpOffTime = now + pumpOnDuration;
        pumpState = pumpOnDuration/TX_INTERVAL;
      }
    }
    return false;
  }

  void doLoop(unsigned long now, bool manual) {
    if (!manual) {
      if (digitalRead(pumpPin) && now > pumpOffTime) {
        digitalWrite(pumpPin, LOW);
      }
    }
  }
};

class Battery {
public:
  FlowController* flow[2];
  LevelController* level[3];
  unsigned int manualEndTime;
  bool manual;
  bool panic;

  Battery() {
    panic = false;
  }

  void attachFlowController(int i, int sensorPin, int pumpPin = -1) {
    flow[i] = new FlowController(sensorPin, pumpPin);
  }

  void attachLevelController(int i, int sensorPin, int pumpPin) {
    level[i] = new LevelController(sensorPin, pumpPin);
  }

  void setManualTimeout(unsigned int timeout) {
    if (timeout>0) {
      manualEndTime = millis() + timeout*60000;
      manual = true;
    }
  }

  unsigned int getManualTimeout() {
    return (millis() - manualEndTime)/60000;
  }

  bool doCycle(unsigned long now) {
    for (int i=0;i<2;i++) panic|= flow[i]->doCycle(now, manual);
    for (int i=0;i<3;i++) panic|= level[i]->doCycle(now, manual);
    return panic;
  }

  void doLoop(unsigned long now) {
    if (now >= manualEndTime) manual = false;
    for (int i=0;i<2;i++) flow[i]->doLoop(now, manual);
    for (int i=0;i<3;i++) level[i]->doLoop(now, manual);
  }
};

