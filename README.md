# BfPlayer 修改记录

## 概述

本文档记录了 BfPlayer 媒体播放器项目的所有修改和新增功能。

---

## 常见问题

### 图标显示不正确

如果应用程序图标显示为默认图标而非自定义图标，是 Windows 图标缓存导致的问题。

**解决方法**：重建图标缓存
```cmd
taskkill /IM explorer.exe /F
cd /d %userprofile%\AppData\Local
del IconCache.db /a
start explorer.exe
```

---

## 修复的问题

### 1. 播放视频没有声音

**问题描述**：播放视频时没有声音输出。

**根本原因**：
- `playAudioFrame` 函数中使用了未声明的变量 `m_audioBuffer`
- Qt 多媒体插件目录结构错误（`multimedia/` → 应为 `mediaservice/` 和 `audio/`）
- `QAudioOutput` 在主线程创建但在工作线程中使用（违反 Qt 线程亲和性）

**修复方案**：
1. 将 `m_audioBuffer` 改为 `m_audioDevice`（`QAudioOutput::start()` 返回的 QIODevice）
2. 创建正确的插件目录结构：
   - `release/mediaservice/` - DirectShow/WMF 音频后端
   - `release/audio/` - Windows 音频插件
3. 将 `initAudioOutput()` 调用从 `openFile()`（主线程）移到 `run()`（工作线程）
4. 添加格式支持检查，不支持时使用最接近的格式
5. 设置较大的音频缓冲区（65536 字节）防止卡顿

**修改文件**：
- `core/VideoPlayerWidget.cpp`
- `core/VideoPlayerThread.h`

---

### 2. "视频截取"对话框时间控件背景颜色

**问题描述**："起始时间"和"结束时间"输入框背景颜色过深。

**修复方案**：在 `TimeEditWidget` 构造函数中设置控件容器背景为浅灰色 `#f5f5f5`，输入框背景为白色 `#ffffff`。

**修改文件**：
- `ui/TimeEditWidget.cpp`

---

### 3. "打开媒体文件"对话框无法正常关闭

**问题描述**：选择文件后对话框消失一下又出现，需要点击取消才能关闭。

**修复方案**：使用 `QFileDialog::getOpenFileName()` 静态函数替代手动创建的对话框对象，由 Qt 自动管理对话框生命周期。添加 `m_isOpeningFile` 防护标志防止重复调用。

**修改文件**：
- `ui/MainWindow.cpp`
- `ui/MainWindow.h`

---

### 4. 截取完成后关闭对话框导致崩溃

**问题描述**：视频截取完成后点击"关闭"按钮，应用程序崩溃。

**根本原因**：`m_workerThread` 和 `m_extractor` 对象被 `deleteLater()` 后指针未置空（悬空指针），`closeEvent()` 访问这些悬空指针导致崩溃。

**修复方案**：将指针类型改为 `QPointer<QThread>` 和 `QPointer<ClipExtractor>`，`QPointer` 会在对象被删除时自动置为 `nullptr`。

**修改文件**：
- `ui/ClipDialog.h`

---

### 5. 暂停状态下拖动进度条画面不更新

**问题描述**：暂停视频后拖动进度条，播放画面没有变化。

**根本原因**：`seek()` 函数只调用了 `m_thread->seekTo()`，该方法仅设置标志位。但线程处于暂停状态时不会处理这个标志位。

**修复方案**：
1. 在 `VideoPlayerThread` 中新增 `seekAndDecodeFrame()` 方法，在工作线程中执行 seek 和解码
2. 新增 `seekFrameReady` 信号，当解码完成后发送帧到 UI 线程
3. `VideoPlayerWidget::seek()` 在暂停状态下直接调用 `seekAndDecodeFrame()`
4. 通过 `Qt::QueuedConnection` 连接信号，确保帧数据跨线程安全传递

**修改文件**：
- `core/VideoPlayerWidget.h`
- `core/VideoPlayerWidget.cpp`
- `core/VideoPlayerThread.h`

---

### 6. 暂停状态下键盘快进/快退位置错误

**问题描述**：暂停在 15 秒，拖动到 20 秒后按右箭头，播放器跳到 15 秒而非 23 秒。

**根本原因**：`doRelativeSeek` 使用 `m_player->position()` 获取当前位置，但暂停时工作线程不会更新位置值。

**修复方案**：在 UI 层添加 `m_currentPositionMs` 变量跟踪当前位置，在 `onSeek`、`onPositionChanged` 中更新它，`doRelativeSeek` 使用这个跟踪值。

**修改文件**：
- `ui/MainWindow.h`
- `ui/MainWindow.cpp`

---

### 7. 网络流无法播放

