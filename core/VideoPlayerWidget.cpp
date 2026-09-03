#include "VideoPlayerWidget.h"
#include "VideoPlayerThread.h"

#include <QPainter>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QBuffer>
#include <QDebug>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QMetaObject>
#include <QProcessEnvironment>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <cmath>
#include <cstdio>
#include <cstdarg>

// ---- 诊断日志：复现崩溃取证（可安全删除）----
static FILE*     g_bfLog = nullptr;
static long long g_vFrameCount = 0;
static long long g_aFrameCount = 0;
static void bfLog(const char* fmt, ...)
{
    if (!g_bfLog) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_bfLog, fmt, ap);
    va_end(ap);
    fputc('\n', g_bfLog);
    fflush(g_bfLog);   // 关键：崩溃前确保最后一行落盘
}

// ============================================================================
// VideoPlayerWidget 实现
// ============================================================================

VideoPlayerWidget::VideoPlayerWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    m_thread = new VideoPlayerThread(this);

    connect(m_thread, &VideoPlayerThread::frameReady,
            this, &VideoPlayerWidget::onFrameReady, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::playbackFinished,
            this, &VideoPlayerWidget::onPlaybackFinished, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::positionChanged,
            this, &VideoPlayerWidget::positionChanged, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::durationChanged,
            this, &VideoPlayerWidget::durationChanged, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::hasVideoChanged,
            this, &VideoPlayerWidget::hasVideoChanged, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::mediaOpened,
            this, &VideoPlayerWidget::mediaOpened, Qt::QueuedConnection);
    connect(m_thread, &VideoPlayerThread::seekFrameReady,
            this, &VideoPlayerWidget::onSeekFrameReady, Qt::QueuedConnection);
}

VideoPlayerWidget::~VideoPlayerWidget()
{
    m_thread->stop();
    m_thread->wait(3000);
}

bool VideoPlayerWidget::open(const QString &filePath)
{
    closeMedia();
    m_isOpened = m_thread->openFile(filePath);
    return m_isOpened;
}

void VideoPlayerWidget::closeMedia()
{
    m_thread->stop();
    m_thread->closeFile();
    QMutexLocker lock(&m_frameMutex);
    m_currentFrame = QImage();
    m_isOpened = false;
    update();
}

void VideoPlayerWidget::play()
{
    if (m_isOpened) m_thread->play();
    emit stateChanged(1);
}

void VideoPlayerWidget::pause()
{
    m_thread->pause();
    emit stateChanged(2);
}

void VideoPlayerWidget::stop()
{
    m_thread->stop();
    emit stateChanged(0);
}

void VideoPlayerWidget::seek(qint64 positionMs)
{
    m_thread->seekTo(positionMs);
}

void VideoPlayerWidget::onSeekFrameReady(const QImage &frame)
{
    QMutexLocker lock(&m_frameMutex);
    m_currentFrame = frame;
    update();
}

void VideoPlayerWidget::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    m_thread->setVolume(m_volume);
}

int VideoPlayerWidget::volume() const { return m_volume; }
bool VideoPlayerWidget::isPlaying() const { return m_thread->isPlaying(); }
bool VideoPlayerWidget::isPaused() const { return m_thread->isPaused(); }
qint64 VideoPlayerWidget::duration() const { return m_thread->duration(); }
qint64 VideoPlayerWidget::position() const { return m_thread->position(); }
bool VideoPlayerWidget::hasVideo() const { return m_thread->hasVideo(); }
bool VideoPlayerWidget::isOpened() const { return m_isOpened; }

void VideoPlayerWidget::setRotation(int degrees)
{
    int d = ((degrees % 360) + 360) % 360;
    // 只允许 0/90/180/270
    d = (d / 90) * 90;
    if (d != m_rotation) {
        m_rotation = d;
        update();
        emit rotationChanged(d);
    }
}

int VideoPlayerWidget::rotation() const { return m_rotation; }

void VideoPlayerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 黑色背景
    p.fillRect(rect(), Qt::black);

    QMutexLocker lock(&m_frameMutex);
    if (!m_currentFrame.isNull()) {
        // 根据旋转角度确定逻辑可用区域（90°/270°时宽高互换）
        int logicalW = width();
        int logicalH = height();
        if (m_rotation == 90 || m_rotation == 270) {
            std::swap(logicalW, logicalH);
        }

        // 按比例缩放绘制
        QSize frameSize = m_currentFrame.size();
        frameSize.scale(logicalW, logicalH, Qt::KeepAspectRatio);
        QRect targetRect(
            (logicalW - frameSize.width()) / 2,
            (logicalH - frameSize.height()) / 2,
            frameSize.width(),
            frameSize.height());

        // 应用旋转变换
        if (m_rotation != 0) {
            p.translate(width() / 2.0, height() / 2.0);
            p.rotate(m_rotation);
            p.translate(-logicalW / 2.0, -logicalH / 2.0);
        }

        p.drawImage(targetRect, m_currentFrame);
    } else {
        // 无帧时显示提示
        p.setPen(Qt::gray);
        QString hintText = m_isOpened ? tr("加载中...") : tr("拖放文件开始播放");
        p.drawText(rect(), Qt::AlignCenter, hintText);
    }
}

void VideoPlayerWidget::resizeEvent(QResizeEvent *)
{
    update();
}

void VideoPlayerWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isOpened) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void VideoPlayerWidget::onFrameReady(const QImage &frame)
{
    updateFrame(frame);
}

void VideoPlayerWidget::onPlaybackFinished()
{
    emit stateChanged(0);
}

void VideoPlayerWidget::updateFrame(const QImage &frame)
{
    QMutexLocker lock(&m_frameMutex);
    m_currentFrame = frame;
    update();
}

// ============================================================================
// VideoPlayerThread 实现
// ============================================================================

VideoPlayerThread::VideoPlayerThread(QObject *parent)
    : QThread(parent)
{
    // 注意：avformat_network_init() 已在 main() 中调用
}

VideoPlayerThread::~VideoPlayerThread()
{
    stop();
    wait(3000);
    closeFile();  // 释放 FFmpeg 上下文（fmt/codec/pkt/frame 等），避免资源泄漏（debug.md D4 兜底）
}

