# 给 AI/Agent 的摄像头对接说明

这个项目提供 ESP32-CAM 摄像头画面，供 AI/Agent 拉取并识别。

## 图片位置（HTTP 下载地址）

摄像头画面通过 HTTP 提供单张 JPEG：

```
http://<摄像头IP>/capture
```

- 实际地址示例：`http://<摄像头IP>/capture`
- IP 是 DHCP 分配的，变了怎么办：按板子 RST 键，在 Arduino 串口监视器(115200)看新 IP
- 前提：你的设备（手机/电脑）和 ESP32 在同一 WiFi

## 怎么拿图

直接对这个 URL 发一个 GET 请求，返回的就是一张 JPEG 图片。

Python 示例：
```python
import requests
resp = requests.get("http://<摄像头IP>/capture", timeout=5)
with open("snapshot.jpg", "wb") as f:
    f.write(resp.content)
# resp.content 就是图片字节
```

curl 示例：
```
curl -o snapshot.jpg http://<摄像头IP>/capture
```

## 怎么喂给视觉模型（如 MiMo-2.5）

拿到图片后，把图片数据传给视觉模型。两种常见方式：

**方式一：传图片字节（base64）**
把 JPEG 转 base64，塞进请求的图片字段。MiMo/大多数 OpenAI 兼容接口支持：
```json
{
  "model": "mimo-2.5",
  "messages": [
    {
      "role": "user",
      "content": [
        {"type": "text", "text": "描述一下画面里有什么"},
        {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,<base64编码的图片>"}}
      ]
    }
  ]
}
```

**方式二：传图片 URL（如果模型支持拉取公网 URL）**
注意：`http://<摄像头IP>/capture` 是局域网地址，云端模型访问不到。**只有模型能访问到该地址时才可以用 URL 方式**。否则必须用方式一（把图传给后端，由后端调模型）。

## 常见问题
- 图片偏绿：刚上电时自动白平衡在调整，等十几秒再拉。
- 模糊：当前分辨率 VGA 640×480。需要更清晰改固件里的 FRAMESIZE_VGA 为 FRAMESIZE_SVGA/UXGA。
- 不是视频：`/capture` 一次返回一帧，要"看实时"就循环拉取（比如每 0.5~1 秒拉一次）。
