// ============================================================
// ESP32-CAM 画面源 v5（OV2640 / OV3660）
// ------------------------------------------------------------
// 用途：启动摄像头，提供 GET /capture 返回一张 JPEG；
//       并提供 GET /status 返回设备诊断信息。
//
// 特性：
//   1. 禁用 WiFi 省电（WiFi.setSleep(false)）—— 避免空闲休眠带来的唤醒延迟
//   2. 双向保活心跳 —— 解决 Mesh 子路由（无线回程）空闲后
//      首次访问 TCP 握手卡顿十几秒的问题
//   3. OTA 无线升级 —— 烧录本版后，以后可通过 WiFi 更新固件，无需插 USB
//   4. 断线自动重连 + /status 远程诊断
//
// 【烧录前必读】
//   - 下面 WiFi 名/密码改成真实的（只能 2.4GHz）
//   - Arduino IDE 分区方案必须选带 OTA 的：
//     Minimal SPIFFS (1.9MB APP with OTA/...SPIFFS)
//   - 本文件已脱敏，请勿把真实 WiFi 信息推回公开仓库
// ============================================================

#define KEEPALIVE_ENABLE       1          // 1=开启双向保活，0=关闭
#define KEEPALIVE_INTERVAL_MS  3000UL     // 保活间隔(ms)
#define GW_IP                  192,168,31,1   // 网关地址（按自己网段改）
#define OTA_PASSWORD           "otapass1234"  // OTA 密码（建议修改）

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============ 改这里：你的 WiFi ============
const char* WIFI_SSID = "你的WiFi名";
const char* WIFI_PASS = "你的WiFi密码";
// ===========================================

WebServer server(80);
WiFiUDP   udp;
unsigned long lastKeepAlive = 0;
unsigned long kaGwCount = 0;      // 发往网关的次数
unsigned long kaClientCount = 0;  // 发往客户端的次数
IPAddress     lastClientIP(0, 0, 0, 0);   // 最近访问的客户端（自动学习）
String        otaHostname = "esp32cam";

// AI-Thinker ESP32-CAM 标准引脚定义
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

// ---------- 辅助函数 ----------
const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW(restart/OTA)";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

const char* psModeStr() {
  wifi_ps_type_t ps;
  if (esp_wifi_get_ps(&ps) != ESP_OK) return "GET_FAIL";
  switch (ps) {
    case WIFI_PS_NONE:      return "NONE(省电已关闭)";
    case WIFI_PS_MIN_MODEM: return "MIN_MODEM(最小省电)";
    case WIFI_PS_MAX_MODEM: return "MAX_MODEM(最大省电)";
    default:                return "UNKNOWN";
  }
}

// 记录发起请求的客户端 IP（供下行保活使用）
void noteClient() {
  IPAddress ip = server.client().remoteIP();
  if (ip != IPAddress(0, 0, 0, 0)) lastClientIP = ip;
}

// ---------- WiFi 连接（禁用省电 + 重试） ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);          // 禁用省电

  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500); Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi OK. IP = "); Serial.println(WiFi.localIP());
    Serial.print("BSSID = ");      Serial.println(WiFi.BSSIDstr());
    Serial.printf("RSSI = %d dBm, Channel = %d\n", WiFi.RSSI(), WiFi.channel());
    Serial.printf("WiFi PS mode = %s\n", psModeStr());
    WiFi.setSleep(false);        // 连接后再确认一次
  } else {
    Serial.println();
    Serial.println("WiFi connect timeout, will retry...");
  }
}

// ---------- OTA 初始化 ----------
void setupOTA() {
  // 用芯片 MAC 低 16 位生成唯一 hostname，避免多台设备重名
  uint32_t chipId = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  otaHostname  = "esp32cam-";
  otaHostname += String(chipId, HEX);

  ArduinoOTA.setHostname(otaHostname.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("\n[OTA] Start updating...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Done. Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.print("OTA ready. hostname = ");
  Serial.println(otaHostname);
}

// ---------- 抓图 ----------
void handleCapture() {
  noteClient();

  // 摄像头空闲重启后，两个缓冲区里存的都是旧画面：各丢一帧，
  // 并留时间给传感器稳定输出新帧
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  delay(150); // 等传感器稳定产出一帧新画面

  fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=snap.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ---------- 初始化 ----------
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // 关掉欠压复位，板子供电不稳时不重启
  Serial.begin(115200);
  delay(100);

  esp_reset_reason_t rr = esp_reset_reason();
  Serial.println();
  Serial.println("========== ESP32-CAM v5 BOOT ==========");
  Serial.printf("reset_reason = %d (%s)\n", (int)rr, resetReasonStr(rr));
  Serial.println("特性：双向保活 + OTA 无线升级");
  Serial.println("======================================");

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

  connectWiFi();

  udp.begin(12345);   // 保活心跳用
  setupOTA();         // OTA 初始化

  server.on("/", []() {
    noteClient();
    server.send(200, "text/html", "<h2>ESP32-CAM v5 OK</h2><a href='/capture'>capture</a> | <a href='/status'>status</a>");
  });

  server.on("/capture", HTTP_GET, handleCapture);

  server.on("/status", []() {
    noteClient();
    char buf[900];
    snprintf(buf, sizeof(buf),
      "reset_reason=%d (%s)\n"
      "uptime_s=%lu\n"
      "wifi_status=%d\n"
      "ip=%s\n"
      "mac=%s\n"
      "bssid=%s\n"
      "rssi=%d\n"
      "channel=%d\n"
      "wifi_ps=%s\n"
      "free_heap=%u\n"
      "keepalive_enabled=%d\n"
      "ka_gw=%lu\n"
      "ka_client=%lu\n"
      "last_client=%s\n"
      "ota_hostname=%s\n",
      (int)esp_reset_reason(), resetReasonStr(esp_reset_reason()),
      millis() / 1000,
      (int)WiFi.status(),
      WiFi.localIP().toString().c_str(),
      WiFi.macAddress().c_str(),
      WiFi.BSSIDstr().c_str(),
      WiFi.RSSI(),
      WiFi.channel(),
      psModeStr(),
      ESP.getFreeHeap(),
      (int)KEEPALIVE_ENABLE,
      kaGwCount,
      kaClientCount,
      lastClientIP.toString().c_str(),
      otaHostname.c_str());
    server.send(200, "text/plain", buf);
  });

  server.begin();
  Serial.println("HTTP server started. try /capture or /status");
}

// ---------- 主循环 ----------
void loop() {
  ArduinoOTA.handle();     // OTA 事件处理

#if KEEPALIVE_ENABLE
  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastKeepAlive >= KEEPALIVE_INTERVAL_MS) {
    lastKeepAlive = millis();

    // ① 上行保活：到网关，保持 子路由→主路由 回程链路活跃
    IPAddress gw(GW_IP);
    udp.beginPacket(gw, 7);
    udp.write((const uint8_t*)"KA", 2);
    udp.endPacket();
    kaGwCount++;

    // ② 下行保活：到最近访问过的客户端（刷新其 ARP 缓存）
    if (lastClientIP != IPAddress(0, 0, 0, 0)) {
      udp.beginPacket(lastClientIP, 9);
      udp.write((const uint8_t*)"KA", 2);
      udp.endPacket();
      kaClientCount++;
    }
  }
#endif

  // 断线自动重连
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Reconnected. IP = ");
      Serial.println(WiFi.localIP());
      WiFi.setSleep(false);
    }
  }

  server.handleClient();
  delay(1);
}
