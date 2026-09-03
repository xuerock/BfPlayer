#ifndef CLIPEXTRACTOR_H
#define CLIPEXTRACTOR_H

#include <QObject>
#include <QString>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
}

/**
 * @brief 视频截取器
 *
 * 使用 FFmpeg 库从视频中截取指定时间段的片段，保存为新文件。
 * 支持两种模式：
 *   - 快速模式 (Stream Copy)：直接复制编码流，速度快、无质量损失，
 *     但起点可能不精确（对齐到关键帧）
 *   - 精确模式 (Re-encode)：重新编码，起点精确，但速度较慢
 *
 * 该对象设计为在子线程中运行，通过信号与 UI 通信。
 */
class ClipExtractor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 截取模式
     */
    enum Mode {
        FastCopy,   ///< 流复制模式（快，起点对齐关键帧）
        Precise     ///< 精确重编码模式（精确到帧，较慢）
    };
    Q_ENUM(Mode)

    explicit ClipExtractor(QObject *parent = nullptr);
    ~ClipExtractor() override;

    /// 设置截取参数
    void setParameters(const QString &inputPath,
                       const QString &outputPath,
                       double startSeconds,
                       double endSeconds,
                       Mode mode = FastCopy);

signals:
    /// 进度更新 (0-100)
    void progressChanged(int percent);
    /// 截取完成（成功或失败）
    void finished(bool success, const QString &message);
    /// 状态描述更新
    void statusMessage(const QString &message);

public slots:
    /// 开始截取（在子线程中调用）
    void startExtraction();
    /// 请求取消截取
    void cancel();

private:
    /// 流复制模式实现
    bool extractByStreamCopy(QString *errorMsg);
    /// 精确重编码模式实现
    bool extractByReencode(QString *errorMsg);

    /// 将秒数转换为指定时基的时间戳
    static int64_t secondsToTimestamp(double seconds, AVRational timeBase);

    QString   m_inputPath;
    QString   m_outputPath;
    double    m_startSec  = 0.0;
    double    m_endSec    = 0.0;
    Mode      m_mode      = FastCopy;
    std::atomic<bool> m_cancelled{false};
};

#endif // CLIPEXTRACTOR_H
