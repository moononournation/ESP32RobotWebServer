// Set these to your desired credentials.
// const char *ssid = "Strider Walker V8";
const char *ssid = "Zigbot";
const char *password = "";

// Uncomment 1 and only 1 hardware config header file
// #include "TTGO_T-JOURNAL_ROBOT.h"
#include "SEEEDSTUDIO_ZIGBOT.h"

#include "app_httpd.h"

#include <Wire.h>

#ifdef CAMERA
#include <esp_camera.h>
#endif

#ifdef I2C_SSD1306_ADDRESS
#include "SSD1306.h"
SSD1306Wire display(I2C_SSD1306_ADDRESS, I2C_SDA_NUM, I2C_SCL_NUM, GEOMETRY_128_32);
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

#ifdef I2C_SDA_NUM
  Serial.printf("Wire.begin(%d, %d);\n", I2C_SDA_NUM, I2C_SCL_NUM);
  Wire.begin(I2C_SDA_NUM, I2C_SCL_NUM);
#endif
#ifdef I2C_SSD1306_ADDRESS
  Serial.println("display.init();");
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(128 / 2, 0, ssid);
  display.drawString(128 / 2, 16, "WiFi >> PLAY!!");
  display.display();
#endif

#ifdef CAMERA
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
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
    s->set_vflip(s, 1); // flip it back
    // s->set_brightness(s, 1); // up the brightness just a bit
    // s->set_saturation(s, -2); // lower the saturation
    s->set_saturation(s, 3);
    s->set_sharpness(s, 3);
  }
  else
  {
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

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif
#endif // CAMERA

#if defined(SERVO360MOTOR)
  // Allow allocation of all timers
  ESP32PWM::allocateTimer(1);
  servo1.setPeriodHertz(50); // Standard 50hz servo
  servo2.setPeriodHertz(50); // Standard 50hz servo
#elif defined(MOTOR)
#if (ESP_ARDUINO_VERSION_MAJOR < 3)
  ledcSetup(1 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_L_A_PIN, 1 /* LEDChannel */);
  ledcWrite(1 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(2 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_L_B_PIN, 2 /* LEDChannel */);
  ledcWrite(2 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(3 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_R_A_PIN, 3 /* LEDChannel */);
  ledcWrite(3 /* LEDChannel */, 0); /* 0-255 */
  ledcSetup(4 /* LEDChannel */, 5000 /* freq */, 8 /* resolution */);
  ledcAttachPin(MOTOR_R_B_PIN, 4 /* LEDChannel */);
  ledcWrite(4 /* LEDChannel */, 0); /* 0-255 */
#else
  ledcAttach(MOTOR_L_A_PIN, 5000, 8);
  ledcWrite(MOTOR_L_A_PIN, 0); /* 0-255 */
  ledcAttach(MOTOR_L_B_PIN, 5000, 8);
  ledcWrite(MOTOR_L_B_PIN, 0); /* 0-255 */
  ledcAttach(MOTOR_R_A_PIN, 5000, 8);
  ledcWrite(MOTOR_R_A_PIN, 0); /* 0-255 */
  ledcAttach(MOTOR_R_B_PIN, 5000, 8);
  ledcWrite(MOTOR_R_B_PIN, 0); /* 0-255 */
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