bool VideoPlayerThread::openFile(const QString &path)
{
    // 注意：调用前应先通过 closeMedia() 确保线程已停止
    // closeFile() 由 closeMedia() 调用，此处不再重复调用

    // 判断是否为网络流
    bool isNetwork = path.startsWith("http://") || path.startsWith("https://") ||
                     path.startsWith("rtmp://") || path.startsWith("rtsp://") ||
                     path.startsWith("udp://") || path.startsWith("tcp://");

    // 网络流选项
    AVDictionary *opts = nullptr;
    if (isNetwork) {
        // 设置超时（微秒）
        av_dict_set_int(&opts, "stimeout", 10000000, 0);     // 10秒超时
        av_dict_set_int(&opts, "rtbufsize", 1024000, 0);     // 缓冲区大小
        // TCP 传输（某些流更稳定）
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        // SSL 验证：默认启用，可通过环境变量 BFPLAYER_IGNORE_SSL=1 忽略（仅用于测试）
        if (QProcessEnvironment::systemEnvironment().value("BFPLAYER_IGNORE_SSL") == "1") {
            av_dict_set(&opts, "tls_verify", "0", 0);
        }
        // 直播流（HLS）鲁棒性：从直播边缘开始、断线自动重连，
        // 减少段获取失败导致的异常退出（问题 1 健壮性修复）
        QString lowerPath = path.toLower();
        if (lowerPath.contains("m3u8")) {
            av_dict_set(&opts, "live_start_index", "-1", 0);
        }
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "2", 0);
    }

    // 打开输入文件
    int ret = avformat_open_input(&m_fmtCtx, path.toUtf8().constData(), nullptr, &opts);
    if (opts) av_dict_free(&opts);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        QString errMsg = tr("无法打开: %1 (错误码: %2)").arg(QString(errBuf)).arg(ret);

        // 检查是否是 HTTPS 不支持
        if (path.startsWith("https://") && ret == -138) {
            errMsg += tr("\n\n当前 FFmpeg 可能不支持 HTTPS 协议。"
                         "请尝试使用 HTTP 地址，或重新编译 FFmpeg 启用 OpenSSL/GnuTLS 支持。");
        }

        // 检查启用的协议
        QString protocols;
        void *opaque = nullptr;
        const char *name;
        while ((name = avio_enum_protocols(&opaque, 0))) {
            if (!protocols.isEmpty()) protocols += ", ";
            protocols += name;
        }
        errMsg += tr("\n\n支持的协议: %1").arg(protocols);

        emit mediaOpened(false, errMsg);
        return false;
    }

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&m_fmtCtx);
        emit mediaOpened(false, tr("无法获取流信息"));
        return false;
    }

    // 查找视频流和音频流
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    m_hasVideo = (m_videoStreamIdx >= 0);
    m_hasAudio = (m_audioStreamIdx >= 0);

    // 检测是否为直播流（时长为 0 或未知表示直播）
    m_isLiveStream = (m_fmtCtx->duration <= 0);

    // 初始化解码器
    if (m_hasVideo) {
        if (!initCodec(m_videoStreamIdx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar,
                       &m_videoCodecCtx, &m_swsCtx, &m_videoWidth, &m_videoHeight)) {
            closeFile();
            return false;
        }
    }

    if (m_hasAudio) {
        if (!initCodec(m_audioStreamIdx, m_fmtCtx->streams[m_audioStreamIdx]->codecpar,
                       &m_audioCodecCtx, nullptr, nullptr, nullptr)) {
            closeFile();
            return false;
        }
        // 注意：initAudioOutput() 将在 run() 中调用（工作线程），
        // 因为 QAudioOutput 必须在其所在线程中使用
    }

    // 时长
    m_durationMs = (m_fmtCtx->duration > 0)
        ? static_cast<qint64>(av_rescale_q(m_fmtCtx->duration, AV_TIME_BASE_Q, {1, 1000}))
        : static_cast<qint64>(m_fmtCtx->duration / AV_TIME_BASE * 1000);

    m_pkt   = av_packet_alloc();
    m_frame = av_frame_alloc();

    emit durationChanged(m_durationMs);
    emit hasVideoChanged(m_hasVideo);
    emit mediaOpened(true, tr("已打开"));
    return true;
}

void VideoPlayerThread::closeFile()
{
    if (m_swsCtx)        { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); }
    if (m_swrCtx)        { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_fmtCtx)        { avformat_close_input(&m_fmtCtx); }
    if (m_pkt)           { av_packet_free(&m_pkt); }
    if (m_frame)         { av_frame_free(&m_frame); }
    // 注意：m_audioOutput 和 m_audioDevice 的清理在 cleanupAudio() 中进行，
    // 它们必须在工作线程中销毁（线程亲和性要求）
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_hasVideo       = false;
    m_hasAudio       = false;
    m_durationMs     = 0;
    m_positionMs     = 0;
    m_audioPosMs     = 0;
}

void VideoPlayerThread::cleanupAudio()
{
    if (m_audioOutput) {
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
        m_audioDevice = nullptr;
    }
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
}

void VideoPlayerThread::play()
{
    m_state = Playing;
    if (!isRunning()) start();
    else m_pauseCond.wakeAll();
}

void VideoPlayerThread::pause()
{
    m_state = Paused;
}

void VideoPlayerThread::stop()
{
    m_state = Stopped;
    m_pauseCond.wakeAll();
    if (isRunning()) {
        requestInterruption();
        wait(2000);
    }
}

void VideoPlayerThread::seekTo(qint64 ms)
{
    m_seekTarget = ms;
    m_seekFlag.store(true);
}

