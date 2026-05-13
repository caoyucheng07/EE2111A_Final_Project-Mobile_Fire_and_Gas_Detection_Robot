#include <MeMCore.h>

// =========================
// Hardware
// =========================
MeDCMotor motorLeft(M1);
MeDCMotor motorRight(M2);
MeUltrasonicSensor ultrasonic(PORT_3);
MeLineFollower lineFinder(PORT_2);

// =========================
// Motor direction tuning
// =========================
const int LEFT_FWD_SIGN  = -1;
const int RIGHT_FWD_SIGN =  1;

// =========================
// Speeds
// =========================
const int PATROL_SPEED    = 120;
const int TURN_SPEED      = 110;
const int DOCK_SPEED      = 95;
const int DOCK_TURN_SPEED = 85;

// =========================
// Timing
// =========================
const uint16_t BACKUP_MS           = 220;
const uint16_t STOP_MS             = 60;
const uint16_t TURN_90_MS          = 585;
const uint16_t TURN_180_MS         = 1170;
const uint16_t INVEST_FWD_MS       = 300;
const uint16_t DOCK_REVERSE_MS     = 900;
const uint16_t DOCK_ALIGN_STABLE_MS = 150;

// =========================
// Safety
// =========================
const int BLOCK_DIST_CM = 12;

// =========================
// Modes
// =========================
enum BotMode {
  MODE_PATROL = 0,
  MODE_INVESTIGATE,
  MODE_DOCK
};

BotMode currentMode = MODE_PATROL;

// =========================
// Dock sub-state
// =========================
enum DockState {
  DOCK_IDLE = 0,
  DOCK_FIND_LINE,
  DOCK_ALIGN_LINE,
  DOCK_FOLLOW_LINE,
  DOCK_REVERSE_IN,
  DOCK_DONE
};

DockState dockState = DOCK_IDLE;
bool dockAutoActive = false;
bool dockIrLatched = false;
unsigned long dockStateStartMs = 0;
unsigned long dockAlignStableStartMs = 0;

// =========================
// Serial RX buffer
// =========================
String rxLine;

// =========================
// Raw motor control
// =========================
void moveForwardRaw(int speed) {
  motorLeft.run(LEFT_FWD_SIGN * speed);
  motorRight.run(RIGHT_FWD_SIGN * speed);
}

void moveBackwardRaw(int speed) {
  motorLeft.run(-LEFT_FWD_SIGN * speed);
  motorRight.run(-RIGHT_FWD_SIGN * speed);
}

void turnLeftRaw(int speed) {
  motorLeft.run(-LEFT_FWD_SIGN * speed);
  motorRight.run(RIGHT_FWD_SIGN * speed);
}

void turnRightRaw(int speed) {
  motorLeft.run(LEFT_FWD_SIGN * speed);
  motorRight.run(-RIGHT_FWD_SIGN * speed);
}

void stopMotors() {
  motorLeft.run(0);
  motorRight.run(0);
}

// =========================
// Reporting
// =========================
const char* modeName(BotMode m) {
  switch (m) {
    case MODE_PATROL:      return "PATROL";
    case MODE_INVESTIGATE: return "INVESTIGATE";
    case MODE_DOCK:        return "DOCK";
    default:               return "UNKNOWN";
  }
}

void report(const String& s) {
  Serial.println(s);
}

void setMode(BotMode newMode, bool announce = true) {
  currentMode = newMode;
  if (announce) {
    Serial.print("MODE:");
    Serial.println(modeName(currentMode));
  }
}

// =========================
// Timed motion helpers
// =========================
void moveForwardTimed(uint16_t ms, int speed = PATROL_SPEED) {
  moveForwardRaw(speed);
  delay(ms);
  stopMotors();
  delay(STOP_MS);
}

void moveBackwardTimed(uint16_t ms, int speed = PATROL_SPEED) {
  moveBackwardRaw(speed);
  delay(ms);
  stopMotors();
  delay(STOP_MS);
}

