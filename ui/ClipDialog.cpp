#include "ClipDialog.h"
#include "core/ClipExtractor.h"
#include "core/VideoPlayerWidget.h"
#include "TimeEditWidget.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QGroupBox>

namespace {
/// 毫秒 → 显示用字符串 (HH:MM:SS.xxx)
QString msToString(qint64 ms)
{
    int totalSec = static_cast<int>(ms / 1000);
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    int frac = static_cast<int>(ms % 1000);
    return QString("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(frac, 3, 10, QChar('0'));
}
}

ClipDialog::ClipDialog(const QString &sourceFilePath,
                       qint64 durationMs,
                       qint64 currentPositionMs,
                       VideoPlayerWidget *player,
                       QWidget *parent)
    : QDialog(parent)
    , m_sourcePath(sourceFilePath)
    , m_durationMs(durationMs)
    , m_player(player)
{
    setupUi();

    // 默认值
    m_startEdit->setTotalMs(currentPositionMs);
    m_endEdit->setTotalMs(durationMs);
    m_startEdit->setMaximum(durationMs);
    m_endEdit->setMaximum(durationMs);
    m_outputEdit->setText(defaultOutputPath());

    setWindowTitle(tr("视频截取"));
    setMinimumWidth(500);
    resize(540, 320);
}

ClipDialog::~ClipDialog()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        if (m_extractor) m_extractor->cancel();
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
    // 工作线程事件循环已停止，m_extractor 的 deleteLater 不会执行，必须手动释放避免内存泄漏
    // （QPointer：若已通过正常完成的 deleteLater 释放则自动为 nullptr，delete 安全）
    delete m_extractor;
    delete m_workerThread;
}

void ClipDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // ---- 源文件信息 ----
    auto *infoGroup = new QGroupBox(tr("源文件"), this);
    auto *infoLayout = new QGridLayout(infoGroup);
    m_durationLabel = new QLabel(tr("总时长: %1").arg(msToString(m_durationMs)), this);
    // 截断过长的路径显示
    QString displayPath = m_sourcePath;
    if (displayPath.length() > 60) {
        displayPath = displayPath.left(30) + "..." + displayPath.right(27);
    }
    infoLayout->addWidget(new QLabel(tr("文件: %1").arg(displayPath)), 0, 0);
    infoLayout->addWidget(m_durationLabel, 1, 0);
    mainLayout->addWidget(infoGroup);

    // ---- 时间设置（时:分:秒 格式）----
    auto *timeGroup = new QGroupBox(tr("截取范围"), this);
    auto *timeLayout = new QGridLayout(timeGroup);

    m_startEdit = new TimeEditWidget(this);
    m_endEdit   = new TimeEditWidget(this);

    m_setStartBtn = new QPushButton(tr("设为当前"), this);
    m_setEndBtn   = new QPushButton(tr("设为当前"), this);

    timeLayout->addWidget(new QLabel(tr("起始时间:")), 0, 0);
    timeLayout->addWidget(m_startEdit, 0, 1);
    timeLayout->addWidget(m_setStartBtn, 0, 2);
    timeLayout->addWidget(new QLabel(tr("结束时间:")), 1, 0);
    timeLayout->addWidget(m_endEdit, 1, 1);
    timeLayout->addWidget(m_setEndBtn, 1, 2);

    mainLayout->addWidget(timeGroup);

    // ---- 输出设置 ----
    auto *outputGroup = new QGroupBox(tr("输出设置"), this);
    auto *outputLayout = new QGridLayout(outputGroup);

    m_outputEdit = new QLineEdit(this);
    m_browseBtn  = new QPushButton(tr("浏览..."), this);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("快速复制（推荐，无损）"), static_cast<int>(ClipExtractor::FastCopy));
    m_modeCombo->addItem(tr("精确重编码（慢，精确到帧）"), static_cast<int>(ClipExtractor::Precise));

    outputLayout->addWidget(new QLabel(tr("输出路径:")), 0, 0);
    outputLayout->addWidget(m_outputEdit, 0, 1);
    outputLayout->addWidget(m_browseBtn, 0, 2);
    outputLayout->addWidget(new QLabel(tr("模式:")), 1, 0);
    outputLayout->addWidget(m_modeCombo, 1, 1);

    mainLayout->addWidget(outputGroup);

    // ---- 进度 ----
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(tr("就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    // ---- 按钮 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_startBtn = new QPushButton(tr("开始截取"), this);
    m_startBtn->setDefault(true);
    m_cancelBtn = new QPushButton(tr("关闭"), this);
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    // ---- 信号连接 ----
    connect(m_setStartBtn, &QPushButton::clicked, this, &ClipDialog::onSetStartToCurrent);
    connect(m_setEndBtn, &QPushButton::clicked, this, &ClipDialog::onSetEndToCurrent);
    connect(m_browseBtn, &QPushButton::clicked, this, &ClipDialog::onBrowseOutput);
    connect(m_startBtn, &QPushButton::clicked, this, &ClipDialog::onStartExtraction);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ClipDialog::onCancelExtraction);
    connect(m_startEdit, &TimeEditWidget::timeChanged, this, &ClipDialog::onTimeChanged);
    connect(m_endEdit, &TimeEditWidget::timeChanged, this, &ClipDialog::onTimeChanged);
}

