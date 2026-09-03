#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class VideoPlayerWidget;
class QSlider;
class QPushButton;
class QLabel;
class QAction;
class QVBoxLayout;
class QTimer;

/**
 * @brief BfPlayer 主窗口
 *
 * 功能：
 *   - 打开并播放音频（MP3、WAV、AAC、FLAC…）和视频（MP4、AVI、MKV…）文件
 *   - 播放控制：播放/暂停、停止、音量、进度拖动
 *   - 视频截取：打开截取对话框，截取指定时间段保存为新文件
 *
 * 使用 FFmpeg 自行解码渲染，不依赖系统解码器，支持几乎所有格式和中文路径。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// 从命令行参数打开文件（用于文件关联双击打开）
    void openFileFromArgs(const QString &filePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void openFile();
    void openNetworkStream();
    void loadAndPlay(const QString &path);
    void openClipDialog();
    void togglePlay();
    void stopPlayback();

    void onMediaOpened(bool success, const QString &message);
    void onStateChanged(int state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onHasVideoChanged(bool hasVideo);
    void onSeek(int value);
    void onVolumeChanged(int value);
    void onRotate();
    void toggleMute();
    void onSeekTimer();
    void doRelativeSeek(qint64 deltaMs);

    // 网络流
    void downloadAndPlay(const QString &url);  // HTTPS 下载后播放

private:
    void setupUi();
    void setupToolBar();
    void setupControlBar(QWidget *central, QVBoxLayout *mainLayout);
    void connectSignals();
    void updateTimeLabel(qint64 current, qint64 total);

    // 播放引擎（FFmpeg 自解码）
    VideoPlayerWidget *m_player = nullptr;

    // 控制栏控件
    QSlider      *m_seekSlider    = nullptr;
    QSlider      *m_volumeSlider  = nullptr;
    QPushButton  *m_muteBtn       = nullptr;  // 禁音按钮
    QPushButton  *m_rotateBtn     = nullptr;
    QLabel       *m_timeLabel     = nullptr;
    QLabel       *m_videoPlaceholder = nullptr;

    // 工具栏动作
    QAction      *m_openAction    = nullptr;
    QAction      *m_networkAction = nullptr;  // 在线播放
    QAction      *m_clipAction    = nullptr;
    QAction      *m_playAction    = nullptr;
    QAction      *m_stopAction    = nullptr;

    // 状态
    qint64        m_durationMs    = 0;
    qint64        m_currentPositionMs = 0;  // UI 层跟踪的当前位置（用于暂停时 seek）
    bool          m_isVideo       = false;
    bool          m_isOpeningFile = false;  // 防止文件对话框重复弹出

    // 禁音功能
    bool          m_isMuted       = true;   // 是否禁音（默认禁音）
    int           m_volumeBeforeMute = 70;  // 禁音前的音量值

    // 键盘快进/快退
    QTimer       *m_seekTimer     = nullptr;  // 连续快进/快退定时器
    int           m_seekDirection = 0;        // 0=无, 1=快进, -1=快退
    static constexpr int SEEK_INTERVAL_MS = 300;      // 连续触发间隔（ms）
    static constexpr int SEEK_STEP_MS     = 3000;     // 每次快进/快退 3 秒
    static constexpr int SEEK_REPEAT_DELAY_MS = 500; // 首次重复前的延迟（ms）
};

#endif // MAINWINDOW_H
