#include "ClipExtractor.h"

#include <QThread>
#include <QVector>
#include <QDebug>

ClipExtractor::ClipExtractor(QObject *parent)
    : QObject(parent)
{
}

ClipExtractor::~ClipExtractor() = default;

void ClipExtractor::setParameters(const QString &inputPath,
                                  const QString &outputPath,
                                  double startSeconds,
                                  double endSeconds,
                                  Mode mode)
{
    m_inputPath  = inputPath;
    m_outputPath = outputPath;
    m_startSec   = startSeconds;
    m_endSec     = endSeconds;
    m_mode       = mode;
    m_cancelled  = false;
}

void ClipExtractor::startExtraction()
{
    if (m_inputPath.isEmpty() || m_outputPath.isEmpty()) {
        emit finished(false, tr("输入或输出路径为空"));
        return;
    }
    if (m_endSec <= m_startSec) {
        emit finished(false, tr("结束时间必须大于开始时间"));
        return;
    }

    // 只使用流复制模式，避免链接问题
    QString errorMsg;
    bool ok = extractByStreamCopy(&errorMsg);

    if (m_cancelled) {
        emit finished(false, tr("已取消"));
    } else {
        emit finished(ok, ok ? tr("截取完成") : errorMsg);
    }
}

void ClipExtractor::cancel()
{
    m_cancelled = true;
}

int64_t ClipExtractor::secondsToTimestamp(double seconds, AVRational timeBase)
{
    return static_cast<int64_t>(seconds * timeBase.den / timeBase.num);
}

