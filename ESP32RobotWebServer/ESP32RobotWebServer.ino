// ===========================
// Set WiFi softAP credentials
// ===========================
// const char *ssid = "Strider Walker V8";
const char *ssid = "JSZWY_CYIS";
// const char *ssid = "Zigbot";
const char *password = "";

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
// #include <PINS_ESP32-S3-CAM.h>
#include <PINS_JSZWY_CYIS.h>
// #include <PINS_T-JOURNAL_ROBOT.h>
// #include <PINS_XIAO_ESP32C6_ZIGBOT.h>

#include "app_httpd.h"

#include <Wire.h>

#ifdef CAMERA_SUPPORTED
#include <esp_camera.h>
#endif

#ifdef I2C_SSD1306_ADDRESS
#include "SSD1306.h"
SSD1306Wire display(I2C_SSD1306_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32);
#endif

#include <FFat.h>
#include <LittleFS.h>

#include <WiFi.h>
#include <WiFiAP.h>
#include <DNSServer.h>

DNSServer dnsServer;

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

void startCameraServer();

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

#ifdef I2C_SUPPORTED
  Serial.printf("Wire.begin(%d, %d);\n", I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);
#endif
#ifdef I2C_SSD1306_ADDRESS
  Serial.println("SSD1306 display.init();");
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(128 / 2, 0, ssid);
  display.drawString(128 / 2, 16, "WiFi >> PLAY!!");
  display.display();
#endif

#ifdef CAMERA_SUPPORTED
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAMERA_Y2;
  config.pin_d1 = CAMERA_Y3;
  config.pin_d2 = CAMERA_Y4;
  config.pin_d3 = CAMERA_Y5;
  config.pin_d4 = CAMERA_Y6;
  config.pin_d5 = CAMERA_Y7;
  config.pin_d6 = CAMERA_Y8;
  config.pin_d7 = CAMERA_Y9;
  config.pin_xclk = CAMERA_XCLK;
  config.pin_pclk = CAMERA_PCLK;
  config.pin_vsync = CAMERA_VSYNC;
  config.pin_href = CAMERA_HREF;
  config.pin_sccb_sda = CAMERA_SIOD;
  config.pin_sccb_scl = CAMERA_SIOC;
  config.pin_pwdn = CAMERA_PWDN;
  config.pin_reset = CAMERA_RST;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  // config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    if (psramFound())
    {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_QVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
      config.jpeg_quality = 12;
      config.fb_count = 2;
    }
  }
  else
  {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  Serial.println("esp_camera_init();");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    Serial.println("Set OV3660");
    s->set_vflip(s, 1); // flip it back
    // s->set_brightness(s, 1); // up the brightness just a bit
    // s->set_saturation(s, -2); // lower the saturation
    s->set_saturation(s, 3);
    s->set_sharpness(s, 3);
  }
  else
  {
    Serial.println("Set non-OV3660");
    // s->set_brightness(s, 2);
    // s->set_contrast(s, 2);
    s->set_saturation(s, 2);
    s->set_aec2(s, true);
    s->set_gainceiling(s, GAINCEILING_128X);
    s->set_lenc(s, true);
  }

  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  Serial.println("Set CAMERA_MODEL_M5STACK_*");
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  Serial.println("Set CAMERA_MODEL_ESP32S3_EYE");
  s->set_vflip(s, 1);
#endif
#endif // CAMERA_SUPPORTED

#if defined(SERVO360MOTOR)
  // Allow allocation of all timers
  ESP32PWM::allocateTimer(1);
  servo1.setPeriodHertz(50); // Standard 50hz servo
  servo2.setPeriodHertz(50); // Standard 50hz servo
#elif defined(MOTOR)
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
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // by default DNSServer is started serving any "*" domain name. It will reply
  // AccessPoint's IP to all DNS request (this is required for Captive Portal detection)
  if (dnsServer.start())
  {
    Serial.println("Started DNS server in captive portal-mode");
  }
  else
  {
    Serial.println("Err: Can't start DNS server!");
  }

  // if (!FFat.begin(false, "/root"))
  if (!LittleFS.begin(false, "/root"))
  {
    Serial.println("ERROR: File system mount failed!");
  }

  startCameraServer();

  Serial.println("Camera Ready! Connect AP and use 'http://192.168.4.1' to connect");
}

void loop()
{
  // Do nothing. Everything is done in another task by the web server
  delay(10000);

  // force deep sleep after 5 minutes
  if (millis() > 5 * 60 * 1000)
  {
    esp_deep_sleep_start();
  }
}