void turnLeftTimed(uint16_t ms) {
  turnLeftRaw(TURN_SPEED);
  delay(ms);
  stopMotors();
  delay(STOP_MS);
}

void turnRightTimed(uint16_t ms) {
  turnRightRaw(TURN_SPEED);
  delay(ms);
  stopMotors();
  delay(STOP_MS);
}

// =========================
// Safety helpers
// =========================
bool borderDetected() {
  int lineState = lineFinder.readSensors();
  return (lineState != S1_OUT_S2_OUT);
}

bool obstacleDetected() {
  long d = ultrasonic.distanceCm();
  return (d > 0 && d < BLOCK_DIST_CM);
}

bool safeForwardNow() {
  if (borderDetected()) return false;
  if (obstacleDetected()) return false;
  return true;
}

// =========================
// Patrol avoidance
// =========================
void avoidLeftBorder() {
  stopMotors();
  delay(STOP_MS);
  moveBackwardTimed(BACKUP_MS);

  long angle = random(45, 136);
  uint16_t t = (uint16_t)((angle * TURN_90_MS) / 90.0);
  turnRightTimed(t);
}

void avoidRightBorder() {
  stopMotors();
  delay(STOP_MS);
  moveBackwardTimed(BACKUP_MS);

  long angle = random(45, 136);
  uint16_t t = (uint16_t)((angle * TURN_90_MS) / 90.0);
  turnLeftTimed(t);
}

void avoidBothBlack() {
  // both sensors see black -> move forward a bit first
  moveForwardRaw(PATROL_SPEED - 10);
  delay(120);
  stopMotors();
  delay(STOP_MS);

  // then do the original random turn
  long angle = random(90, 181);
  uint16_t t = (uint16_t)((angle * TURN_90_MS) / 90.0);

  if (random(2) == 0) turnLeftTimed(t);
  else                turnRightTimed(t);
}

void avoidObstacle() {
  stopMotors();
  delay(STOP_MS);
  moveBackwardTimed(BACKUP_MS);

  long angle = random(60, 181);
  uint16_t t = (uint16_t)((angle * TURN_90_MS) / 90.0);

  if (random(2) == 0) turnLeftTimed(t);
  else                turnRightTimed(t);
}

// =========================
// Smooth patrol
// =========================
void patrolStep() {
  int lineState = lineFinder.readSensors();

  if (lineState == S1_IN_S2_OUT) {
    avoidLeftBorder();
    return;
  }

  if (lineState == S1_OUT_S2_IN) {
    avoidRightBorder();
    return;
  }

  if (lineState == S1_IN_S2_IN) {
    avoidBothBlack();
    return;
  }

  if (obstacleDetected()) {
    avoidObstacle();
    return;
  }

  moveForwardRaw(PATROL_SPEED);
}

// =========================
// Dock auto logic
// Desired follow pattern: LEFT sensor on black, RIGHT sensor on white
// =========================
void clearDockFlags() {
  dockIrLatched = false;
  dockAlignStableStartMs = 0;
}

void dockEnterFindLine() {
  dockAutoActive = true;
  dockState = DOCK_FIND_LINE;
  dockStateStartMs = millis();
  clearDockFlags();
  setMode(MODE_DOCK);
  report("DOCK:START");
}

void dockStartReverseIn() {
  stopMotors();
  delay(120);
  dockState = DOCK_REVERSE_IN;
  dockStateStartMs = millis();
  report("DOCK:IR_FOUND_REVERSING");
}

void dockFinish() {
  stopMotors();
  dockState = DOCK_DONE;
  dockAutoActive = false;
  report("DOCKED");
}

