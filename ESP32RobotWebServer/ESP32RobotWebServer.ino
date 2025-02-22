// ===========================
// Set WiFi softAP credentials
// ===========================
// #define WIFI_STATION
#ifdef WIFI_STATION
const char *STA_SSID = "YourAP";
const char *STA_PASSWORD = "PleaseInputYourPasswordHere";
#else  // !WIFI_STATION
// const char *AP_SSID = "Emoji Sign";
const char *AP_SSID = "JSZWY_CYIS";
// const char *AP_SSID = "Strider Walker V8";
// const char *AP_SSID = "Zigbot";
const char *AP_PASSWORD = "";
#endif // !WIFI_STATION

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
// #include <PINS_ESP32-S3-CAM.h>
// #include <PINS_ESP32-S3-MATRIX.h>
#include <PINS_JSZWY_CYIS.h>
// #include <PINS_JSZWY_CYIS_V2.h>
// #include <PINS_T-JOURNAL_ROBOT.h>
// #include <PINS_XIAO_ESP32C6_ZIGBOT.h>

#include <WiFi.h>

#include "app_httpd.h"

#ifndef WIFI_STATION
#include <DNSServer.h>
DNSServer dnsServer;
#endif

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

#ifdef WIFI_STATION
  WiFi.begin(STA_SSID, STA_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
#else  // !WIFI_STATION
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
#endif // !WIFI_STATION

  module_gpio_init();

#ifdef I2C_SUPPORTED
  module_i2c_init();
#endif

#ifdef CAMERA_SUPPORTED
  module_camera_init();
#endif

#ifdef MOTOR_SUPPORTED
  module_motor_init();
#endif

#ifdef NEOPIXEL
  module_neopixel_init();
#endif

#ifndef WIFI_STATION
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
#endif

  start_http_server();

#ifdef CAMERA_SUPPORTED
  module_camera_start_stream_server();
#endif // CAMERA_SUPPORTED

  Serial.println("HTTP server ready! Connect AP and use 'http://192.168.4.1' to connect");
}

void loop()
{
  // Do nothing. Everything is done in another task by the web server
  delay(1000);

  // force deep sleep after 5 minutes of last HTTP activity
  if ((millis() - last_http_activity_ms) > (5 * 60 * 1000))
  {
    esp_deep_sleep_start();
  }
}
