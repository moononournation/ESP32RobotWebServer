#ifndef DEV_DEVICE_PINS
#define I2C_SUPPORTED
#define I2C_SSD1306_ADDRESS
#endif

#ifdef I2C_SUPPORTED

#include <Wire.h>

#ifdef I2C_SSD1306_ADDRESS
#include <SSD1306.h>
SSD1306Wire display(I2C_SSD1306_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32);
#endif

void module_i2c_init()
{
  Serial.printf("Wire.begin(%d, %d);\n", I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);

#ifdef I2C_SSD1306_ADDRESS
  Serial.println("SSD1306 display.init();");
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
#ifdef WIFI_STATION
  display.drawString(128 / 2, 0, WiFi.localIP().toString());
#else  // !WIFI_STATION
  display.drawString(128 / 2, 0, AP_SSID);
#endif // !WIFI_STATION
  display.drawString(128 / 2, 16, "WiFi >> PLAY!!");
  display.display();
#endif
}

#endif // I2C_SUPPORTED