void dockStep() {
  if (!dockAutoActive) {
    delay(10);
    return;
  }

  switch (dockState) {
    case DOCK_FIND_LINE: {
      int lineState = lineFinder.readSensors();

      // Move forward until any sensor sees black
      if (lineState == S1_OUT_S2_OUT) {
        moveForwardRaw(DOCK_SPEED);
      } else {
        stopMotors();
        delay(80);
        dockState = DOCK_ALIGN_LINE;
        dockStateStartMs = millis();
        dockAlignStableStartMs = 0;
        report("DOCK:LINE_FOUND_ALIGNING");
      }
      break;
    }

    case DOCK_ALIGN_LINE: {
  int lineState = lineFinder.readSensors();

  // Target alignment: LEFT on black, RIGHT on white
  if (lineState == S1_IN_S2_OUT) {
    dockState = DOCK_FOLLOW_LINE;
    dockStateStartMs = millis();
    report("DOCK:ALIGNED_FOLLOWING");
  }
  else {
    // Lost line completely -> search again
    if (lineState == S1_OUT_S2_OUT) {
      turnLeftRaw(DOCK_TURN_SPEED);
    }
    // Too deep over line -> steer right while moving
    else if (lineState == S1_IN_S2_IN) {
      motorLeft.run(LEFT_FWD_SIGN * DOCK_SPEED);
      motorRight.run(RIGHT_FWD_SIGN * (DOCK_SPEED - 35));
    }
    // Wrong sensor on line -> correct right
    else if (lineState == S1_OUT_S2_IN) {
      turnRightRaw(DOCK_TURN_SPEED);
        }
        }
      break;
      }

    case DOCK_FOLLOW_LINE: {
      if (dockIrLatched) {
        dockStartReverseIn();
        break;
      }

      int lineState = lineFinder.readSensors();

      // Desired: LEFT on black, RIGHT on white
      if (lineState == S1_IN_S2_OUT) {
        moveForwardRaw(DOCK_SPEED);
      }
      // Lost line inward -> curve left to reacquire
      else if (lineState == S1_OUT_S2_OUT) {
        motorLeft.run(LEFT_FWD_SIGN * (DOCK_SPEED - 25));
        motorRight.run(RIGHT_FWD_SIGN * (DOCK_SPEED + 20));
      }
      // Too far onto line -> right correction
      else if (lineState == S1_IN_S2_IN) {
        motorLeft.run(LEFT_FWD_SIGN * DOCK_SPEED);
        motorRight.run(RIGHT_FWD_SIGN * (DOCK_SPEED - 35));
      }
      // Wrong sensor on line -> strong right correction
      else if (lineState == S1_OUT_S2_IN) {
        turnRightRaw(DOCK_TURN_SPEED);
      }
      break;
    }

    case DOCK_REVERSE_IN: {
      if (millis() - dockStateStartMs < DOCK_REVERSE_MS) {
        moveBackwardRaw(DOCK_SPEED);
      } else {
        dockFinish();
      }
      break;
    }

    case DOCK_DONE: {
      stopMotors();
      break;
    }

    default: {
      stopMotors();
      break;
    }
  }
}

// =========================
// Investigation commands
// =========================
void cmdPatrolOn() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  clearDockFlags();
  setMode(MODE_PATROL);
  report("DONE:PATROL_ON");
}

void cmdPatrolOff() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  clearDockFlags();
  setMode(MODE_INVESTIGATE);
  report("DONE:PATROL_OFF");
}

void cmdStop() {
  stopMotors();

  if (currentMode != MODE_DOCK) {
    setMode(MODE_INVESTIGATE, false);
  }

  dockAutoActive = false;
  dockState = DOCK_IDLE;
  report("DONE:STOP");
}

void cmdTurnLeft90() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_INVESTIGATE, false);
  turnLeftTimed(TURN_90_MS);
  report("DONE:TURN_LEFT_90");
}

void cmdTurnRight90() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_INVESTIGATE, false);
  turnRightTimed(TURN_90_MS);
  report("DONE:TURN_RIGHT_90");
}

