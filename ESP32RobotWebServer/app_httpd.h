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
#include "esp_http_server.h"
#include "sdkconfig.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

#define TAG "HTTPD"

#include <FFat.h>
#include <LittleFS.h>

httpd_handle_t app_httpd = NULL;
static uint16_t hexValue(uint8_t h);
static esp_err_t parse_get(httpd_req_t *req, char **obuf);
httpd_ws_frame_t ok_reply = {
    .final = false,
    .fragmented = false,
    .type = HTTPD_WS_TYPE_TEXT,
    .payload = (uint8_t *)"OK",
    .len = 3};

#include "module_camera.h"
#include "module_gpio.h"
#include "module_i2c.h"
#include "module_motor.h"
#include "module_neopixel.h"

unsigned long last_http_activity_ms;

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

static char buf[1024];
static esp_err_t httpd_resp_send_file(httpd_req_t *req, const char *filename)
{
  last_http_activity_ms = millis();

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
  last_http_activity_ms = millis();

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

  if (strncmp((const char *)buf, GPIO_CMD, sizeof(GPIO_CMD) - 1) == 0)
  {
    module_gpio_cmd((char *)(buf + (sizeof(GPIO_CMD) - 1)));
    httpd_ws_send_frame(req, &ok_reply);
  }
#ifdef MOTOR_SUPPORTED
  else if (strncmp((const char *)buf, MOTOR_CMD, sizeof(MOTOR_CMD) - 1) == 0)
  {
    module_motor_cmd((char *)(buf + (sizeof(MOTOR_CMD) - 1)));
    httpd_ws_send_frame(req, &ok_reply);
  }
#endif
#ifdef NEOPIXEL_SUPPORTED
  else if (strncmp((const char *)buf, NEOPIXEL_CMD, sizeof(NEOPIXEL_CMD) - 1) == 0)
  {
    module_neopixel_cmd((char *)(buf + (sizeof(NEOPIXEL_CMD) - 1)));
    httpd_ws_send_frame(req, &ok_reply);
  }
#endif
  else
  {
    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
  }

  free(buf);
  return ret;
}

static esp_err_t index_handler(httpd_req_t *req)
{
  last_http_activity_ms = millis();

  httpd_resp_set_type(req, "text/html");
#if defined(CAMERA_SUPPORTED)
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL)
  {
    if (s->id.PID == OV3660_PID)
    {
      return httpd_resp_send_file(req, "/static/ov3660.htm");
    }
    else if (s->id.PID == OV5640_PID)
    {
      return httpd_resp_send_file(req, "/static/ov5640.htm");
    }
    else // if (s->id.PID == OV2640_PID)
    {
      return httpd_resp_send_file(req, "/static/ov2640.htm");
    }
  }
  else
  {
    log_e("Camera sensor not found");
    return httpd_resp_send_500(req);
  }
#elif defined(MOTOR_SUPPORTED)
  return httpd_resp_send_file(req, "/static/touchremote.htm");
#elif defined(NEOPIXEL_SUPPORTED)
  return httpd_resp_send_file(req, "/static/emojisign.htm");
#else
  return httpd_resp_send_file(req, "/static/index.htm");
#endif
}

static esp_err_t static_handler(httpd_req_t *req)
{
  last_http_activity_ms = millis();
  const char *p = req->uri;
  p += strlen(p) - 3;
  if (strcmp(p, "ico") == 0)
  {
    httpd_resp_set_type(req, "image/vnd.microsoft.icon");
  }
  else if (strcmp(p, "png") == 0)
  {
    httpd_resp_set_type(req, "image/png");
  }
  else if (strcmp(p, "svg") == 0)
  {
    httpd_resp_set_type(req, "image/svg+xml");
  }
  else
  {
    httpd_resp_set_type(req, "text/html");
  }
  return httpd_resp_send_file(req, req->uri);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
  last_http_activity_ms = millis();

  httpd_resp_set_type(req, "image/vnd.microsoft.icon");
  return httpd_resp_send_file(req, "/static/favicon.ico");
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t)
{
  last_http_activity_ms = millis();

#if defined(CAMERA_SUPPORTED)
  httpd_resp_set_hdr(req, "Location", "/static/camerarobot.htm");
#elif defined(MOTOR_SUPPORTED)
  httpd_resp_set_hdr(req, "Location", "/static/touchremote.htm");
#elif defined(NEOPIXEL_SUPPORTED)
  httpd_resp_set_hdr(req, "Location", "/static/emojisign.htm");
#else
  httpd_resp_set_hdr(req, "Location", "/static/index.htm");
#endif
  httpd_resp_set_status(req, "302");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "Redirect HTML", 13);
}

void start_http_server()
{
  // if (!FFat.begin(false, "/static"))
  if (!LittleFS.begin(false, "/static"))
  {
    Serial.println("ERROR: File system mount failed!");
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 32;
  config.uri_match_fn = httpd_uri_match_wildcard;

  httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = ws_handler,
      .user_ctx = NULL,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = "arduino"};

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL,
      .is_websocket = false};

  httpd_uri_t static_uri = {
      .uri = "/static/*",
      .method = HTTP_GET,
      .handler = static_handler,
      .user_ctx = NULL,
      .is_websocket = false};

  httpd_uri_t favicon_uri = {
      .uri = "/favicon.ico",
      .method = HTTP_GET,
      .handler = favicon_handler,
      .user_ctx = NULL,
      .is_websocket = false};

  log_i("Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&app_httpd, &config) == ESP_OK)
  {
#ifdef CAMERA_SUPPORTED
    module_camera_httpd_reg();
#endif // CAMERA_SUPPORTED

    module_gpio_httpd_reg();

    httpd_register_uri_handler(app_httpd, &ws_uri);

    httpd_register_uri_handler(app_httpd, &index_uri);
    httpd_register_uri_handler(app_httpd, &favicon_uri);
    httpd_register_uri_handler(app_httpd, &static_uri);

    httpd_register_err_handler(app_httpd, HTTPD_404_NOT_FOUND, &not_found_handler);
  }

  last_http_activity_ms = millis();
}
