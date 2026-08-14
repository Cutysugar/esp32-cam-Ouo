# ESP32-CAM 画面源（AI 恋人摄像头）

给 AI 恋人做的 ESP32-CAM 摄像头画面源。板子提供 `GET /capture` 返回单张 JPEG，供同一 WiFi 下的手机/后端拉取识别。

## 硬件
- ESP32-CAM（AI-Thinker，带 4MB PSRAM）+ 下载底板
- 摄像头 OV3660（3MP，兼容 OV2640 排线）
- Micro-USB 数据线烧录

## 烧录
1. Arduino IDE 2.x + esp32 平台（Espressif 官方）
2. 把 `esp32_cam.ino` 前两行的 `你的WiFi名` / `你的WiFi密码` 改成真实的（只能 2.4GHz）
3. 开发板选：AI Thinker ESP32-CAM
4. 端口选：你的 COM 口（CH340）
5. 上传，按 RST 复位，串口监视器(115200)看 IP

## 使用
浏览器打开 `http://<IP>/capture` 看到照片即成功。
同一 WiFi 下的手机 agent 也可直接拉这个地址的图做视觉识别。

## 状态
见 `docs/project-status.md`。