void cmdTurnRight180() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_INVESTIGATE, false);
  turnRightTimed(TURN_180_MS);
  report("DONE:TURN_RIGHT_180");
}

void cmdStepFwdInvestigate() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_INVESTIGATE, false);

  if (!safeForwardNow()) {
    report("ABORT:STEP_FWD_INVEST");
    return;
  }

  moveForwardTimed(INVEST_FWD_MS, PATROL_SPEED);
  report("DONE:STEP_FWD_INVEST");
}

// =========================
// Dock / manual commands
// =========================
void cmdDockStart() {
  stopMotors();
  dockEnterFindLine();
  report("DONE:DOCK_START");
}

void cmdDockAbort() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  clearDockFlags();
  setMode(MODE_INVESTIGATE, false);
  report("DONE:DOCK_ABORT");
}

void cmdDockIrHit() {
  if (currentMode == MODE_DOCK && dockAutoActive) {
    dockIrLatched = true;
    report("DONE:DOCK_IR_HIT");
  } else {
    report("IGNORED:DOCK_IR_HIT");
  }
}

// Manual continuous control still available
void cmdDockForward() {
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_DOCK, false);
  moveForwardRaw(DOCK_SPEED);
  report("DONE:F");
}

void cmdDockBackward() {
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_DOCK, false);
  moveBackwardRaw(DOCK_SPEED);
  report("DONE:B");
}

void cmdDockLeft() {
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_DOCK, false);
  turnLeftRaw(DOCK_SPEED);
  report("DONE:L");
}

void cmdDockRight() {
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_DOCK, false);
  turnRightRaw(DOCK_SPEED);
  report("DONE:R");
}

void cmdDockStop() {
  stopMotors();
  dockAutoActive = false;
  dockState = DOCK_IDLE;
  setMode(MODE_DOCK, false);
  report("DONE:S");
}

// =========================
// Command parser
// =========================
void handleCmd(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "PING") {
    report("PONG");
  }
  else if (cmd == "PATROL_ON") {
    cmdPatrolOn();
  }
  else if (cmd == "PATROL_OFF") {
    cmdPatrolOff();
  }
  else if (cmd == "STOP") {
    cmdStop();
  }
  else if (cmd == "TURN_LEFT_90") {
    cmdTurnLeft90();
  }
  else if (cmd == "TURN_RIGHT_90") {
    cmdTurnRight90();
  }
  else if (cmd == "TURN_RIGHT_180") {
    cmdTurnRight180();
  }
  else if (cmd == "STEP_FWD_INVEST") {
    cmdStepFwdInvestigate();
  }
  else if (cmd == "DOCK_START") {
    cmdDockStart();
  }
  else if (cmd == "DOCK_ABORT") {
    cmdDockAbort();
  }
  else if (cmd == "DOCK_IR_HIT") {
    cmdDockIrHit();
  }
  else if (cmd == "F") {
    cmdDockForward();
  }
  else if (cmd == "B") {
    cmdDockBackward();
  }
  else if (cmd == "L") {
    cmdDockLeft();
  }
  else if (cmd == "R") {
    cmdDockRight();
  }
  else if (cmd == "S") {
    cmdDockStop();
  }
  else {
    report("ERR:UNKNOWN_CMD:" + cmd);
  }
}

void readSerialCmds() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (rxLine.length() > 0) {
        handleCmd(rxLine);
        rxLine = "";
      }
    } else {
      if (rxLine.length() < 50) {
        rxLine += c;
      } else {
        rxLine = "";
      }
    }
  }
}

// =========================
// Setup / loop
// =========================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A7));

  stopMotors();
  setMode(MODE_PATROL, false);
  report("BOT_ONLINE");
  report("MODE:PATROL");
}

void loop() {
  readSerialCmds();

  if (currentMode == MODE_PATROL) {
    patrolStep();
  }
  else if (currentMode == MODE_DOCK) {
    dockStep();
  }
  else {
    delay(10);
  }
}