void ClipDialog::onSetStartToCurrent()
{
    if (m_player) {
        m_startEdit->setTotalMs(m_player->position());
    }
    updateStartEndLimits();
}

void ClipDialog::onSetEndToCurrent()
{
    if (m_player) {
        m_endEdit->setTotalMs(m_player->position());
    }
    updateStartEndLimits();
}

void ClipDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("保存截取片段"),
                                                m_outputEdit->text(),
                                                tr("MP4 视频 (*.mp4);;MKV 视频 (*.mkv);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        m_outputEdit->setText(path);
    }
}

void ClipDialog::onTimeChanged()
{
    // 时间变化时自动更新输出文件名（仅当用户未手动修改路径时）
    m_outputEdit->setText(defaultOutputPath());
}

void ClipDialog::onStartExtraction()
{
    qint64 startMs = m_startEdit->totalMs();
    qint64 endMs   = m_endEdit->totalMs();
    QString outPath = m_outputEdit->text().trimmed();

    if (endMs <= startMs) {
        QMessageBox::warning(this, tr("参数错误"), tr("结束时间必须大于起始时间"));
        return;
    }
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("参数错误"), tr("请指定输出文件路径"));
        return;
    }

    // 创建后台线程和工作对象
    m_workerThread = new QThread(this);
    m_extractor    = new ClipExtractor();
    m_extractor->moveToThread(m_workerThread);

    // 设置参数（秒数）
    m_extractor->setParameters(m_sourcePath, outPath,
                               startMs / 1000.0, endMs / 1000.0,
                               static_cast<ClipExtractor::Mode>(
                                   m_modeCombo->currentData().toInt()));

    // 信号连接
    connect(m_workerThread, &QThread::started, m_extractor, &ClipExtractor::startExtraction);
    connect(m_extractor, &ClipExtractor::progressChanged, this, &ClipDialog::onExtractionProgress);
    connect(m_extractor, &ClipExtractor::statusMessage, this, &ClipDialog::onStatusMessage);
    connect(m_extractor, &ClipExtractor::finished, this, &ClipDialog::onExtractionFinished);

    // 清理
    connect(m_extractor, &ClipExtractor::finished, m_workerThread, &QThread::quit);
    connect(m_extractor, &ClipExtractor::finished, m_extractor, &ClipExtractor::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QThread::deleteLater);

    // 更新 UI 状态
    m_startBtn->setEnabled(false);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("正在截取..."));
    m_cancelBtn->setText(tr("取消"));

    m_workerThread->start();
}

void ClipDialog::onCancelExtraction()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        m_extractor->cancel();
        m_statusLabel->setText(tr("正在取消..."));
    } else {
        // 没有正在进行的任务，直接关闭对话框
        done(QDialog::Rejected);
    }
}

void ClipDialog::onExtractionProgress(int percent)
{
    m_progressBar->setValue(percent);
}

void ClipDialog::onStatusMessage(const QString &message)
{
    m_statusLabel->setText(message);
}

void ClipDialog::onExtractionFinished(bool success, const QString &message)
{
    m_startBtn->setEnabled(true);
    m_cancelBtn->setText(tr("关闭"));
    m_statusLabel->setText(message);

    if (success) {
        m_progressBar->setValue(100);
        QMessageBox::information(this, tr("完成"), tr("视频截取完成！\n已保存到: %1")
                                 .arg(m_outputEdit->text()));
    } else if (message != tr("已取消")) {
        QMessageBox::critical(this, tr("错误"), message);
    }
}

void ClipDialog::updateStartEndLimits()
{
    // 确保起始 <= 结束
    qint64 startMs = m_startEdit->totalMs();
    qint64 endMs   = m_endEdit->totalMs();
    if (startMs > endMs) {
        m_startEdit->setTotalMs(endMs);
    }
}

QString ClipDialog::defaultOutputPath() const
{
    // 在源文件同目录下生成默认输出文件名
    // 格式：原文件名(无扩展名)-开始时间-结束时间.扩展名
    // 时间格式：HHMMSS（时分秒，6位紧凑数字）
    QFileInfo fi(m_sourcePath);
    QString base = fi.completeBaseName();
    QString suffix = fi.suffix().isEmpty() ? "mp4" : fi.suffix();
    qint64 startMs = m_startEdit->totalMs();
    qint64 endMs   = m_endEdit->totalMs();
    auto fmtTime = [](qint64 ms) -> QString {
        int totalSec = static_cast<int>(ms / 1000);
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        return QString("%1%2%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    };
    return fi.absolutePath() + "/" + base + "-" + fmtTime(startMs) + "-" + fmtTime(endMs) + "." + suffix;
}

void ClipDialog::setCurrentTime(qint64 currentMs, qint64 durationMs)
{
    m_startEdit->setTotalMs(currentMs);
    m_startEdit->setMaximum(durationMs);
    m_endEdit->setMaximum(durationMs);
    updateStartEndLimits();
}

void ClipDialog::closeEvent(QCloseEvent *event)
{
    if (m_workerThread && m_workerThread->isRunning()) {
        m_extractor->cancel();
        m_workerThread->quit();
        m_workerThread->wait(2000);
    }
    QDialog::closeEvent(event);
}
