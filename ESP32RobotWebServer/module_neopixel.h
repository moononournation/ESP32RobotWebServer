#ifndef DEV_DEVICE_PINS
#define NEOPIXEL_SUPPORTED
#endif

#ifdef NEOPIXEL_SUPPORTED

#define NEOPIXEL_NUMS (NEOPIXEL_WIDTH * NEOPIXEL_HEIGHT)
#include <FastLED.h>
CRGB neopixels[NEOPIXEL_NUMS];

#define NEOPIXEL_CMD "NEOPIXEL:"

void module_neopixel_init()
{
  FastLED.addLeds<WS2812, NEOPIXEL_PIN, GRB>(neopixels, NEOPIXEL_NUMS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(NEOPIXEL_DEFAULT_BRIGHTNESS); // 1-255
}

static void module_neopixel_cmd(char *cmd)
{
  int i = NEOPIXEL_NUMS;
  while (i--)
  {
    gpio = hexValue(*(cmd++));
  }
}

#endif // NEOPIXEL_SUPPORTED