**问题描述**：无法播放在线视频和直播流。

**根本原因**：
1. FFmpeg 编译时未启用网络协议支持
2. 缺少 FFmpeg 网络依赖库（libgnutls 等）
3. HLS 直播流被错误地下载到本地再播放
4. SSL 证书验证失败

**修复方案**：
1. 替换为支持网络的 FFmpeg DLL（MSYS2 版本，启用 gnutls）
2. 复制所有依赖库到 release 目录（libgnutls、libiconv、zlib 等）
3. 在 `main()` 中调用 `avformat_network_init()` 初始化网络支持
4. 区分直播流和点播视频：
   - 直播流（.m3u8/.m3u、rtmp://、rtsp://）：直接让 FFmpeg 处理
   - 点播视频（http://、https:// 的 MP4/MKV）：下载到本地再播放
5. 添加 `tls_verify=0` 选项忽略 SSL 证书错误
6. 添加网络流选项（超时、缓冲区大小等）

**修改文件**：
- `main.cpp` - 添加 `avformat_network_init()`
- `core/VideoPlayerWidget.cpp` - 添加网络流选项
- `ui/MainWindow.cpp` - 区分直播流和点播

---

## 新增功能

### 1. 禁音按钮

**功能描述**：在音量控制旁添加禁音按钮，点击切换禁音/有声音状态。

**实现细节**：
- 按钮显示状态：`🔇`（禁音）/ `🔊`（有声音）
- 禁音时保存当前音量并将音量设为 0
- 取消禁音时恢复之前保存的音量
- 默认状态：程序启动时和打开新媒体时默认为禁音状态

**修改文件**：
- `ui/MainWindow.h` - 添加 `m_muteBtn`、`m_isMuted`、`m_volumeBeforeMute` 成员
- `ui/MainWindow.cpp` - 实现 `toggleMute()` 槽函数

---

### 2. 点击进度条跳转

**功能描述**：点击进度条任意位置，视频跳转到对应时间点。

**实现细节**：
- 通过事件过滤器监听进度条鼠标点击事件
- 根据点击位置 x 坐标计算对应的时间值
- 调用 `seek()` 跳转到该位置
- 事件过滤器返回 `false` 保持滑块拖动功能正常

**修改文件**：
- `ui/MainWindow.cpp` - 实现 `eventFilter()` 函数

---

### 3. 键盘快进/快退

**功能描述**：使用键盘左右箭头键控制快进/快退。

**实现细节**：
- `←` 左箭头：快退 3 秒
- `→` 右箭头：快进 3 秒
- 按住不放：500ms 延迟后开始连续快退/快进（每 300ms 一次，每次 3 秒）
- 主窗口焦点策略设为 `Qt::StrongFocus`，子控件设为 `Qt::NoFocus`

**修改文件**：
- `ui/MainWindow.h` - 添加 `m_seekTimer`、`m_seekDirection` 成员及相关函数声明
- `ui/MainWindow.cpp` - 实现 `keyPressEvent()`、`keyReleaseEvent()`、`onSeekTimer()`、`doRelativeSeek()`

---

### 4. 点击播放画面暂停/播放

**功能描述**：点击视频播放画面，切换暂停/播放状态。

**实现细节**：
- 在 `VideoPlayerWidget` 中重写 `mousePressEvent`
- 点击时发射 `clicked()` 信号
- 连接到 `MainWindow::togglePlay()` 实现暂停/播放切换

**修改文件**：
- `core/VideoPlayerWidget.h` - 添加 `mousePressEvent` 和 `clicked()` 信号
- `core/VideoPlayerWidget.cpp` - 实现 `mousePressEvent`
- `ui/MainWindow.cpp` - 连接 `clicked()` 信号到 `togglePlay()`

---

### 5. 应用程序图标

**功能描述**：设置应用程序窗口图标和可执行文件图标。

**实现细节**：
- 使用 Python 脚本生成包含 16/32/48/64/128/256 六种尺寸的标准 Windows ICO 文件
- 通过 Qt 资源文件设置窗口图标
- 通过 Windows RC 文件设置 .exe 文件图标

**修改文件**：
- `BfPlayer.qrc` - Qt 资源文件
- `BfPlayer.rc` - Windows 资源文件
- `BfPlayer.pro` - 添加 `RESOURCES` 和 `RC_FILE` 配置
- `main.cpp` - 设置应用程序窗口图标

---

### 6. 在线播放

**功能描述**：播放网络视频和直播流。

**实现细节**：
- 点播视频（MP4/MKV）：下载到本地后播放
- 直播流（HLS/RTMP/RTSP）：直接由 FFmpeg 实时播放
- 支持 HTTP/HTTPS 协议
- 状态栏显示下载进度
- 支持 URL 历史记录

