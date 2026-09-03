# BfPlayer - 媒体播放器
# 支持 MP3/MP4 等常见格式播放，以及视频截取功能
#
# 构建方法（在 MSYS2 MinGW64 shell 中）：
#   qmake BfPlayer.pro
#   make -j4

QT       += core gui widgets multimedia network

CONFIG   += c++17

TARGET   = BfPlayer
TEMPLATE = app

# 源文件
SOURCES += \
    main.cpp \
    ui/MainWindow.cpp \
    ui/ClipDialog.cpp \
    ui/TimeEditWidget.cpp \
    core/ClipExtractor.cpp \
    core/VideoPlayerWidget.cpp

# 头文件
HEADERS += \
    ui/MainWindow.h \
    ui/ClipDialog.h \
    ui/TimeEditWidget.h \
    core/ClipExtractor.h \
    core/VideoPlayerWidget.h \
    core/VideoPlayerThread.h

# FFmpeg 开发库路径（MSYS2 MinGW64）
QMAKE_LIBDIR += D:/workspace/tools/cpp-toolchain/msys64/mingw64/lib

# FFmpeg 开发库（通过 MSYS2 pacman 安装: mingw-w64-x86_64-ffmpeg）
# 注意：libswscale.a/libswresample.a 损坏（只有符号表，无实际对象文件），必须用 .dll.a 导入库
FFMPEG_LIB_DIR = D:/workspace/tools/cpp-toolchain/msys64/mingw64/lib
LIBS += -lavformat -lavcodec -lavutil
LIBS += $$FFMPEG_LIB_DIR/libswscale.dll.a
LIBS += $$FFMPEG_LIB_DIR/libswresample.dll.a

# Windows 下 FFmpeg 需要的附加库
LIBS += -lbcrypt -lstrmiids -lole32 -loleaut32 -luuid -lsecur32 -lmfplat -lmf -lmfreadwrite

# 资源文件
RESOURCES += BfPlayer.qrc

# Windows 可执行文件图标
RC_FILE = BfPlayer.rc

# 安装路径（make install 用）
target.path = $$OUT_PWD/bin
INSTALLS += target
