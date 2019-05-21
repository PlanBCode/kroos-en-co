#include <PID_v1.h>

#define lengthof(x) (sizeof(x)/sizeof(*x))

// set relais convention
#define PUMP_ON LOW
#define PUMP_OFF HIGH
#define DIRECTION_FORWARD LOW
#define DIRECTION_REVERSE HIGH

#ifndef LMIC_PRINTF_TO
#error "printfs need LMIC_PRINTF_TO, or they will crash"
#endif

const unsigned long CYCLE_INTERVAL = 5 * 60000UL;
//const unsigned long CYCLE_INTERVAL = 10000UL;

class PumpController {
public:
  int pumpPin;
  uint8_t enabled;
  uint8_t pumpDutyCycle;
  unsigned long pumpOnDuration;

  PumpController(int pumpPin) {
    if (pumpPin!=-1) {
      digitalWrite(pumpPin, PUMP_OFF);
      pinMode(pumpPin, OUTPUT);
    }
    enabled = false;
  }

  void enable() {
    enabled = true;
  }

  void disable() {
    enabled = false;
    digitalWrite(pumpPin, PUMP_OFF);
    setPumpDutyCycle(0);
  }

  double getPumpDutyCycle() {
    return pumpDutyCycle;
  }

  void setPumpDutyCycle(uint8_t dc) {
    if (pumpPin == -1)
      return;

    printf("SetPumpDutyCycle: %d\n", (int)dc);
    pumpDutyCycle = dc;
    pumpOnDuration = CYCLE_INTERVAL*dc/255;
    if (dc)
      setPumpState(true);
  }

  bool getPumpState() {
    if (pumpPin == -1)
      return false;

    return digitalRead(pumpPin) == PUMP_ON;
  }

  void setPumpState(bool state) {
    if (pumpPin != -1 && (enabled || !state)) {
      printf("SetPumpState: %d\n", (int)state);
      digitalWrite(pumpPin, state ? PUMP_ON : PUMP_OFF);
    }
  }

  bool doCycle(unsigned long /* prevDuration */, bool manual) {
    // Enable the pumps at the start of the cycle if required
    if (pumpPin != -1 && manual && pumpDutyCycle)
      setPumpState(true);
    return false;
  }

  void doLoop(unsigned long durationSoFar, bool /* manual */) {
    if (pumpPin != -1 && pumpDutyCycle < 255 && durationSoFar > pumpOnDuration && getPumpState()) {
      setPumpState(false);
    }
  }
};

class FlowController : public PumpController {
public:
  int pulsePin;
  int directionPin;
  int lastPulseState;
  unsigned int forwardCounter;
  unsigned int reverseCounter;
  const double pulsesPerM3 = 200.0;
  double forwardFlow;
  double reverseFlow;
  double targetFlow;
  uint32_t targetCount;
  uint8_t prevPumpDutyCycle;

  unsigned long lastPulseStart;

  // pumpPin -1 means no pump is connected, only measure flow
  // directionPin -1 means no direction pin is connected, assume forward
  // flow only.
  FlowController(int pulsePin, int directionPin = -1, int pumpPin = -1)
  : PumpController(pumpPin) {
    this->pulsePin = pulsePin;
    this->directionPin = directionPin;
    this->pumpPin = pumpPin;
    pinMode(pulsePin, INPUT);
    // Use a pullup for the direction pin, since this is switched by a
    // relay in the sensor.
    if (directionPin >= 0)
      pinMode(directionPin, INPUT_PULLUP);

    forwardCounter = 0;
    reverseCounter = 0;
    targetFlow = 0.0;
    targetCount = 0;
  }

  double getCurrentFlow() {
    return forwardFlow - reverseFlow;
  }

  double getForwardFlow() {
    return forwardFlow;
  }

  double getReverseFlow() {
    return reverseFlow;
  }

  void setTargetFlow(double value) {
    printf("SetTargetFlow: %d\n", (int)value);
    targetFlow = value;
  }

  double getTargetFlow() {
    return targetFlow;
  }

