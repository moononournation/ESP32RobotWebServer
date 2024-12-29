// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifdef CAMERA_SUPPORTED
#include <esp_camera.h>
#endif

#include "esp_http_server.h"
#include "sdkconfig.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

#define TAG "HTTPD"

#define MOTORCMD "MOTOR:"
#if defined(SERVO360MOTOR)
#include <ESP32Servo.h>
Servo servo1;
Servo servo2;
// Published values for SG90 servos; adjust if needed
int minUs = 1000;
int maxUs = 2000;
#elif defined(MOTOR)
#endif

typedef struct
{
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static uint16_t hexValue(uint8_t h)
{
  if (h >= '0' && (h <= '9'))
  {
    return h - '0';
  }
  else if ((h >= 'A') && (h <= 'F'))
  {
    return 10 + h - 'A';
  }
  else if ((h >= 'a') && (h <= 'f'))
  {
    return 10 + h - 'a';
  }
  return 0;
}

static void setMotor(uint8_t la, uint8_t lb, uint8_t ra, uint8_t rb)
{
  int lv = la + 255 - lb;
  int rv = ra + 255 - rb;
#if defined(SERVO360MOTOR)
#ifdef SERVO360_REVERSE
  int lAngle = map(lv, 0, 510, 179, 0);
  int rAngle = map(rv, 0, 510, 0, 179);
#else
  int lAngle = map(lv, 0, 510, 0, 179);
  int rAngle = map(rv, 0, 510, 179, 0);
#endif
  servo1.attach(SERVO360_L_PIN, minUs, maxUs);
  servo2.attach(SERVO360_R_PIN, minUs, maxUs);
  servo1.write(lAngle);
  servo2.write(rAngle);
  Serial.printf("la: %d, lb: %d, lAngle: %d, ra: %d, rb: %d, rAngle: %d\n", la, lb, lAngle, ra, rb, rAngle);
#elif defined(MOTOR)
#if (ESP_ARDUINO_VERSION_MAJOR < 3)
  ledcWrite(1 /* LEDChannel */, la); /* 0-255 */
  ledcWrite(2 /* LEDChannel */, lb); /* 0-255 */
  ledcWrite(3 /* LEDChannel */, ra); /* 0-255 */
  ledcWrite(4 /* LEDChannel */, rb); /* 0-255 */
#else
  ledcWrite(MOTOR_L_A_PIN, la); /* 0-255 */
  ledcWrite(MOTOR_L_B_PIN, lb); /* 0-255 */
  ledcWrite(MOTOR_R_A_PIN, ra); /* 0-255 */
  ledcWrite(MOTOR_R_B_PIN, rb); /* 0-255 */
#endif
#endif
}

#ifdef CAMERA_SUPPORTED
static esp_err_t bmp_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_start = esp_timer_get_time();
#endif
  fb = esp_camera_fb_get();
  if (!fb)
  {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted)
  {
    log_e("BMP Conversion failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_end = esp_timer_get_time();
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len);
#endif
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index)
  {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
  {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif

  fb = esp_camera_fb_get();

  if (!fb)
  {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  size_t fb_len = 0;
#endif
  if (fb->format == PIXFORMAT_JPEG)
  {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = fb->len;
#endif
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  }
  else
  {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = jchunk.len;
#endif
  }
  esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_end = esp_timer_get_time();
  log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
#endif
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  static int64_t last_frame = 0;
  if (!last_frame)
  {
    last_frame = esp_timer_get_time();
  }
#endif
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
  {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  while (true)
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      log_e("Camera capture failed");
      res = ESP_FAIL;
    }
    else
    {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG)
      {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted)
        {
          log_e("JPEG compression failed");
          res = ESP_FAIL;
        }
      }
      else
      {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK)
    {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (fb)
    {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    }
    else if (_jpg_buf)
    {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK)
    {
      log_e("Send frame failed");
      break;
    }
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    frame_time /= 1000;
    log_i("MJPG: %uB %ums (%.1ffps)",
          (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    last_frame = fr_end;
#endif
  }

  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1)
  {
    buf = (char *)malloc(buf_len);
    if (!buf)
    {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
    {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int val = atoi(value);
  log_i("%s = %d", variable, val);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize"))
  {
    if (s->pixformat == PIXFORMAT_JPEG)
    {
      res = s->set_framesize(s, (framesize_t)val);
    }
  }
  else if (!strcmp(variable, "quality"))
  {
    res = s->set_quality(s, val);
  }
  else if (!strcmp(variable, "contrast"))
  {
    res = s->set_contrast(s, val);
  }
  else if (!strcmp(variable, "brightness"))
  {
    res = s->set_brightness(s, val);
  }
  else if (!strcmp(variable, "saturation"))
  {
    res = s->set_saturation(s, val);
  }
  else if (!strcmp(variable, "gainceiling"))
  {
    res = s->set_gainceiling(s, (gainceiling_t)val);
  }
  else if (!strcmp(variable, "colorbar"))
  {
    res = s->set_colorbar(s, val);
  }
  else if (!strcmp(variable, "awb"))
  {
    res = s->set_whitebal(s, val);
  }
  else if (!strcmp(variable, "agc"))
  {
    res = s->set_gain_ctrl(s, val);
  }
  else if (!strcmp(variable, "aec"))
  {
    res = s->set_exposure_ctrl(s, val);
  }
  else if (!strcmp(variable, "hmirror"))
  {
    res = s->set_hmirror(s, val);
  }
  else if (!strcmp(variable, "vflip"))
  {
    res = s->set_vflip(s, val);
  }
  else if (!strcmp(variable, "awb_gain"))
  {
    res = s->set_awb_gain(s, val);
  }
  else if (!strcmp(variable, "agc_gain"))
  {
    res = s->set_agc_gain(s, val);
  }
  else if (!strcmp(variable, "aec_value"))
  {
    res = s->set_aec_value(s, val);
  }
  else if (!strcmp(variable, "aec2"))
  {
    res = s->set_aec2(s, val);
  }
  else if (!strcmp(variable, "dcw"))
  {
    res = s->set_dcw(s, val);
  }
  else if (!strcmp(variable, "bpc"))
  {
    res = s->set_bpc(s, val);
  }
  else if (!strcmp(variable, "wpc"))
  {
    res = s->set_wpc(s, val);
  }
  else if (!strcmp(variable, "raw_gma"))
  {
    res = s->set_raw_gma(s, val);
  }
  else if (!strcmp(variable, "lenc"))
  {
    res = s->set_lenc(s, val);
  }
  else if (!strcmp(variable, "special_effect"))
  {
    res = s->set_special_effect(s, val);
  }
  else if (!strcmp(variable, "wb_mode"))
  {
    res = s->set_wb_mode(s, val);
  }
  else if (!strcmp(variable, "ae_level"))
  {
    res = s->set_ae_level(s, val);
  }
  else
  {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask)
{
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req)
{
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';

  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID)
  {
    for (int reg = 0x3400; reg < 0x3406; reg += 2)
    {
      p += print_reg(p, s, reg, 0xFFF); // 12 bit
    }
    p += print_reg(p, s, 0x3406, 0xFF);

    p += print_reg(p, s, 0x3500, 0xFFFF0); // 16 bit
    p += print_reg(p, s, 0x3503, 0xFF);
    p += print_reg(p, s, 0x350a, 0x3FF);  // 10 bit
    p += print_reg(p, s, 0x350c, 0xFFFF); // 16 bit

    for (int reg = 0x5480; reg <= 0x5490; reg++)
    {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5380; reg <= 0x538b; reg++)
    {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5580; reg < 0x558a; reg++)
    {
      p += print_reg(p, s, reg, 0xFF);
    }
    p += print_reg(p, s, 0x558a, 0x1FF); // 9 bit
  }
  else if (s->id.PID == OV2640_PID)
  {
    p += print_reg(p, s, 0xd3, 0xFF);
    p += print_reg(p, s, 0x111, 0xFF);
    p += print_reg(p, s, 0x132, 0xFF);
  }

  p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
  p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
  p += sprintf(p, ",\"led_intensity\":%d", -1);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t xclk_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _xclk[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _reg[32];
  char _mask[32];
  char _val[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK || httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _reg[32];
  char _mask[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->get_reg(s, reg, mask);
  if (res < 0)
  {
    return httpd_resp_send_500(req);
  }
  log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

  char buffer[20];
  const char *val = itoa(res, buffer, 10);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int def)
{
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK)
  {
    return def;
  }
  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req)
{
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul = parse_get_var(buf, "mul", 0);
  int sys = parse_get_var(buf, "sys", 0);
  int root = parse_get_var(buf, "root", 0);
  int pre = parse_get_var(buf, "pre", 0);
  int seld5 = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req)
{
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }

  int startX = parse_get_var(buf, "sx", 0);
  int startY = parse_get_var(buf, "sy", 0);
  int endX = parse_get_var(buf, "ex", 0);
  int endY = parse_get_var(buf, "ey", 0);
  int offsetX = parse_get_var(buf, "offx", 0);
  int offsetY = parse_get_var(buf, "offy", 0);
  int totalX = parse_get_var(buf, "tx", 0);
  int totalY = parse_get_var(buf, "ty", 0); // codespell:ignore totaly
  int outputX = parse_get_var(buf, "ox", 0);
  int outputY = parse_get_var(buf, "oy", 0);
  bool scale = parse_get_var(buf, "scale", 0) == 1;
  bool binning = parse_get_var(buf, "binning", 0) == 1;
  free(buf);

  log_i(
      "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
      totalX, totalY, outputX, outputY, scale, binning // codespell:ignore totaly
  );
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning); // codespell:ignore totaly
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}
#endif // CAMERA_SUPPORTED

static char buf[1024];
static esp_err_t httpd_resp_send_file(httpd_req_t *req, const char *filename)
{
  log_i("httpd_resp_send_file: %s", filename);
  FILE *f = fopen(filename, "r");
  size_t r = fread(buf, 1, sizeof(buf), f);
  while (r)
  {
    httpd_resp_send_chunk(req, buf, r);
    r = fread(buf, 1, sizeof(buf), f);
  }
  fclose(f);
  return httpd_resp_send_chunk(req, buf, 0);
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
  if (req->method == HTTP_GET)
  {
    ESP_LOGI(TAG, "Handshake done, the new connection was opened");
    return ESP_OK;
  }
  httpd_ws_frame_t ws_pkt;
  uint8_t *buf = NULL;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  /* Set max_len = 0 to get the frame len */
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
    return ret;
  }
  ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
  if (ws_pkt.len)
  {
    /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
    buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
    if (buf == NULL)
    {
      ESP_LOGE(TAG, "Failed to calloc memory for buf");
      return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    /* Set max_len = ws_pkt.len to get the frame payload */
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
      free(buf);
      return ret;
    }
    ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
  }
  ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);

  ret = httpd_ws_send_frame(req, &ws_pkt);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
  }

  uint8_t *p = buf + sizeof(MOTORCMD) - 1;
  uint8_t la = hexValue(*(p++));
  if (*p != ':')
  {
    la = (la * 16) + hexValue(*(p++));
  }
  p++; // skip seperator
  uint8_t lb = hexValue(*(p++));
  if (*p != ':')
  {
    lb = (lb * 16) + hexValue(*(p++));
  }
  p++; // skip seperator
  uint8_t ra = hexValue(*(p++));
  if (*p != ':')
  {
    ra = (ra * 16) + hexValue(*(p++));
  }
  p++; // skip seperator
  uint8_t rb = hexValue(*(p++));
  if (*p != ':')
  {
    rb = (rb * 16) + hexValue(*(p++));
  }
  setMotor(la, lb, ra, rb);

  free(buf);
  return ret;
}

static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
#ifdef CAMERA_SUPPORTED
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL)
  {
    if (s->id.PID == OV3660_PID)
    {
      return httpd_resp_send_file(req, "/root/ov3660.htm");
    }
    else if (s->id.PID == OV5640_PID)
    {
      return httpd_resp_send_file(req, "/root/ov5640.htm");
    }
    else // if (s->id.PID == OV2640_PID)
    {
      return httpd_resp_send_file(req, "/root/ov2640.htm");
    }
  }
  else
  {
    log_e("Camera sensor not found");
    return httpd_resp_send_500(req);
  }
#else
  return httpd_resp_send_file(req, "/root/touchremote.htm");
#endif
}

static esp_err_t arrows_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "image/svg+xml");
  return httpd_resp_send_file(req, "/root/arrows.svg");
}

