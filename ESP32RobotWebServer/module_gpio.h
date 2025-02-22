#define GPIO_CMD "GPIO:"
#define LED_STR "LED"

void module_gpio_init()
{
#ifdef LED_A
  pinMode(LED_A, OUTPUT);
#endif
}

static void set_gpio(char *cmd)
{
  uint8_t gpio, val;
#ifdef LED_A
  if (strncmp((const char *)cmd, LED_STR, sizeof(LED_STR)))
  {
    gpio = LED_A;
    cmd += sizeof(LED_STR);
  }
  else
#endif
  {
    gpio = hexValue(*(cmd++));
    if (*cmd != ':')
    {
      gpio = (gpio * 16) + hexValue(*(cmd++));
    }
    cmd++; // skip seperator
  }
  val = hexValue(*(cmd++));

  digitalWrite(gpio, val);
}

static esp_err_t gpio_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char cmd[16];
  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  set_gpio(cmd);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

void module_gpio_httpd_reg()
{
  httpd_uri_t gpio_uri = {
      .uri = "/gpio",
      .method = HTTP_GET,
      .handler = gpio_handler,
      .user_ctx = NULL,
      .is_websocket = false};

  httpd_register_uri_handler(app_httpd, &gpio_uri);
}
