#include <adns2620.h> // https://github.com/ehalferty/arduino-ADNS2620
#include <TimerOne.h> // https://github.com/ehalferty/arduino-timerone
// We're actually using ADNS2610 rather than ADNS2620, registers are at a different offset:
#define CONFIGURATION_REG_2   0x00
#define DELTA_Y_REG_2         0x02
#define DELTA_X_REG_2         0x03
#define NUM_DIFFS 128
#define ADB_PIN 2
#define ADNS_SDIO_PIN 3
#define ADNS_SCK_PIN 4
#define BUTTON_PIN 5
#define SENSOR_UPDATES_PER_SECOND 20
#define MOUSE_SENSATIVITY 0.3
#define WAITING_FOR_ATTENTION 0
#define WAITING_FOR_SYNC 1
#define READING_COMMAND_BITS 2
#define READING_COMMAND_ARGS 4
#define TALK 3
#define LISTEN 2
#define SHORT_PULSE_DURATION 30
#define LONG_PULSE_DURATION 55
volatile unsigned int dx = 0, dy = 0, buttonState = 1;
volatile int haveDataToSend = 0;
volatile unsigned long diff, startTime, endTime, args;
volatile unsigned long diffs[NUM_DIFFS];
volatile unsigned int count = 0, state = WAITING_FOR_ATTENTION;
volatile unsigned char command;
volatile unsigned char myAddress = 3;
volatile int srqEnabled = 1;
volatile int handlerId = 2;
ADNS2620 mouse(ADNS_SDIO_PIN, ADNS_SCK_PIN);
void setup() {
  mouse.begin();
  delay(100);
  mouse.sync();
  mouse.write(CONFIGURATION_REG_2, 0x01);
  pinMode(ADB_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ADB_PIN), adbStateChanged, CHANGE);
  Timer1.initialize(10000);
  Timer1.stop();
  Timer1.restart();
  Timer1.detachInterrupt();
  delay(200);
}
void loop() {
  delay(1000 / SENSOR_UPDATES_PER_SECOND);
  if (digitalRead(BUTTON_PIN)) {
    buttonState = 0;
  } else {
    buttonState = 1;
  }
  dx = mouse.read(DELTA_X_REG_2) * MOUSE_SENSATIVITY;
  dy = -mouse.read(DELTA_Y_REG_2) * MOUSE_SENSATIVITY;
  haveDataToSend = 1;
}
void lowPulse(unsigned int duration) {
  pinMode(ADB_PIN, OUTPUT);
  digitalWrite(ADB_PIN, LOW);
  delayMicroseconds(duration);
  digitalWrite(ADB_PIN, HIGH);
  pinMode(ADB_PIN, INPUT_PULLUP);
}
void send(boolean b) {
  pinMode(ADB_PIN, OUTPUT);
  digitalWrite(ADB_PIN, LOW);
  delayMicroseconds(b ? SHORT_PULSE_DURATION : LONG_PULSE_DURATION);
  digitalWrite(ADB_PIN, HIGH);
  pinMode(ADB_PIN, INPUT_PULLUP);
  delayMicroseconds(b ? LONG_PULSE_DURATION : SHORT_PULSE_DURATION);
}
void sendByte(unsigned char b) {
  for (int i = 0; i < 8; i++) {
    send((b >> (7 - i)) & 1);
  }
}
void talk0() {
  delayMicroseconds(100);
  delayMicroseconds(160);
  send(1);
  unsigned char data0, data1;
  data0 = (buttonState << 7) | (dy & 0x7F);
  data1 = 0x80 | (dx & 0x7F);
  sendByte(data0);
  sendByte(data1);
  send(0);
}
void talk3() {
  delayMicroseconds(100);
  delayMicroseconds(160);
  send(1);
  unsigned char data0, data1;
  myAddress = random(4, 15);
  myAddress = 3;
  data0 = (1 << 6) | (srqEnabled << 5) | myAddress;
  data1 = handlerId;
  sendByte(data0);
  sendByte(data1);
  send(0);
}
void adbStateChanged() {
  diff = TCNT1 >> 1;
  if (state == WAITING_FOR_ATTENTION) {
    if (diff < 850 && diff > 750) {
      state = WAITING_FOR_SYNC;
    }
  } else if (state == WAITING_FOR_SYNC) {
    if (diff < 75 && diff > 55) {
      state = READING_COMMAND_BITS;
      count = 0;
      command = 0;
    } else {
      state = WAITING_FOR_ATTENTION;
    }
  } else if (state == READING_COMMAND_BITS) {
    diffs[count] = diff;
    if (count % 2 == 0 && (count / 2) < 8) {
      if (diff < 50) {
        command |= (1 << (7 - (count / 2)));
      }
    }
    count++;
    if (count >= 16) {
      int commandType = (command >> 2) & 3;
      if (commandType == TALK) {
        if ((command >> 4) == myAddress) {
          if ((command & 3) == 3) {
            talk3();
          } else if ((command & 3) == 0) {
            if (haveDataToSend) {
              talk0();
            }
          }
          haveDataToSend = 0;
          state = WAITING_FOR_ATTENTION;
        } else {
          if (haveDataToSend && srqEnabled) {
            lowPulse(200);
          }
          count = 0;
          state = WAITING_FOR_ATTENTION;
        }
      } else if (commandType == LISTEN) {
        if ((command >> 4) == myAddress) {
          count = 0;
          args = 0;
          state = READING_COMMAND_ARGS;
        } else {
          state = WAITING_FOR_ATTENTION;
        }
      }
    }
  } else if (state == READING_COMMAND_ARGS) {
    diffs[count] = diff;
    if (count > 38 || diff > 75) {
      // TODO: Handle LISTEN commands here.
      state = WAITING_FOR_ATTENTION;
    }
    count++;
  }
  TCNT1 = 0;
}
