#ifndef VIDEOPLAYERWIDGET_H
#define VIDEOPLAYERWIDGET_H

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <atomic>

class VideoPlayerThread;

/**
 * @brief 基于 FFmpeg 的视频播放控件
 *
 * 自行解码渲染，不依赖系统解码器，支持几乎所有视频格式。
 * 音频通过 QAudioOutput 输出。
 *
 * 信号：
 *   durationChanged(qint64 ms)   - 总时长变化
 *   positionChanged(qint64 ms)   - 当前播放位置变化
 *   stateChanged(int state)      - 播放状态变化 (0=停止 1=播放 2=暂停)
 *   hasVideoChanged(bool has)    - 是否为视频文件（vs 纯音频）
 */
class VideoPlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayerWidget(QWidget *parent = nullptr);
    ~VideoPlayerWidget() override;

    /// 打开文件（支持中文路径）
    bool open(const QString &filePath);
    /// 关闭当前文件
    void closeMedia();

    /// 播放控制
    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);

    /// 音量 0-100
    void setVolume(int volume);
    int volume() const;

    /// 画面旋转角度 (0, 90, 180, 270)
    void setRotation(int degrees);
    int rotation() const;

    /// 当前状态
    bool isPlaying() const;
    bool isPaused() const;
    qint64 duration() const;
    qint64 position() const;
    bool hasVideo() const;
    bool isOpened() const;

signals:
    void durationChanged(qint64 durationMs);
    void positionChanged(qint64 positionMs);
    void stateChanged(int state); // 0=Stopped, 1=Playing, 2=Paused
    void hasVideoChanged(bool hasVideo);
    void mediaOpened(bool success, const QString &message);
    void rotationChanged(int degrees);
    void clicked();  // 点击播放画面信号

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onFrameReady(const QImage &frame);
    void onPlaybackFinished();
    void onSeekFrameReady(const QImage &frame);

private:
    void updateFrame(const QImage &frame);

    VideoPlayerThread *m_thread = nullptr;

    // 当前帧
    QImage  m_currentFrame;
    QMutex  m_frameMutex;

    // 状态
    std::atomic<bool> m_hasVideo{false};
    bool   m_isOpened  = false;
    qint64 m_durationMs = 0;
    int    m_volume    = 70;
    int    m_rotation  = 0;   // 0, 90, 180, 270
};

#endif // VIDEOPLAYERWIDGET_H
