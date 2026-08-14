# ESP32-CAM 项目状态（2026-08-14）

## 目标
给 AI 恋人做一个摄像头"眼睛"。用 ESP32-CAM 在家拍摄画面，通过后端调用 MiMo-2.5 视觉模型识别，让 AI 能"看到"。

## 已完成
- ESP32-CAM 烧录成功，画面正常输出（OV3660 摄像头，带 4MB PSRAM）
- 固件：`C:Usersangelesp32_camesp32_cam.ino`
- 板子提供 `GET /capture` 返回单张 JPEG
- 局域网地址示例：`http://192.168.31.222/capture`（DHCP 分配，重新上电可能变，按 RST 后串口监视器可看新 IP）

## 硬件
- ESP32-CAM + 下载底板（USB-SERIAL CH340, COM 口之前是 COM6）
- 摄像头 OV3660（3MP），排线物理兼容 OV2640
- 固件当前分辨率 VGA 640×480，jpeg_quality 12，双缓冲

## 最新架构（2026-08-14 已打通）
`ESP32-CAM ──(同一WiFi)──▶ 手机 agent 直接拉图 ──▶ MiMo-2.5 识别`

**手机 agent 能直接访问 `http://<IP>/capture` 拿图并识别，已实测成功。**
前提：手机和 ESP32 在同一 WiFi。

（VPS 推帧方案作为备选，仅当手机/agent 不在家 WiFi 时才需要）

## 架构决策（关键）
- **后端在 VPS 上**，ESP32 在家局域网 → 后端无法反向拉帧
- **最终方案：ESP32 主动推帧**到 VPS 后端接口，不走隧道
- 识别模型：**MiMo-2.5**（小米多模态）

## 待办（下一步）
1. 让写后端的 AI 加接收图片接口：`POST /api/cam/frame`，multipart 字段名 `file`，JPEG，加 token 鉴权
2. 接口写好测试通后，把 ESP32 固件改成"定时抓帧 → POST 到接口"
3. 后端收到图后调 MiMo-2.5 识别

## 给写后端的 AI 的话术
见对话记录。要点：加收图接口 + 调 MiMo-2.5 + 鉴权 + 给测试方法。

## ⚠️ 烧录前提醒（重要）
固件已推送到公开 GitHub，WiFi 名和密码已替换为占位符 `你的WiFi名` / `你的WiFi密码`。
**本地烧录前，必须把这两行改回真实 WiFi 信息**，否则板子连不上网。
改完别把它再推到公开仓库。
