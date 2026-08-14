// ESP32-CAM 简单画面源（OV2640 / OV3660）
// 用途：启动摄像头，提供 GET /capture 返回一张 JPEG。
// 后续推送到你自己的后端时，改这里即可。

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============ 改这里：你的 WiFi ============
const char* WIFI_SSID = "你的WiFi名";
const char* WIFI_PASS = "你的WiFi密码";
// ===========================================

WebServer server(80);

// AI-Thinker ESP32-CAM 标准引脚定义（OV2640 / OV3660 通用）
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=snap.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // 关掉欠压复位，板子供电不稳时不重启
  Serial.begin(115200);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // 分辨率：这里用 VGA(640x480)。若要更大：FRAMESIZE_QVGA / SVGA(800x600) / UXGA(1600x1200)
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;      // 0~63，越小越清晰，文件越大
  config.fb_count     = 2;       // 双缓冲，抓帧更稳

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP = http://");
  Serial.println(WiFi.localIP());

  server.on("/", []() { server.send(200, "text/html", "<h2>ESP32-CAM OK</h2><a href='/capture'>capture</a>"); });
  server.on("/capture", HTTP_GET, handleCapture);
  server.begin();
  Serial.println("HTTP server started. Try http://<IP>/capture");
}

void loop() {
  server.handleClient();
  delay(1);
}
