# OCR 插件

`ocr_rapidocr.py` 是截图"文字识别"功能的默认 OCR 插件，按以下顺序选择引擎：

1. **RapidOCR**（推荐，PP-OCRv5 模型，中文识别效果远好于 tesseract）
   ```bash
   pip install rapidocr        # 或 pip3 install --user rapidocr
   ```
   首次运行会联网下载模型文件到本地缓存，之后完全离线。如需在无网机器上使用，
   可先在联网机器上运行一次识别，然后把模型缓存目录（`~/.cache/` 或
   `~/.local/share/` 下 rapidocr 相关目录）一并拷贝过去。

2. **tesseract**（自动兜底，rapidocr 不可用时）
   ```bash
   sudo apt install tesseract-ocr tesseract-ocr-chi-sim
   ```

## 调用协议

程序执行截图 OCR 时以如下方式调用本目录插件（见 `src/media/ocr_runner.cpp`）：

```
python3 ocr_rapidocr.py --image-path=<临时png> --del-image=true
```

- 识别文本打印到 stdout（每行一个文本块），调用方直接读取 stdout 作为结果；
- 诊断信息一律走 stderr；
- `--del-image=true` 时插件负责删除临时图片。

另有快速探测：`python3 ocr_rapidocr.py --check`，仅测试 RapidOCR 是否可导入
（不初始化引擎），退出码 0 = 支持、1 = 不支持。程序启动时用它检测是否需要
弹出"回退到 tesseract"提醒（`config.json` 的 `ocr.rapidocrHintHidden` 可关闭
该提醒）。

## 更换/自定义插件

- 默认自动检测 `<程序目录>/assets/plugins/ocr_rapidocr.py`（系统安装时为
  `/usr/share/simplescreenshot/assets/plugins/ocr_rapidocr.py`）；
- 可直接改 `~/.config/screencapture/config.json` 指定自定义插件（`.py` 结尾的
  脚本会自动经 `python3` 调用，其余按可执行文件直接执行）：

  ```json
  { "ocr": { "plugin": "/path/to/your/plugin.py" } }
  ```

- 插件路径留空时使用自动检测；检测不到插件时回退系统 `tesseract` 命令。
