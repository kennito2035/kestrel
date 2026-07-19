/*
 * Kestrel RP2350 firmware, Arduino IDE version.
 *
 * Functionally identical to the pico-sdk firmware in rp2350/src/:
 *   core 0 (setup/loop):   PIR pre-screen -> H750 wake pulse
 *   core 1 (setup1/loop1): UART detection events -> servo strike
 * Plus a bring-up aid: every H750 UART line is echoed verbatim to the
 * USB serial port (115200), so the board doubles as a UART-to-USB
 * bridge for capturing the H750's CSV telemetry; PIR wake pulses are
 * logged there too as "# PIR wake pulse <n>".
 *
 * Setup:
 *   1. Install the arduino-pico core (Earle Philhower):
 *      File > Preferences > Additional Board Manager URLs:
 *      https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
 *      then Boards Manager > install "Raspberry Pi Pico/RP2040/RP2350"
 *   2. Tools > Board > "Generic RP2350", Flash Size: 16MB
 *   3. First flash: hold BOOT while plugging in, then Upload.
 *      Later flashes: Upload works directly over USB.
 *
 * Wiring: see the repository README (pin choices constrained by the
 * mini board's breakout; GP16-19 are not exposed).
 *
 * License: MIT (see repository root).
 */
#include <Servo.h>

const int PIN_SERVO = 2;     // SG90 signal
const int PIN_PIR = 14;      // HC-SR501 OUT, rising edge on motion
const int PIN_WAKE_OUT = 15; // -> H750 PC0 (EXTI wake)

const int WAKE_PULSE_MS = 50;
const int RETRIGGER_HOLD_MS = 2000; // ignore PIR re-fires while H750 wakes
const int SERVO_REST_US = 1000;
const int SERVO_STRIKE_US = 2000;
const int STRIKE_HOLD_MS = 600;

// ---------- Core 0: PIR pre-screen ----------

volatile bool pirFired = false;
volatile uint32_t pirWakeCount = 0; // read by core 1 for USB logging

void pirIsr() { pirFired = true; }

void setup() {
  pinMode(PIN_WAKE_OUT, OUTPUT);
  digitalWrite(PIN_WAKE_OUT, LOW);
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), pirIsr, RISING);
}

void loop() {
  if (pirFired) {
    pirFired = false;
    digitalWrite(PIN_WAKE_OUT, HIGH);
    delay(WAKE_PULSE_MS);
    digitalWrite(PIN_WAKE_OUT, LOW);
    pirWakeCount = pirWakeCount + 1;
    delay(RETRIGGER_HOLD_MS);
  }
  delay(1);
}

// ---------- Core 1: detection events -> servo ----------
// Protocol from the H750 (one ASCII line per event):
//   DET,<class>,<confidence_pct>    e.g. DET,person,91
//   GATE,<OPEN|CLOSED>,<changed_px> (telemetry, ignored here)

Servo servo;
String line;
uint32_t pirWakePrinted = 0;

void setup1() {
  Serial.begin(115200);  // USB CDC: transparent echo of H750 telemetry
  Serial1.begin(115200); // UART0: TX=GP0, RX=GP1 (arduino-pico defaults)
  servo.attach(PIN_SERVO, 500, 2500);
  servo.writeMicroseconds(SERVO_REST_US);
  line.reserve(64);
}

void loop1() {
  // All USB prints happen on this core only (no cross-core interleaving).
  // H750 lines pass through verbatim, so capturing the Serial Monitor
  // output doubles as the gate_results.csv evidence capture.
  if (pirWakeCount != pirWakePrinted) {
    pirWakePrinted = pirWakeCount;
    Serial.printf("# PIR wake pulse %lu\n", (unsigned long)pirWakePrinted);
  }
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        Serial.println(line); // verbatim echo to USB
      }
      if (line.startsWith("DET,person,")) {
        Serial.println("# strike");
        servo.writeMicroseconds(SERVO_STRIKE_US); // the kestrel's dive
        delay(STRIKE_HOLD_MS);
        servo.writeMicroseconds(SERVO_REST_US);
      }
      line = "";
    } else if (line.length() < 63) {
      line += c;
    } else {
      line = ""; // overlong line: discard and resync
    }
  }
}
