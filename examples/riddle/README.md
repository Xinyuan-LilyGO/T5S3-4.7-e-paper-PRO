# Riddle Diary for T5 E-Paper S3 Pro

这是将 [`riddle`](https://github.com/MaximeRivest/riddle) 的核心日记交互重写到 ESP32-S3 的示例。它复用 `touch_gt6972p_pen_display` 的 GT6972P 主动笔、触摸采样队列和连续墨水屏扫描链路，不包含原项目的 Linux、Qt、quill、evdev、systemd 或 `pi` 后端。

当前实现覆盖无记忆 MVP：

- 全屏压感书写与笔尾橡皮擦；
- 在 PSRAM 保存全屏 1-bit 页面真值，擦除结果与提交内容逐像素一致；
- 抬笔静置 2.8 秒后裁剪笔迹并生成 1-bit PNG；
- 14 阶段“吸墨”动画和思考墨点；
- OpenAI-compatible `/chat/completions` 视觉请求；
- 中英文普通字体逐字显示回复；
- 回复停留、10 阶段消散和质量刷新；
- 未配置网络时自动使用离线演示回复，便于先验证硬件闭环。

## 配置

在 `main/` 下创建不提交到 Git 的 `RiddleSecrets.h`：

```cpp
#pragma once
#define RIDDLE_WIFI_SSID "your-wifi"
#define RIDDLE_WIFI_PASSWORD "your-password"
#define RIDDLE_OPENAI_API_KEY "your-api-key"

// 可选：默认值分别是 https://api.openai.com/v1 和 gpt-4o-mini。
// #define RIDDLE_OPENAI_BASE_URL "https://api.openai.com/v1"
// #define RIDDLE_OPENAI_MODEL "gpt-4o-mini"

// 推荐提供 endpoint 对应的 PEM 根证书。
// #define RIDDLE_OPENAI_ROOT_CA "-----BEGIN CERTIFICATE-----\n..."

// 仅限隔离的台架快速验证；这会关闭服务端身份校验。
// #define RIDDLE_OPENAI_INSECURE_TLS 1
```

默认会安全失败，只有提供 `RIDDLE_OPENAI_ROOT_CA` 或显式设置 `RIDDLE_OPENAI_INSECURE_TLS=1` 才会联网请求。使用 CA 时固件会在首次请求前通过 NTP 同步证书校验所需的系统时间。每次 HTTP 事务有统一的 120 秒截止时间，响应头与正文扫描长度也有上限。API key 不应写入公开固件；量产版本应改为受控配网和安全存储。

## 构建

在根目录 `platformio.ini` 的 `[platformio]` 中选择：

```ini
src_dir = examples/riddle/main
```

然后运行：

```text
pio run -e T5_E_PAPER_S3_V7
pio run -e T5_E_PAPER_S3_V7 -t upload
pio device monitor -b 115200
```

短按 BOOT 清空当前页面，长按一秒执行物理质量刷新。网络、PNG 和 TLS 在 core 0 的低优先级任务运行；主动笔采样保持 priority 4，UI 保持 priority 2，EPD 扫描继续独占 core 1。

## 当前边界

这是阶段 1 示例，不包含原项目的 SSE 按句流式回复、Dancing Script 中心线逐笔动画、本地记忆/历史页、问号帮助页、休眠或 OTA。视觉请求会把当前裁剪后的手写 PNG 发送到配置的 API endpoint；离线模式不会发送数据。

移植行为参考 `riddle` commit `0d1a9feea75027543e2ed49c721656cd261f03a1`。原项目代码为 MIT License，许可文本见 `LICENSE.riddle`；本示例与仓库内 GPL-2.0-only 的 GT6972P 驱动组合使用。
