# SimpleScreenShot

轻量级 Linux X11 截图工具，纯 C++20 实现（GTK3 + Cairo + Pango），单进程、无重运行时依赖。
当前版本：**1.0.0**。

## 功能特性

- **截图**：全屏 / 多显示器区域截图，标注工具（箭头、矩形、椭圆、直线、文字、马赛克、序号标号）
- **贴图**：截图钉在桌面置顶显示
- **长截图**：滚动窗口拼接
- **OCR 文字识别**：识别结果自动复制到剪贴板
- **二维码识别**：选区二维码内容解析（内置 quirc 解码，无外部依赖）
- **录屏**：区域录制 MP4 / GIF，可选录制系统声音与麦克风（PipeWire / PulseAudio 桌面均可）
- **延迟截图**：托盘菜单 3 / 5 / 10 秒倒计时 + 弹窗自定义秒数
- **托盘常驻**、单实例运行、全局快捷键（默认 `Ctrl+Alt+A`，可在配置修改）
- 界面语言：简体中文 / English

## 系统要求

- Linux + **X11** 桌面环境（GNOME、MATE 等已实测；暂不支持 Wayland）
- 已在 **Ubuntu 20.04** 与 **Linux Mint 22.3** (Ubuntu 24.04 基座) 上实测编译、运行

## 从源码编译

### 编译依赖（Ubuntu / Debian 包名）

```bash
sudo apt install build-essential cmake pkg-config dpkg-dev \
    libgtk-3-dev libcairo2-dev libpango1.0-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxtst-dev libxfixes-dev libglib2.0-dev
```

- CMake ≥ 3.16，GCC ≥ 9（Ubuntu 20.04 默认的 gcc 9 即可，CMake 会自动使用 `-std=c++2a`）

### 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/screencapture
```

### Debian 打包

```bash
./build-deb.sh            # 生成 build-deb/simplescreenshot_<版本>_amd64.deb
./build-deb.sh --clean    # 清理后全量重新打包
```

安装布局（FHS）：`/usr/bin/screencapture`、`/usr/share/simplescreenshot/assets/`、
桌面入口 `/usr/share/applications/simplescreenshot.desktop`。

## 运行时依赖

### 系统库（deb 包已声明为硬依赖，装完即用）

GTK3 / Cairo / Pango / X11 等共享库由 deb 按实际链接自动声明（`Depends`），
`apt install ./simplescreenshot_*.deb` 即可完整安装程序本体，二维码识别
内置解码器，无需任何额外依赖。

### 功能级依赖（deb 均声明为推荐依赖 `Recommends`，不装不影响其他功能）

各功能在使用时自行检查依赖，缺失时弹窗提示安装命令，不会静默失败或崩溃。

| 依赖 | 用途 | 缺失时的表现 |
|---|---|---|
| `ffmpeg` | **录屏**（MP4/GIF，基于 x11grab；音频经 PipeWire / PulseAudio） | 其余功能完全正常；点"开始录制"即弹窗提示安装 ffmpeg |
| `tesseract-ocr` + `tesseract-ocr-chi-sim` | OCR 文字识别的兜底引擎（中文+英文） | 其余功能完全正常；OCR 时弹窗提示安装命令 |
| `xclip` | 剪贴板图片持久化（系统无剪贴板管理器时，程序退出后剪贴板中的截图仍不丢失） | 有剪贴板管理器的桌面无需安装 |
| `rapidocr`（pip 安装） | 可选的高级 OCR 引擎，中文识别效果显著优于 tesseract | 启动时提示一次（可勾选"不再提醒"）；OCR 回退使用 tesseract |

```bash
sudo apt install ffmpeg xclip tesseract-ocr tesseract-ocr-chi-sim   # 推荐依赖
pip3 install rapidocr[onnxruntime]                                  # 可选高级 OCR
```

### 离线 OCR 说明

- deb 包内置 OCR 插件 `assets/plugins/ocr_rapidocr.py`：优先使用 RapidOCR，
  自动回退 tesseract；调用协议与自定义插件方法见
  [assets/plugins/README.md](assets/plugins/README.md)。
- RapidOCR 首次识别会联网下载模型到本地缓存（`~/.cache/` 或 `~/.local/share/` 下
  rapidocr 相关目录），**之后完全离线运行**。无网机器可在联网机器上先跑一次识别，
  再把模型缓存目录一并拷贝过去，具体步骤见上述 README。
- 插件路径留空时自动检测内置插件；也可在 `config.json` 中指定自定义插件：

  ```json
  { "ocr": { "plugin": "/path/to/your/plugin.py" } }
  ```

## 录屏说明

- MP4（H.264 + AAC，最长 120 分钟）与 GIF（最长 6 分钟）两种格式；工具条上
  可切换并单独开关"系统声音 / 麦克风"，两者可同时录制（自动混合）。
- 系统声音通过当前默认输出设备的 monitor 源采集。PipeWire 与 PulseAudio
  桌面均支持（PipeWire 环境经 `pw-cat`、PulseAudio 环境经 `parecord` 采集，
  绕开 pipewire-pulse 下 ffmpeg pulse 输入录到静音的问题）。
- 录制以边框内缩 2px 的区域为准，避免把红色录制边框录进视频。
- 点"开始录制"时若系统缺少 ffmpeg 会立即弹窗提示，停止后若编码失败
  （如磁盘/设备异常）也会弹窗提示，不会保存损坏的文件。

## 配置文件

`~/.config/screencapture/config.json`（程序目录下存在 `config.json` 时优先使用便携模式）。

常用项：界面语言、截图快捷键、OCR 插件路径（`ocr.plugin`）、启动 OCR 提示开关
（`ocr.rapidocrHintHidden`）等，均为首次使用时按需写入。

## 仓库结构

```
include/ src/        C++ 源码（平台层 / 界面层 / 媒体层）
assets/              运行资源（图标、字体、界面语言 JSON、OCR 插件）
third_party/         内嵌第三方源码（nlohmann/json、quirc、cgif）
packaging/           deb 打包附属文件（desktop、copyright、changelog、man）
build-deb.sh         deb 打包脚本（CPack）
```

## 许可

专有软件：© 2026 xinwf <xinwf18@gmail.com>，保留所有权利。
详见 [packaging/copyright](packaging/copyright)。
