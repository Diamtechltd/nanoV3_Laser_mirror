#include <Arduino.h>

struct Motor {
  const uint8_t stepPin;
  const uint8_t dirPin;
  const uint8_t enablePin;
  long positionSteps;
  float stepsPerMm;

  Motor(uint8_t step, uint8_t dir, uint8_t enable, float spm)
      : stepPin(step), dirPin(dir), enablePin(enable), positionSteps(0), stepsPerMm(spm) {}
};

Motor motorX(5, 6, 4, 200.0f);
Motor motorY(9, 10, 8, 200.0f);

constexpr uint16_t kStepDelayUs = 1200;
constexpr uint16_t kHomeDelayUs = 800;

void setMotorEnabled(Motor& motor, bool enabled) {
  digitalWrite(motor.enablePin, enabled ? LOW : HIGH);
}

void setMotorDirection(Motor& motor, bool forward) {
  digitalWrite(motor.dirPin, forward ? HIGH : LOW);
}

void pulseStep(const Motor& motor, uint16_t delayUs) {
  digitalWrite(motor.stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(motor.stepPin, LOW);
  delayMicroseconds(delayUs);
}

void moveMotor(Motor& motor, long steps) {
  if (steps == 0) {
    return;
  }

  const bool forward = steps > 0;
  setMotorDirection(motor, forward);
  const uint16_t delayUs = forward ? kStepDelayUs : kStepDelayUs;

  for (long i = 0; i < abs(steps); ++i) {
    pulseStep(motor, delayUs);
  }

  motor.positionSteps += steps;
}

void moveMotorMm(Motor& motor, float mm) {
  const long steps = static_cast<long>(mm * motor.stepsPerMm);
  moveMotor(motor, steps);
}

void printStatus() {
  Serial.println(F("-- mirror controller --"));
  Serial.print(F("X position: "));
  Serial.print(motorX.positionSteps);
  Serial.println(F(" steps"));
  Serial.print(F("Y position: "));
  Serial.print(motorY.positionSteps);
  Serial.println(F(" steps"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  help"));
  Serial.println(F("  status"));
  Serial.println(F("  home x|y|all"));
  Serial.println(F("  x <mm>"));
  Serial.println(F("  y <mm>"));
  Serial.println(F("  jog x <steps>"));
  Serial.println(F("  jog y <steps>"));
}

void processCommand(String command) {
  command.trim();
  if (command.length() == 0) {
    return;
  }

  if (command.equalsIgnoreCase("help")) {
    printHelp();
    return;
  }

  if (command.equalsIgnoreCase("status")) {
    printStatus();
    return;
  }

  if (command.startsWith("home ")) {
    const String target = command.substring(5);
    if (target.equalsIgnoreCase("x")) {
      motorX.positionSteps = 0;
      Serial.println(F("Home X complete"));
    } else if (target.equalsIgnoreCase("y")) {
      motorY.positionSteps = 0;
      Serial.println(F("Home Y complete"));
    } else if (target.equalsIgnoreCase("all")) {
      motorX.positionSteps = 0;
      motorY.positionSteps = 0;
      Serial.println(F("Home all complete"));
    } else {
      Serial.println(F("Usage: home x|y|all"));
    }
    return;
  }

  if (command.startsWith("x ")) {
    const String valueText = command.substring(2);
    const float mm = valueText.toFloat();
    setMotorEnabled(motorX, true);
    moveMotorMm(motorX, mm);
    setMotorEnabled(motorX, false);
    Serial.print(F("Moved X by "));
    Serial.print(mm);
    Serial.println(F(" mm"));
    return;
  }

  if (command.startsWith("y ")) {
    const String valueText = command.substring(2);
    const float mm = valueText.toFloat();
    setMotorEnabled(motorY, true);
    moveMotorMm(motorY, mm);
    setMotorEnabled(motorY, false);
    Serial.print(F("Moved Y by "));
    Serial.print(mm);
    Serial.println(F(" mm"));
    return;
  }

  if (command.startsWith("jog x ")) {
    const String valueText = command.substring(6);
    const long steps = valueText.toInt();
    setMotorEnabled(motorX, true);
    moveMotor(motorX, steps);
    setMotorEnabled(motorX, false);
    Serial.print(F("Jogged X by "));
    Serial.print(steps);
    Serial.println(F(" steps"));
    return;
  }

  if (command.startsWith("jog y ")) {
    const String valueText = command.substring(6);
    const long steps = valueText.toInt();
    setMotorEnabled(motorY, true);
    moveMotor(motorY, steps);
    setMotorEnabled(motorY, false);
    Serial.print(F("Jogged Y by "));
    Serial.print(steps);
    Serial.println(F(" steps"));
    return;
  }

  Serial.println(F("Unknown command. Type 'help'."));
}

void setup() {
  Serial.begin(115200);
  pinMode(motorX.stepPin, OUTPUT);
  pinMode(motorX.dirPin, OUTPUT);
  pinMode(motorX.enablePin, OUTPUT);
  pinMode(motorY.stepPin, OUTPUT);
  pinMode(motorY.dirPin, OUTPUT);
  pinMode(motorY.enablePin, OUTPUT);

  digitalWrite(motorX.stepPin, LOW);
  digitalWrite(motorX.dirPin, LOW);
  digitalWrite(motorX.enablePin, HIGH);
  digitalWrite(motorY.stepPin, LOW);
  digitalWrite(motorY.dirPin, LOW);
  digitalWrite(motorY.enablePin, HIGH);

  Serial.println(F("Mirror alignment controller ready"));
  printHelp();
}

void loop() {
  static String input;

  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (input.length() > 0) {
        processCommand(input);
        input = "";
      }
    } else {
      input += c;
    }
  }
}