static esp_err_t camerarobot_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send_file(req, "/root/camerarobot.htm");
}

static esp_err_t echo_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send_file(req, "/root/echo.htm");
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "image/vnd.microsoft.icon");
  return httpd_resp_send_file(req, "/root/favicon.ico");
}

static esp_err_t touchremote_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send_file(req, "/root/touchremote.htm");
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t)
{
#ifdef CAMERA_SUPPORTED
  httpd_resp_set_hdr(req, "Location", "/camerarobot.htm");
#else
  httpd_resp_set_hdr(req, "Location", "/touchremote.htm");
#endif
  httpd_resp_set_status(req, "302");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "Redirect HTML", 13);
}

void startCameraServer()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 32;

  httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = ws_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = "arduino"
#endif
  };

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t arrows_uri = {
      .uri = "/arrows.svg",
      .method = HTTP_GET,
      .handler = arrows_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t camerarobot_uri = {
      .uri = "/camerarobot.htm",
      .method = HTTP_GET,
      .handler = camerarobot_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t echo_uri = {
      .uri = "/echo.htm",
      .method = HTTP_GET,
      .handler = echo_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t favicon_uri = {
      .uri = "/favicon.ico",
      .method = HTTP_GET,
      .handler = favicon_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t touchremote_uri = {
      .uri = "/touchremote.htm",
      .method = HTTP_GET,
      .handler = touchremote_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

#ifdef CAMERA_SUPPORTED
  httpd_uri_t cmd_uri = {
      .uri = "/control",
      .method = HTTP_GET,
      .handler = cmd_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t status_uri = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t capture_uri = {
      .uri = "/capture",
      .method = HTTP_GET,
      .handler = capture_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t bmp_uri = {
      .uri = "/bmp",
      .method = HTTP_GET,
      .handler = bmp_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t xclk_uri = {
      .uri = "/xclk",
      .method = HTTP_GET,
      .handler = xclk_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t reg_uri = {
      .uri = "/reg",
      .method = HTTP_GET,
      .handler = reg_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t greg_uri = {
      .uri = "/greg",
      .method = HTTP_GET,
      .handler = greg_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t pll_uri = {
      .uri = "/pll",
      .method = HTTP_GET,
      .handler = pll_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t win_uri = {
      .uri = "/resolution",
      .method = HTTP_GET,
      .handler = win_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };
#endif // CAMERA_SUPPORTED

  log_i("Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
  {
    httpd_register_uri_handler(camera_httpd, &ws_uri);

    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &arrows_uri);
    httpd_register_uri_handler(camera_httpd, &camerarobot_uri);
    httpd_register_uri_handler(camera_httpd, &echo_uri);
    httpd_register_uri_handler(camera_httpd, &favicon_uri);
    httpd_register_uri_handler(camera_httpd, &touchremote_uri);

#ifdef CAMERA_SUPPORTED
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &bmp_uri);

    httpd_register_uri_handler(camera_httpd, &xclk_uri);
    httpd_register_uri_handler(camera_httpd, &reg_uri);
    httpd_register_uri_handler(camera_httpd, &greg_uri);
    httpd_register_uri_handler(camera_httpd, &pll_uri);
    httpd_register_uri_handler(camera_httpd, &win_uri);
#endif // CAMERA_SUPPORTED

    httpd_register_err_handler(camera_httpd, HTTPD_404_NOT_FOUND, &not_found_handler);
  }

#ifdef CAMERA_SUPPORTED
  config.server_port += 1;
  config.ctrl_port += 1;
  log_i("Starting stream server on port: '%d'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK)
  {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
#endif // CAMERA_SUPPORTED
}