  bool doCycle(unsigned long prevDuration, bool manual) {
    PumpController::doCycle(prevDuration, manual);
    forwardFlow = (forwardCounter / pulsesPerM3) * (3600000/prevDuration);
    reverseFlow = (reverseCounter / pulsesPerM3) * (3600000/prevDuration);
    Serial.print("3600000/prevDuration: ");
    Serial.println(3600000/prevDuration);
    Serial.print("Forward ");
    Serial.print(forwardCounter);
    Serial.print(" pulses, ");
    Serial.print(forwardCounter / pulsesPerM3);
    Serial.print(" m3, ");
    Serial.print(forwardFlow);
    Serial.println(" m3/h");

    Serial.print("Reverse ");
    Serial.print(reverseCounter);
    Serial.print(" pulses, ");
    Serial.print(reverseCounter / pulsesPerM3);
    Serial.print(" m3, ");
    Serial.print(reverseFlow);
    Serial.println(" m3/h");

    // Calculate how much extra counts of forward flow there have been
    // (e.g. too much flow due to flow after the pump shut down, or
    // perhaps too little flow due to water flowing back).
    int extraCounts = forwardCounter - targetCount - reverseCounter;

    forwardCounter = 0;
    reverseCounter = 0;

    if (!manual) {
      if (pumpPin != -1) {
        // If we haven't reached the target by the end of the cycle,
        // doLoop won't have set prevPumpDutyCycle
        if (digitalRead(pumpPin) == PUMP_ON) {
          printf("Did not reach flow target\n");
          prevPumpDutyCycle = 255;
        }
        targetCount = targetFlow * pulsesPerM3 / (3600000/CYCLE_INTERVAL);

        // Correct for extra (or too little) flow in the previous cycle
        if ((int)targetCount > extraCounts)
          targetCount -= extraCounts;
        else
          targetCount = 0;

        setPumpDutyCycle(targetCount >= 1 ? 255 : 0);
        printf("Flow pulses target*100: %d\n", (int)(targetCount*100));
      }
    } else {
      prevPumpDutyCycle = pumpDutyCycle;
    }
    return false;
  }

  void doLoop(unsigned long durationSoFar, bool manual) {
    PumpController::doLoop(durationSoFar, manual);

    int pulseState = digitalRead(pulsePin);
    if (pulseState != lastPulseState) {
      if (pulseState == HIGH) lastPulseStart = millis();
      if (pulseState == LOW && millis() - lastPulseStart > 10) {
        if (directionPin < 0 || digitalRead(directionPin) == DIRECTION_FORWARD)
          forwardCounter++;
        else
          reverseCounter++;
      }
      lastPulseState = pulseState;
    }

    if (!manual) {
      if (pumpPin != -1) {
        if (getPumpState() && forwardCounter >= targetCount) {
          setPumpState(0);
          // Calculate how long the pump has been on
          prevPumpDutyCycle = durationSoFar * 255 / CYCLE_INTERVAL;
          printf("Flow target reached, duty cycle was %d\n", prevPumpDutyCycle);
        }
      }
    }
  }
};

class LevelController : public PumpController {
public:
  int sensorPin;
  // Value in cm = analog reading * a + b
  double a;
  double b;
  double currentLevel;
  double targetLevel;
  double minLevel;
  double maxLevel;

  uint8_t prevPumpDutyCycle;

  PID* pid;
  double pidOutput;
  const double Kp=0.0072, Ki=0.00018, Kd=0;

  LevelController(int sensorPin, int pumpPin, double a, double b)
  : PumpController(pumpPin) {
    this->sensorPin = sensorPin;
    this->pumpPin = pumpPin;
    this->a = a;
    this->b = b;

    pinMode(pumpPin, OUTPUT);
    setPumpState(0);
    enabled = false;

    pidOutput = 0.0;
    targetLevel = 0;
    currentLevel = 0;
    minLevel = 0;
    maxLevel = 1023; // Max ADC reading

    pid = new PID(&currentLevel, &pidOutput, &targetLevel, Kp, Ki, Kd, REVERSE);
    // The library expects ms, so divides this by 1000 so the Ki/Kd
    // values are per second. However, 5 minutes in ms overflows an int,
    // so scale by 60 here. The Ki/Kd values become per minute from
    // this.
    pid->SetSampleTime(CYCLE_INTERVAL / 60);
    pid->SetOutputLimits(0, 1);
    pid->SetMode(AUTOMATIC);
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

  // return value true means panic
  bool doCycle(unsigned long prevDuration, bool manual) {
    PumpController::doCycle(prevDuration, manual);

    currentLevel = analogRead(sensorPin);
    double cm = a*currentLevel + b;
    printf("Note: calibration values for calculating mm are not synchronized with server!\n");
    printf("Level adc: %u, mm: %d\n", (unsigned)currentLevel, (int)(cm * 10));
    if (currentLevel < minLevel || currentLevel > maxLevel) {
      return true;
    }
    else if (!manual) {
      // Store pumpstate over *previous* cycle
      prevPumpDutyCycle = pumpDutyCycle;
      // Calculate pump time for next cycle
      pid->Compute();
      setPumpDutyCycle(pidOutput * 255);
      printf("PID says current: %d, target: %d dc: %d\n", (int)currentLevel, (int)targetLevel, (int)(pumpDutyCycle));
    } else {
      prevPumpDutyCycle = pumpDutyCycle;
    }
    return false;
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

  void attachFlowController(size_t i, int sensorPin, int directionPin = -1, int pumpPin = -1) {
    flow[i] = new FlowController(sensorPin, directionPin, pumpPin);
  }

  void attachLevelController(size_t i, int sensorPin, int pumpPin, double a, double b) {
    level[i] = new LevelController(sensorPin, pumpPin, a, b);
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

