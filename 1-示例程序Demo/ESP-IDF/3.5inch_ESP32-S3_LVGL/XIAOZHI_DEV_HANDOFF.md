# XiaoZhi AI 集成开发交接文档

## 当前状态

### 已完成
- **OTA 配置获取** — 能成功连接 `api.tenclass.net` 获取 WebSocket URL/Token（HTTP 200）
- **WebSocket TLS 连接** — TLS 握手成功，但 WebSocket 升级后被服务器断开
- **音频 RX 延迟开启** — 启动时只开 I2S TX，XiaoZhi 需要麦克风时才开 RX，消除噪音
- **依赖组件补全** — 添加了 `78/esp-ml307` v1.7.1、`78/esp-opus`、`78/esp-opus-encoder` 到 managed_components
- **EspHttp API 适配** — 适配 v1.7.1 API（无 SetTimeout/SetContent/ReadAll）
- **EspHttp bug 修复** — `GetStatusCode()` 之前始终返回 0，已修复
- **TLS 安全加固** — Connect 失败后重建 TLS 上下文，Send/Receive 加空指针检查
- **栈溢出修复** — `xiaozhi_audio_tx` 任务栈从 8192 增到 16384
- **WebSocket 析构安全** — 断开后延迟再 join 接收线程

### 未完成（下一步）

#### 1. WebSocket 握手被服务器拒绝（最高优先级）
**症状：** TLS 连接成功，WebSocket 握手发送后，服务器返回 `-0x6C00`（连接被对端关闭）

**可能原因：**
- WebSocket 握手缺少 `Host` 头或格式不对
- 服务器期望的 `Protocol-Version` 与发送的不匹配
- OTA 返回的 WebSocket URL 路径格式有变化
- 服务器要求额外的认证头

**调试方法：**
- 已在 `web_socket.cc` 中添加了握手请求和响应的日志
- 重新刷机后点击 Talk，查看串口日志中的 `WS handshake response`
- 对比原版 xiaozhi-esp32 的 WebSocket 握手请求

#### 2. TLS 内存分配间歇性失败
**症状：** `mbedtls_ssl_setup returned -0x7F00`（内存不足），导致 OTA 请求间歇性失败

**可能原因：**
- mbedTLS 从内部 SRAM 分配，内部 SRAM 碎片化
- 天气、电台等模块同时占用 TLS 上下文

**建议方案：**
- 启用 `CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC` 让 mbedTLS 使用 PSRAM
- 或在 XiaoZhi 连接前主动释放其他 TLS 连接

#### 3. WebSocket 连接后对话流程未验证
**需要验证：**
- hello 握手（session_id 获取）
- listen 状态发送
- mic Opus 编码上传
- TTS Opus 解码播放
- STT/LLM/TTS JSON 事件处理

## 关键文件

| 文件 | 修改内容 |
|------|---------|
| `main/app_xiaozhi.cpp` | XiaoZhi 客户端主逻辑（OTA + WebSocket + Opus） |
| `main/app_audio.cpp` | 音频模块，RX 延迟开启 |
| `main/idf_component.yml` | 依赖声明（78/esp-ml307 v1.7.1 等） |
| `managed_components/78__esp-ml307/` | TLS/WebSocket 组件（v1.7.1，有本地修改） |
| `patches/78__esp-ml307/` | 组件修改备份（git 跟踪） |

## 恢复开发步骤

```bash
# 1. 拉取项目
cd /Users/tupi/esp32s3-35inch-lvgl-xiaozhi
git pull

# 2. 恢复组件补丁
BASE="1-示例程序Demo/ESP-IDF/3.5inch_ESP32-S3_LVGL"
cp "$BASE/patches/78__esp-ml307/"* "$BASE/managed_components/78__esp-ml307/"

# 3. 源码环境
source /Users/tupi/esp/esp-idf-v5.5/export.sh

# 4. 构建
cd "$BASE" && idf.py build

# 5. 刷机
idf.py -p /dev/tty.usbmodem212301 flash

# 6. 监听日志（查看 WebSocket 握手细节）
python3 -c "
import serial, time
ser = serial.Serial('/dev/tty.usbmodem212301', 115200, timeout=1)
time.sleep(12)
start = time.time()
while time.time() - start < 120:
    line = ser.readline()
    if line:
        txt = line.decode('utf-8', errors='replace').rstrip()
        if any(k in txt for k in ['WS ', 'WebSocket', 'OTA', 'error', 'Error', 'handshake', 'tts', 'stt', 'hello', 'session']):
            print(txt)
ser.close()
"
```

## 服务器信息

- OTA 端点: `https://api.tenclass.net/xiaozhi/ota/`
- WebSocket 端点: `wss://api.tenclass.net:443/xiaozhi/v1/`（从 OTA 响应获取）
- 设备 MAC: `14:c1:9f:d1:aa:a8`
- 设备已在小智平台注册（OTA 返回 200）

## ESP-IDF 环境

- ESP-IDF v5.5.2 位于 `/Users/tupi/esp/esp-idf-v5.5`
- 串口: `/dev/tty.usbmodem212301`
- 目标芯片: ESP32-S3
