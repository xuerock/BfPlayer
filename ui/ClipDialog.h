#ifndef CLIPDIALOG_H
#define CLIPDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QThread>

class QLabel;
class QPushButton;
class QProgressBar;
class QComboBox;
class QLineEdit;
class TimeEditWidget;
class VideoPlayerWidget;

class ClipExtractor;

/**
 * @brief 视频截取对话框
 *
 * 用户在此设置截取参数：
 *   - 起始时间 / 结束时间（时:分:秒 格式）
 *   - 截取模式（快速复制 / 精确重编码）
 *   - 输出文件路径
 *
 * 执行截取时在后台线程运行 ClipExtractor，UI 保持响应。
 */
class ClipDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClipDialog(const QString &sourceFilePath,
                        qint64 durationMs,
                        qint64 currentPositionMs,
                        VideoPlayerWidget *player,
                        QWidget *parent = nullptr);
    ~ClipDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSetStartToCurrent();
    void onSetEndToCurrent();
    void onBrowseOutput();
    void onTimeChanged();
    void onStartExtraction();
    void onCancelExtraction();
    void onExtractionProgress(int percent);
    void onStatusMessage(const QString &message);
    void onExtractionFinished(bool success, const QString &message);

private:
    void setupUi();
    void updateStartEndLimits();
    QString defaultOutputPath() const;
    /// 更新起始/结束时间为播放器当前位置（由外部调用）
    void setCurrentTime(qint64 currentMs, qint64 durationMs);

    // 输入信息
    QString m_sourcePath;
    qint64  m_durationMs;
    VideoPlayerWidget *m_player = nullptr;

    // UI 控件
    QLabel        *m_durationLabel    = nullptr;
    TimeEditWidget *m_startEdit       = nullptr;
    TimeEditWidget *m_endEdit         = nullptr;
    QPushButton   *m_setStartBtn      = nullptr;
    QPushButton   *m_setEndBtn        = nullptr;
    QLineEdit     *m_outputEdit       = nullptr;
    QPushButton   *m_browseBtn        = nullptr;
    QComboBox     *m_modeCombo        = nullptr;
    QProgressBar  *m_progressBar      = nullptr;
    QPushButton   *m_startBtn         = nullptr;
    QPushButton   *m_cancelBtn        = nullptr;
    QLabel        *m_statusLabel      = nullptr;

    // 后台截取（使用 QPointer 自动置空，防止悬空指针）
    QPointer<QThread>       m_workerThread;
    QPointer<ClipExtractor> m_extractor;
};

#endif // CLIPDIALOG_H
