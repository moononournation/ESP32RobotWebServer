#ifndef DEV_DEVICE_PINS
#define MOTOR_SUPPORTED
#endif

#ifdef MOTOR_SUPPORTED

#define MOTOR_CMD "MOTOR:"

#ifdef SERVO360MOTOR
#include <ESP32Servo.h>
Servo servo1;
Servo servo2;
// Published values for SG90 servos; adjust if needed
int minUs = 1000;
int maxUs = 2000;
#endif

void module_motor_init()
{
#ifdef SERVO360MOTOR
  // Allow allocation of all timers
  ESP32PWM::allocateTimer(1);
  servo1.setPeriodHertz(50); // Standard 50hz servo
  servo2.setPeriodHertz(50); // Standard 50hz servo
#else // ! SERVO360MOTOR
#if (ESP_ARDUINO_VERSION_MAJOR < 3)
  ledcSetup(1 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_L_A, 1 /* LEDChannel */);
  ledcWrite(1 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(2 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_L_B, 2 /* LEDChannel */);
  ledcWrite(2 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(3 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_R_A, 3 /* LEDChannel */);
  ledcWrite(3 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(4 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_R_B, 4 /* LEDChannel */);
  ledcWrite(4 /* LEDChannel */, 0); /* 0-255 */
#else
  ledcAttach(MOTOR_L_A, 5000, 8);
  ledcWrite(MOTOR_L_A, 0); /* 0-255 */
  ledcAttach(MOTOR_L_B, 5000, 8);
  ledcWrite(MOTOR_L_B, 0); /* 0-255 */
  ledcAttach(MOTOR_R_A, 5000, 8);
  ledcWrite(MOTOR_R_A, 0); /* 0-255 */
  ledcAttach(MOTOR_R_B, 5000, 8);
  ledcWrite(MOTOR_R_B, 0); /* 0-255 */
#endif
#endif // !SERVO360MOTOR
}

static void set_motor(char *cmd)
{
  uint8_t la = hexValue(*(cmd++));
  if (*cmd != ':')
  {
    la = (la * 16) + hexValue(*(cmd++));
  }
  cmd++; // skip seperator
  uint8_t lb = hexValue(*(cmd++));
  if (*cmd != ':')
  {
    lb = (lb * 16) + hexValue(*(cmd++));
  }
  cmd++; // skip seperator
  uint8_t ra = hexValue(*(cmd++));
  if (*cmd != ':')
  {
    ra = (ra * 16) + hexValue(*(cmd++));
  }
  cmd++; // skip seperator
  uint8_t rb = hexValue(*(cmd++));
  if (*cmd != ':')
  {
    rb = (rb * 16) + hexValue(*(cmd++));
  }

#ifdef SERVO360MOTOR
  int lv = la + 255 - lb;
  int rv = ra + 255 - rb;
#ifdef SERVO360_REVERSE
  int lAngle = map(lv, 0, 510, 179, 0);
  int rAngle = map(rv, 0, 510, 0, 179);
#else
  int lAngle = map(lv, 0, 510, 0, 179);
  int rAngle = map(rv, 0, 510, 179, 0);
#endif
  servo1.attach(SERVO360_L, minUs, maxUs);
  servo2.attach(SERVO360_R, minUs, maxUs);
  servo1.write(lAngle);
  servo2.write(rAngle);
  Serial.printf("la: %d, lb: %d, lAngle: %d, ra: %d, rb: %d, rAngle: %d\n", la, lb, lAngle, ra, rb, rAngle);
#else // !SERVO360MOTOR
#if (ESP_ARDUINO_VERSION_MAJOR < 3)
  ledcWrite(1 /* LEDChannel */, la); /* 0-255 */
  ledcWrite(2 /* LEDChannel */, lb); /* 0-255 */
  ledcWrite(3 /* LEDChannel */, ra); /* 0-255 */
  ledcWrite(4 /* LEDChannel */, rb); /* 0-255 */
#else
  ledcWrite(MOTOR_L_A, la); /* 0-255 */
  ledcWrite(MOTOR_L_B, lb); /* 0-255 */
  ledcWrite(MOTOR_R_A, ra); /* 0-255 */
  ledcWrite(MOTOR_R_B, rb); /* 0-255 */
#endif
#endif // !SERVO360MOTOR
}

#endif // MOTOR_SUPPORTED