// seekAndDecodeFrame 已删除 - 暂停 seek 现在通过 m_seekAndDecode 标志在工作线程中执行

void VideoPlayerThread::setVolume(int vol)
{
    // 只记录目标音量，实际应用到音频设备在工作线程中进行（避免跨线程调用 QAudioOutput）
    m_volume = qBound(0, vol, 100);
    m_volumeChanged.store(true);
}

// Getters
qint64 VideoPlayerThread::duration() const { return m_durationMs; }
qint64 VideoPlayerThread::position() const { return m_positionMs; }
qint64 VideoPlayerThread::audioPosition() const { return m_audioPosMs; }
bool   VideoPlayerThread::isPlaying() const { return m_state == Playing; }
bool   VideoPlayerThread::isPaused()  const { return m_state == Paused; }
bool   VideoPlayerThread::hasVideo()  const { return m_hasVideo; }
bool   VideoPlayerThread::isOpened()  const { return m_fmtCtx != nullptr; }

void VideoPlayerThread::run()
{
    if (!m_fmtCtx) return;

    // 诊断日志：打开 runtime 日志（exe 同目录），复现崩溃取证
    {
        g_vFrameCount = 0; g_aFrameCount = 0;
        QString logPath = QCoreApplication::applicationDirPath() + "/bfplayer_runtime.log";
        g_bfLog = fopen(logPath.toUtf8().constData(), "w");
        bfLog("[run] start hasVideo=%d hasAudio=%d isLive=%d", m_hasVideo, m_hasAudio, m_isLiveStream);
    }

    // 在工作线程中初始化音频输出（确保线程亲和性）
    if (m_hasAudio) {
        initAudioOutput();
        bfLog("[run] audio init done swrCtx=%p device=%p vol=%d",
              (void*)m_swrCtx, (void*)m_audioDevice, m_volume.load());
    }

    m_state = Playing;

    // 播放时钟：用于帧率控制，确保视频按正确速率播放
    QElapsedTimer wallClock;
    qint64 clockOffsetMs = 0;   // 时钟起始对应的媒体位置（ms）
    bool clockStarted = false;

    while (!isInterruptionRequested() && m_state != Stopped) {
        // 在 worker 线程中应用音量变更（跨线程安全）
        applyVolumeIfChanged();

        // 处理暂停
        if (m_state == Paused) {
            QMutexLocker lock(&m_mutex);
            m_pauseCond.wait(&m_mutex, 100);
            // 暂停结束后重置时钟，避免累积偏移
            clockStarted = false;
            continue;
        }

        // 处理 seek
        if (m_seekFlag.load()) {
            doSeek(m_seekTarget);
            m_seekFlag.store(false);
            // seek 后重置时钟
            clockStarted = false;
        }

        int ret = av_read_frame(m_fmtCtx, m_pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) { bfLog("[read] EOF -> break"); break; }
            // 网络/其它错误：打印错误码后继续（demuxer 状态可能已损坏，崩溃候选点）
            char errbuf[256]; av_strerror(ret, errbuf, sizeof(errbuf));
            bfLog("[read] ERROR ret=%d (%s) -> continue. V=%lld A=%lld",
                  ret, errbuf, (long long)g_vFrameCount, (long long)g_aFrameCount);
            continue;
        }

        if (m_pkt->stream_index == m_videoStreamIdx && m_hasVideo) {
            ret = avcodec_send_packet(m_videoCodecCtx, m_pkt);
            if (ret < 0) { av_packet_unref(m_pkt); continue; }

            while (avcodec_receive_frame(m_videoCodecCtx, m_frame) >= 0) {
                QImage img = frameToQImage(m_frame);
                if (!img.isNull()) {
                    // 计算视频位置
                    int64_t pts = m_frame->best_effort_timestamp;
                    if (pts != AV_NOPTS_VALUE) {
                        m_positionMs = static_cast<qint64>(av_rescale_q(pts,
                            m_fmtCtx->streams[m_videoStreamIdx]->time_base, {1, 1000}));
                        emit positionChanged(m_positionMs);
                    }
                    g_vFrameCount++;
                    bfLog("[video] #%lld pts=%lld w=%d h=%d fmt=%d posMs=%lld",
                          (long long)g_vFrameCount, (long long)pts,
                          m_frame->width, m_frame->height, m_frame->format,
                          (long long)m_positionMs);
                    // 发送图像
                    emit frameReady(img);

                    // 帧率控制：限制帧率避免 UI 过载
                    if (m_isLiveStream) {
                        // 直播流：限制到约 20fps
                        msleep(50);
                    } else if (pts != AV_NOPTS_VALUE) {
                        if (!clockStarted) {
                            clockOffsetMs = m_positionMs;
                            wallClock.start();
                            clockStarted = true;
                        } else {
                            qint64 frameTimeMs = m_positionMs - clockOffsetMs;
                            qint64 elapsedMs = wallClock.elapsed();
                            qint64 sleepMs = frameTimeMs - elapsedMs;
                            if (sleepMs > 5) {
                                msleep(static_cast<unsigned long>(sleepMs));
                            }
                        }
                    }
                }
            }
        } else if (m_pkt->stream_index == m_audioStreamIdx && m_hasAudio) {
            ret = avcodec_send_packet(m_audioCodecCtx, m_pkt);
            if (ret < 0) { av_packet_unref(m_pkt); continue; }

            while (avcodec_receive_frame(m_audioCodecCtx, m_frame) >= 0) {
                g_aFrameCount++;
                if (g_aFrameCount % 50 == 0)
                    bfLog("[audio] #%lld nb_samples=%d", (long long)g_aFrameCount, m_frame->nb_samples);
                playAudioFrame(m_frame);
                // 更新位置（纯音频时）
                int64_t pts = m_frame->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE && !m_hasVideo) {
                    m_positionMs = static_cast<qint64>(av_rescale_q(pts,
                        m_fmtCtx->streams[m_audioStreamIdx]->time_base, {1, 1000}));
                    emit positionChanged(m_positionMs);
                }
            }
        }

        av_packet_unref(m_pkt);
    }

    // 在工作线程中清理音频资源（线程亲和性要求）
    cleanupAudio();
    m_state = Stopped;
    bfLog("[run] finished normally. V=%lld A=%lld", (long long)g_vFrameCount, (long long)g_aFrameCount);
    if (g_bfLog) { fclose(g_bfLog); g_bfLog = nullptr; }
    emit playbackFinished();
}

