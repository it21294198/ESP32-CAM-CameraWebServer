#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>

// Select camera model
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
#include "camera_pins.h"

// WiFi credentials
#include "secret.h"
const char *ssid = USER_NAME;
const char *password = PASSWORD;

// Control pins (choose pins that are safe to use)
#define PIN4 4   // GPIO2 (built-in LED, can be repurposed)

// Stream settings
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
  }
  return res;
}

// Pin control handler
static esp_err_t cmd_handler(httpd_req_t *req) {
  char* buf;
  size_t buf_len;
  char pin[5];
  char value[5];
  int pinNum, pinValue;
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "pin", pin, sizeof(pin)) == ESP_OK &&
          httpd_query_key_value(buf, "value", value, sizeof(value)) == ESP_OK) {
        pinNum = atoi(pin);
        pinValue = atoi(value);
        
        // Set the pin value
        switch (pinNum) {
          case 4: digitalWrite(PIN4, pinValue); break;
          default: break;
        }
      }
    }
    free(buf);
  }
  
  // Redirect back to main page
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Main page handler with camera stream and control buttons
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  
  const char* html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>ESP32-CAM Control</title>"
      "<style>"
        "body{font-family:Arial;text-align:center;margin:0;padding:10px}"
        "img{width:100%;max-width:800px;height:auto}"
        ".button{display:inline-block;background-color:#4CAF50;border:none;color:white;padding:10px 20px;"
        "text-align:center;text-decoration:none;font-size:16px;margin:2px;cursor:pointer;border-radius:4px}"
        ".button-off{background-color:#555555}"
        ".control-box{margin:5px;padding:5px;border:1px solid #ccc;border-radius:5px}"
      "</style>"
    "</head>"
    "<body>"
      "<h1>ESP32-CAM Stream & Control</h1>"
      "<img src='/stream' alt='Loading Video...'>"
      "<div class='control-box'>"
        "<h3>GPIO Controls</h3>"
        "<div>"
          "<a class='button' href='/control?pin=4&value=1'>PIN4 ON</a>"
          "<a class='button button-off' href='/control?pin=4&value=0'>PIN4 OFF</a>"
        "</div>"
      "</div>"
    "</body>"
    "</html>";
  
  return httpd_resp_send(req, html, strlen(html));
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  
  // Index page
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };
  
  // Video stream
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };
  
  // Control endpoint
  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };
  
  Serial.println("Starting web server on port: 80");
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    httpd_register_uri_handler(stream_httpd, &cmd_uri);
  }
}

void setup() {
  Serial.begin(9600);
  
  // Initialize pins
  pinMode(PIN4, OUTPUT);
  
  // Initial state - all OFF
  digitalWrite(PIN4, LOW);
  
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 4;
  config.fb_count = 1;
  
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 4;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
  }
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  // Configure camera settings
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 0);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  s->set_framesize(s, FRAMESIZE_QVGA);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Start camera & web server
  startCameraServer();
  
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  delay(1000);
}