// ============================================================================
// 流复制模式：直接复制音视频包，速度快、无质量损失
// ============================================================================
bool ClipExtractor::extractByStreamCopy(QString *errorMsg)
{
    AVFormatContext *inFmtCtx  = nullptr;
    AVFormatContext *outFmtCtx = nullptr;
    AVPacket        *pkt       = nullptr;
    int64_t          startPts  = 0;
    int64_t          endPts    = 0;
    int              ret       = 0;

    auto cleanup = [&]() {
        if (inFmtCtx)  avformat_close_input(&inFmtCtx);
        if (outFmtCtx) {
            if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE) && outFmtCtx->pb)
                avio_closep(&outFmtCtx->pb);
            avformat_free_context(outFmtCtx);
        }
        if (pkt) av_packet_free(&pkt);
    };

    // 打开输入文件
    ret = avformat_open_input(&inFmtCtx, m_inputPath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        *errorMsg = tr("无法打开输入文件: %1").arg(QString::fromLocal8Bit(errBuf));
        cleanup();
        return false;
    }

    ret = avformat_find_stream_info(inFmtCtx, nullptr);
    if (ret < 0) {
        *errorMsg = tr("无法获取流信息");
        cleanup();
        return false;
    }

    // 创建输出上下文
    ret = avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr,
                                         m_outputPath.toUtf8().constData());
    if (ret < 0 || !outFmtCtx) {
        *errorMsg = tr("无法创建输出上下文");
        cleanup();
        return false;
    }

    // 复制所有流到输出
    QVector<int> streamIndexMap(inFmtCtx->nb_streams, -1);
    int outStreamIndex = 0;

    for (unsigned int i = 0; i < inFmtCtx->nb_streams; ++i) {
        AVStream *inStream = inFmtCtx->streams[i];
        // 只复制视频、音频、字幕流
        if (inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            inStream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }

        AVStream *outStream = avformat_new_stream(outFmtCtx, nullptr);
        if (!outStream) {
            *errorMsg = tr("无法创建输出流");
            cleanup();
            return false;
        }

        ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        if (ret < 0) {
            *errorMsg = tr("无法复制编解码器参数");
            cleanup();
            return false;
        }
        outStream->codecpar->codec_tag = 0;

        streamIndexMap[i] = outStreamIndex++;
    }

    // 打开输出文件
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, m_outputPath.toUtf8().constData(),
                        AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            *errorMsg = tr("无法打开输出文件: %1").arg(QString::fromLocal8Bit(errBuf));
            cleanup();
            return false;
        }
    }

    // 写入文件头
    ret = avformat_write_header(outFmtCtx, nullptr);
    if (ret < 0) {
        *errorMsg = tr("无法写入文件头");
        cleanup();
        return false;
    }

    // 计算起止时间戳（使用视频流的时基，如果没有视频流则用音频流）
    int videoIdx = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audioIdx = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    int refIdx   = (videoIdx >= 0) ? videoIdx : audioIdx;
    if (refIdx < 0) {
        *errorMsg = tr("未找到视频或音频流");
        cleanup();
        return false;
    }
    AVRational refTimeBase = inFmtCtx->streams[refIdx]->time_base;
    int64_t durationSec = inFmtCtx->duration / AV_TIME_BASE;

    startPts = secondsToTimestamp(m_startSec, refTimeBase);
    endPts   = secondsToTimestamp(m_endSec, refTimeBase);

    // Seek 到起始位置之前的关键帧
    ret = av_seek_frame(inFmtCtx, refIdx, startPts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // seek 失败不致命，从头部开始读
        qWarning() << "Seek failed, reading from beginning";
    }

    emit statusMessage(tr("正在复制流..."));

    // 读取包并写入输出
    pkt = av_packet_alloc();
    int64_t totalPackets = 0;
    int64_t writtenPackets = 0;
    int lastProgress = -1;
    int consecutiveWriteFailures = 0;  // 连续写入失败计数

    while (av_read_frame(inFmtCtx, pkt) >= 0) {
        if (m_cancelled) {
            av_packet_unref(pkt);
            break;
        }

        int inIdx  = pkt->stream_index;
        int outIdx = streamIndexMap[inIdx];

        // 跳过未映射的流
        if (outIdx < 0) {
            av_packet_unref(pkt);
            continue;
        }

        // 检查是否超出结束时间（以参考流为准）
        if (inIdx == refIdx && pkt->pts > endPts) {
            av_packet_unref(pkt);
            break;
        }

        // 调整时间戳
        AVStream *inStream  = inFmtCtx->streams[inIdx];
        AVStream *outStream = outFmtCtx->streams[outIdx];

        pkt->pts      = av_rescale_q_rnd(pkt->pts - secondsToTimestamp(m_startSec, inStream->time_base),
                                        inStream->time_base, outStream->time_base,
                                        static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->dts      = av_rescale_q_rnd(pkt->dts - secondsToTimestamp(m_startSec, inStream->time_base),
                                        inStream->time_base, outStream->time_base,
                                        static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->duration = av_rescale_q(pkt->duration, inStream->time_base, outStream->time_base);
        pkt->pos      = -1;
        pkt->stream_index = outIdx;

        // 确保时间戳单调递增
        if (pkt->pts < 0) pkt->pts = 0;
        if (pkt->dts < 0) pkt->dts = 0;

        ret = av_interleaved_write_frame(outFmtCtx, pkt);
        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            qWarning() << "Error writing packet:" << errBuf;
            // 累计连续失败次数，超过阈值后中止截取
            consecutiveWriteFailures++;
            if (consecutiveWriteFailures >= 10) {
                qCritical() << "Too many consecutive write failures, aborting extraction";
                av_packet_unref(pkt);
                emit finished(false, tr("写入失败次数过多，截取已中止"));
                return false;
            }
        } else {
            writtenPackets++;
            consecutiveWriteFailures = 0;
        }

        // 在 unref 之前保存 pts 用于进度计算
        int64_t savedPts = pkt->pts;

        av_packet_unref(pkt);
        totalPackets++;

        // 更新进度（基于时间估算）
        if (inIdx == refIdx && durationSec > 0) {
            int64_t currentSec = av_rescale_q(savedPts,
                                              outStream->time_base, {1, 1}) / outStream->time_base.den
                                 + static_cast<int>(m_startSec);
            int progress = qBound(0, static_cast<int>((currentSec * 100 / durationSec)), 100);
            if (progress != lastProgress) {
                lastProgress = progress;
                emit progressChanged(progress);
            }
        }

        // 每处理一批包，让出时间片
        if (totalPackets % 1000 == 0) {
            QThread::msleep(1);
        }
    }

    // 写入文件尾
    av_write_trailer(outFmtCtx);

    int finalProgress = lastProgress > 0 ? lastProgress : 100;
    emit progressChanged(finalProgress);

    qDebug() << "Extraction done. Total packets read:" << totalPackets
             << "Written:" << writtenPackets;

    cleanup();
    return !m_cancelled;
}