// ----------------------------------------------------------------------------
// 私有方法
// ----------------------------------------------------------------------------
bool VideoPlayerThread::initCodec(int streamIdx, AVCodecParameters *codecpar,
                                  AVCodecContext **codecCtx, SwsContext **swsCtx,
                                  int *width, int *height)
{
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        emit mediaOpened(false, tr("找不到解码器: %1").arg(avcodec_get_name(codecpar->codec_id)));
        return false;
    }

    *codecCtx = avcodec_alloc_context3(codec);
    if (!*codecCtx) return false;

    if (avcodec_parameters_to_context(*codecCtx, codecpar) < 0) {
        avcodec_free_context(codecCtx);
        return false;
    }

    // 关闭帧级多线程解码：直播/HLS 等参数可能中途变化的流，FF_THREAD_FRAME 会在
    // 段边界 SPS/分辨率变化时污染解码器缓冲状态，导致几秒后崩溃（问题 1 主因之一）
    (*codecCtx)->thread_count = 1;

    if (avcodec_open2(*codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(codecCtx);
        emit mediaOpened(false, tr("无法打开解码器"));
        return false;
    }

    // 视频：初始化 SWS 上下文
    if (swsCtx && codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        *width  = codecpar->width;
        *height = codecpar->height;
        *swsCtx = sws_getContext(
            codecpar->width, codecpar->height, static_cast<AVPixelFormat>(codecpar->format),
            codecpar->width, codecpar->height, AV_PIX_FMT_RGB32,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    return true;
}

void VideoPlayerThread::initAudioOutput()
{
    AVCodecParameters *par = m_fmtCtx->streams[m_audioStreamIdx]->codecpar;

    QAudioFormat format;
    // 兜底：部分异常 HLS 音频轨 sample_rate 可能为 0，会导致 swr 输出率 0、
    // 后续 m_audioPosMs 除零崩溃，以及 swr 初始化异常。统一回退到 48000。
    int sampleRate = (par->sample_rate > 0) ? par->sample_rate : 48000;
    format.setSampleRate(sampleRate);
    format.setChannelCount(par->ch_layout.nb_channels > 0 ? par->ch_layout.nb_channels : 2);
    format.setCodec("audio/pcm");
    format.setSampleSize(16);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    // 检查音频设备是否可用
    QAudioDeviceInfo defaultDevice = QAudioDeviceInfo::defaultOutputDevice();
    fprintf(stderr, "[Audio] Default device isNull: %d\n", defaultDevice.isNull());
    if (defaultDevice.isNull()) {
        // 没有音频设备，忽略音频
        m_audioOutput = nullptr;
        m_audioDevice = nullptr;
        return;
    }
    fprintf(stderr, "[Audio] Device name: %s\n", defaultDevice.deviceName().toUtf8().constData());

    // 检查格式是否被支持，如果不支持则使用最接近的格式
    if (!defaultDevice.isFormatSupported(format)) {
        format = defaultDevice.nearestFormat(format);
    }

    m_audioOutput = new QAudioOutput(format);
    m_audioOutput->setVolume(m_volume.load() / 100.0);
    // 设置适中的缓冲区大小
    m_audioOutput->setBufferSize(16384);  // 16KB 缓冲区

    // start() 返回一个 QIODevice，直接向它写入 PCM 数据即可播放
    m_audioDevice = m_audioOutput->start();

    if (!m_audioDevice) {
        qWarning() << "Failed to start audio output!";
        delete m_audioOutput;
        m_audioOutput = nullptr;
        return;
    }

    // 根据 QAudioFormat 确定输出采样格式
    m_outChannels = format.channelCount();
    AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16;
    m_outSampleSize = 2;  // 默认 S16 = 2 字节
    if (format.sampleType() == QAudioFormat::Float && format.sampleSize() == 32) {
        outSampleFmt = AV_SAMPLE_FMT_FLT;
        m_outSampleSize = 4;
    } else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 32) {
        outSampleFmt = AV_SAMPLE_FMT_S32;
        m_outSampleSize = 4;
    } else if (format.sampleType() == QAudioFormat::UnSignedInt && format.sampleSize() == 8) {
        outSampleFmt = AV_SAMPLE_FMT_U8;
        m_outSampleSize = 1;
    }
    // else: 默认 S16

    AVChannelLayout outLayout;
    if (m_outChannels == 1) {
        outLayout = AV_CHANNEL_LAYOUT_MONO;
    } else {
        outLayout = AV_CHANNEL_LAYOUT_STEREO;
    }

    // 处理源通道布局可能未指定的情况
    AVChannelLayout srcLayout;
    if (par->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || par->ch_layout.nb_channels == 0) {
        // 源通道布局未指定，根据声道数使用默认布局
        int srcChannels = par->ch_layout.nb_channels > 0 ? par->ch_layout.nb_channels : 2;
        av_channel_layout_default(&srcLayout, srcChannels);
    } else {
        srcLayout = par->ch_layout;
    }

    FILE *logFile = fopen("D:/workspace/BfPlayer/release/audio_debug.log", "a");
    if (logFile) {
        fprintf(logFile, "[Audio] src_sample_rate=%d, dst_sample_rate=%d, channels=%d\n", par->sample_rate, format.sampleRate(), format.channelCount());
        fclose(logFile);
    }

    swr_alloc_set_opts2(&m_swrCtx,
        &outLayout, outSampleFmt, format.sampleRate(),
        &srcLayout, static_cast<AVSampleFormat>(par->format), par->sample_rate,
        0, nullptr);

    if (!m_swrCtx || swr_init(m_swrCtx) < 0) {
        qWarning() << "Failed to initialize audio resampler!";
        swr_free(&m_swrCtx);
    }
}

QImage VideoPlayerThread::frameToQImage(AVFrame *frame)
{
    // 防御：空帧或解码异常（SPS 缺失期间 frame->width/height 可能为 0）→ 跳过，避免崩溃
    if (!frame || !frame->data[0] || frame->width <= 0 || frame->height <= 0) {
        bfLog("[frame] SKIP invalid (data0=%p w=%d h=%d)",
              (void*)(frame ? frame->data[0] : nullptr),
              frame ? frame->width : 0, frame ? frame->height : 0);
        return QImage();
    }
    if (!m_swsCtx) return QImage();

    // 直播流段边界可能改变分辨率/像素格式，必须按实际帧尺寸重建 SwsContext，
    // 否则 sws_scale 会越界写入固定尺寸的 QImage 缓冲，造成堆损坏延迟崩溃（问题 1 主因之二）
    if (frame->width != m_videoWidth || frame->height != m_videoHeight) {
        QMutexLocker lock(&m_mutex);
        if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
        m_videoWidth  = frame->width;
        m_videoHeight = frame->height;
        m_swsCtx = sws_getContext(
            m_videoWidth, m_videoHeight, static_cast<AVPixelFormat>(frame->format),
            m_videoWidth, m_videoHeight, AV_PIX_FMT_RGB32,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        bfLog("[sws] rebuilt ctx=%p for %dx%d fmt=%d", (void*)m_swsCtx, m_videoWidth, m_videoHeight, frame->format);
        if (!m_swsCtx) return QImage();
    }

    int w = m_videoWidth;
    int h = m_videoHeight;

    QImage img(w, h, QImage::Format_RGB32);
    if (img.isNull()) return QImage();
    uint8_t *dst[] = { img.bits() };
    int dstLinesize[] = { static_cast<int>(img.bytesPerLine()) };

    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h, dst, dstLinesize);
    return img;
}

void VideoPlayerThread::playAudioFrame(AVFrame *frame)
{
    if (!m_swrCtx || !m_audioDevice || !m_audioOutput) return;
    if (frame->nb_samples <= 0) return;

    // 分配输出缓冲区并进行重采样
    int outSamples = av_rescale_rnd(swr_get_delay(m_swrCtx, 48000) + frame->nb_samples,
                                    48000, 48000, AV_ROUND_UP);
    int bufSize = outSamples * m_outSampleSize * m_outChannels;
    QByteArray audioData(bufSize, 0);
    uint8_t *outBuf = reinterpret_cast<uint8_t *>(audioData.data());

    int converted = swr_convert(m_swrCtx, &outBuf, outSamples,
                                const_cast<const uint8_t **>(frame->data), frame->nb_samples);
    if (converted > 0) {
        int actualSize = converted * m_outSampleSize * m_outChannels;
        if (actualSize > 0) {
            m_audioDevice->write(audioData.constData(), actualSize);
        }
    }
}

void VideoPlayerThread::applyVolumeIfChanged()
{
    // 在工作线程中应用音量变更，避免跨线程访问 QAudioOutput（线程亲和性要求）
    if (m_volumeChanged.load()) {
        if (m_audioOutput) m_audioOutput->setVolume(m_volume.load() / 100.0);
        m_volumeChanged.store(false);
    }
}

void VideoPlayerThread::doSeek(qint64 targetMs)
{
    if (!m_fmtCtx) return;

    int64_t ts = av_rescale_q(static_cast<int64_t>(targetMs), {1, 1000}, AV_TIME_BASE_Q);
    int ret = av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return;

    if (m_videoCodecCtx) avcodec_flush_buffers(m_videoCodecCtx);
    if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
    // 重置音频输出位置，确保 seek 后同步重新开始
    m_audioPosMs = targetMs;
}
