#ifndef VIDEOPLAYERTHREAD_H
#define VIDEOPLAYERTHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QIODevice>
#include <QAudioOutput>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

/**
 * @brief FFmpeg 解码线程
 *
 * 在后台线程中解码视频帧和音频帧。
 * 视频帧通过 frameReady 信号发送 QImage。
 * 音频帧通过 QAudioOutput 直接播放。
 *
 * 支持播放、暂停、停止、seek 操作。
 */
class VideoPlayerWidget;  // 前向声明

class VideoPlayerThread : public QThread
{
    Q_OBJECT

    friend class VideoPlayerWidget;  // 允许 VideoPlayerWidget 访问私有成员

public:
    enum State { Stopped, Playing, Paused };

    explicit VideoPlayerThread(QObject *parent = nullptr);
    ~VideoPlayerThread() override;

    /// 打开媒体文件
    bool openFile(const QString &path);
    /// 关闭当前文件
    void closeFile();

    /// 播放控制
    void play();
    void pause();
    void stop();
    void seekTo(qint64 ms);
    void setVolume(int vol);

    /// 状态查询
    qint64 duration() const;
    qint64 position() const;
    bool   isPlaying() const;
    bool   isPaused()  const;
    bool   hasVideo()  const;
    bool   isOpened()  const;
    /// 获取音频输出位置（用于音视频同步）
    qint64 audioPosition() const;

signals:
    void frameReady(const QImage &frame);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void hasVideoChanged(bool hasVideo);
    void playbackFinished();
    void mediaOpened(bool success, const QString &message);
    void seekFrameReady(const QImage &frame);  // 暂停 seek 完成的帧

protected:
    void run() override;

private:
    bool initCodec(int streamIdx, AVCodecParameters *codecpar,
                   AVCodecContext **codecCtx, SwsContext **swsCtx,
                   int *width, int *height);
    void initAudioOutput();
    void cleanupAudio();
    QImage frameToQImage(AVFrame *frame);
    void playAudioFrame(AVFrame *frame);
    void doSeek(qint64 targetMs);
    void applyVolumeIfChanged();  // 在工作线程中安全应用音量变更（避免跨线程访问 QAudioOutput）

    // FFmpeg 上下文
    AVFormatContext *m_fmtCtx        = nullptr;
    AVCodecContext  *m_videoCodecCtx = nullptr;
    AVCodecContext  *m_audioCodecCtx = nullptr;
    SwsContext      *m_swsCtx        = nullptr;
    SwrContext      *m_swrCtx        = nullptr;
    AVPacket        *m_pkt           = nullptr;
    AVFrame         *m_frame         = nullptr;

    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;
    int m_videoWidth     = 0;
    int m_videoHeight    = 0;
    int m_outChannels    = 2;  // 输出声道数（SwrContext 不透明，需单独记录）
    int m_outSampleSize  = 2;  // 输出采样大小（字节）：S16=2, S32=4, FLT=4

    // 播放控制
    std::atomic<State> m_state{Stopped};
    std::atomic<bool>  m_seekFlag{false};
    qint64  m_seekTarget = 0;
    qint64  m_positionMs = 0;
    qint64  m_audioPosMs = 0;
    qint64  m_durationMs = 0;
    bool    m_hasVideo   = false;
    bool    m_hasAudio   = false;
    bool    m_isLiveStream = false;
    std::atomic<int>  m_volume{70};           // 音量（原子：主线程写 / 工作线程读）
    std::atomic<bool> m_volumeChanged{false}; // 音量变更标志（跨线程安全应用）

    // 线程同步
    QMutex m_mutex;
    QWaitCondition m_pauseCond;

    // 音频输出
    QAudioOutput *m_audioOutput = nullptr;
    QIODevice    *m_audioDevice = nullptr;  // QAudioOutput::start() 返回的写入设备
};

#endif // VIDEOPLAYERTHREAD_H