**测试 URL**：
- HLS 直播：`https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8`
- MP4 视频：`https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4`

**修改文件**：
- `ui/MainWindow.h` - 添加 `openNetworkStream()` 槽函数
- `ui/MainWindow.cpp` - 实现在线播放对话框和下载逻辑
- `BfPlayer.pro` - 添加 `network` 模块

---

### 7. 在线电视（IPTV）

**功能描述**：加载和播放 M3U/M3U8 播放列表。

**实现细节**：
- 支持本地文件和远程 URL 的 M3U/M3U8 播放列表
- 解析频道列表（支持 `#EXTINF` 标签）
- 双击频道名称播放
- 支持频道搜索和分组（可扩展）

**修改文件**：
- `ui/MainWindow.h` - 添加 `openIPTVDialog()` 槽函数
- `ui/MainWindow.cpp` - 实现 IPTV 对话框和播放列表解析

---

## 文件结构

```
BfPlayer/
├── BfPlayer.pro          # 项目配置文件
├── BfPlayer.qrc          # Qt 资源文件
├── BfPlayer.rc           # Windows 资源文件（图标）
├── main.cpp              # 程序入口
├── readmebf.md           # 本文档
├── core/
│   ├── VideoPlayerWidget.h   # 视频播放控件
│   ├── VideoPlayerWidget.cpp # 视频播放实现
│   ├── VideoPlayerThread.h   # 解码线程
│   └── VideoPlayerThread.cpp # 解码线程实现
├── ui/
│   ├── MainWindow.h      # 主窗口
│   ├── MainWindow.cpp    # 主窗口实现
│   ├── ClipDialog.h      # 截取对话框
│   ├── ClipDialog.cpp    # 截取对话框实现
│   ├── TimeEditWidget.h  # 时间输入控件
│   └── TimeEditWidget.cpp# 时间输入控件实现
└── resources/            # 资源目录
    ├── icon-111.png      # 应用程序图标（PNG）
    └── main.ico          # 应用程序图标（ICO）
```

---

## 构建方法

在 MSYS2 MinGW64 shell 中：

```bash
cd D:/workspace/BfPlayer
qmake-qt5.exe BfPlayer.pro
mingw32-make -j4
```

可执行文件输出到 `release/BfPlayer.exe`。

---

## 依赖项

- Qt 5.x（Core, Gui, Widgets, Multimedia, Network）
- FFmpeg 9.0.1（avformat, avcodec, avutil, swscale, swresample）- 需要网络支持版本
- FFmpeg 依赖库：libgnutls, libiconv, zlib, libbz2, libxml2, librtmp 等
- Windows 平台库（bcrypt, strmiids, ole32, oleaut32, uuid, secur32, mfplat, mf, mfreadwrite）

---

## 使用说明

### 基本操作
- **打开文件**：点击工具栏"打开文件"按钮或按 `Ctrl+O`
- **播放/暂停**：点击播放画面或点击工具栏"播放/暂停"按钮
- **停止**：点击工具栏"停止"按钮
- **音量控制**：拖动音量滑块调节音量
- **禁音**：点击 `🔇` 按钮切换禁音状态
- **进度跳转**：点击进度条任意位置跳转到该时间点

### 在线播放
- **在线播放**：点击工具栏"在线播放"按钮或按 `Ctrl+U`
  - 输入网络 URL（支持 HTTP/HTTPS/RTMP/RTSP）
  - 点播视频会下载后播放
  - 直播流（.m3u8）会实时播放
- **在线电视**：点击工具栏"在线电视"按钮或按 `Ctrl+T`
  - 加载 M3U/M3U8 播放列表
  - 双击频道播放

### 键盘快捷键
- `←` 左箭头：快退 3 秒（按住连续快退）
- `→` 右箭头：快进 3 秒（按住连续快进）
- `Space`：播放/暂停
- `Ctrl+O`：打开文件
- `Ctrl+U`：在线播放
- `Ctrl+T`：在线电视
- `Ctrl+K`：视频截取

### 视频截取
- 点击工具栏"视频截取..."按钮或按 `Ctrl+K`
- 设置起始时间和结束时间
- 选择输出路径和模式
- 点击"开始截取"

---

## 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| 1.0.0 | 2026-09-01 | 初始版本，修复音频、对话框、崩溃问题，新增禁音、进度跳转、键盘控制等功能 |
| 1.0.1 | 2026-09-01 | 修复暂停时拖动进度条画面不更新、键盘快进/快退位置错误等问题 |
| 1.0.2 | 2026-09-02 | 新增在线播放、在线电视功能，修复网络流播放问题 |
