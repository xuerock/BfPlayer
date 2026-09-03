#include "MainWindow.h"
#include "ClipDialog.h"
#include "../core/VideoPlayerWidget.h"
#include "../core/VideoPlayerThread.h"


#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>
#include <QCoreApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QTimer>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QInputDialog>
#include <QProgressDialog>
#include <QDateTime>
#include <QSslSocket>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 设置焦点策略，确保主窗口能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    setupUi();
    setupToolBar();
    connectSignals();

    // 初始状态为禁音，设置播放器音量为 0
    m_player->setVolume(0);

    setWindowTitle(tr("BfPlayer - 媒体播放器"));
    resize(900, 620);
    setAcceptDrops(true);

    // 设置状态栏样式（深色主题）
    statusBar()->setStyleSheet(
        "QStatusBar { background: #2b2b2b; color: #aaa; border-top: 1px solid #444; "
        "padding: 4px; }");
}

MainWindow::~MainWindow() = default;

// ----------------------------------------------------------------------------
// UI 构建
// ----------------------------------------------------------------------------
void MainWindow::setupUi()
{
    // 中央控件
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. 视频播放区
    m_player = new VideoPlayerWidget(this);
    m_player->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 纯音频时的占位标签（叠在 player 上方，无视频时显示）
    m_videoPlaceholder = new QLabel(tr("🎵 音频播放中"), m_player);
    m_videoPlaceholder->setAlignment(Qt::AlignCenter);
    m_videoPlaceholder->setStyleSheet(
        "QLabel { color: #aaa; font-size: 24px; background: transparent; }");
    m_videoPlaceholder->hide();

    mainLayout->addWidget(m_player, 1);

    // 2. 控制栏（需要在 setCentralWidget 之前添加控件）
    setupControlBar(central, mainLayout);

    setCentralWidget(central);
}

void MainWindow::setupControlBar(QWidget *central, QVBoxLayout *mainLayout)
{
    // 控制栏容器：放在主窗口底部
    auto *controlBar = new QFrame;
    controlBar->setObjectName("controlBar");
    controlBar->setFrameShape(QFrame::NoFrame);
    controlBar->setStyleSheet("QFrame#controlBar { background: #2b2b2b; }");

    auto *layout = new QHBoxLayout(controlBar);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    // 左侧留白
    layout->addSpacing(20);

    // 进度滑块
    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setValue(0);
    m_seekSlider->setEnabled(false);
    m_seekSlider->setFocusPolicy(Qt::NoFocus);  // 不抢夺键盘焦点
    m_seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 6px; background: #555; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -4px 0; "
        "background: #4fc3f7; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: #4fc3f7; border-radius: 3px; }");
    layout->addWidget(m_seekSlider, 1);

    // 时间标签
    m_timeLabel = new QLabel("00:00 / 00:00");
    m_timeLabel->setStyleSheet("color: #ccc; font-family: monospace;");
    m_timeLabel->setFixedWidth(120);
    layout->addWidget(m_timeLabel);

    // 旋转按钮
    m_rotateBtn = new QPushButton(tr("↻ 0°"));
    m_rotateBtn->setFixedWidth(50);
    m_rotateBtn->setStyleSheet(
        "QPushButton { color: #ccc; background: #444; border: none; "
        "padding: 3px 6px; border-radius: 4px; }"
        "QPushButton:hover { background: #555; }");
    layout->addWidget(m_rotateBtn);

    // 禁音按钮
    m_muteBtn = new QPushButton(tr("🔇"));
    m_muteBtn->setFixedWidth(30);
    m_muteBtn->setToolTip(tr("禁音"));
    m_muteBtn->setStyleSheet(
        "QPushButton { color: #ff6b6b; background: #444; border: none; "
        "padding: 3px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background: #555; }");
    layout->addWidget(m_muteBtn);

    // 音量
    auto *volLabel = new QLabel(tr("Vol"));
    volLabel->setStyleSheet("color: #ccc;");
    layout->addWidget(volLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(0);  // 默认禁音，音量为 0
    m_volumeSlider->setFixedWidth(75);
    m_volumeSlider->setFocusPolicy(Qt::NoFocus);  // 不抢夺键盘焦点
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #555; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; "
        "background: #aaa; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #aaa; border-radius: 2px; }");
    layout->addWidget(m_volumeSlider);

    // 将控制栏添加到主布局底部
    if (mainLayout) {
        mainLayout->addWidget(controlBar);
    }
}

void MainWindow::setupToolBar()
{
    // 创建动作（原在菜单栏中创建，现移至工具栏）
    m_openAction = new QAction(tr("打开文件"), this);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);
    m_openAction->setShortcut(QKeySequence::Open);

    m_playAction = new QAction(tr("▶ 播放"), this);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlay);
    m_playAction->setShortcut(Qt::Key_Space);
    m_playAction->setEnabled(false);

    m_stopAction = new QAction(tr("⏹ 停止"), this);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopPlayback);
    m_stopAction->setEnabled(false);

    m_networkAction = new QAction(tr("在线播放"), this);
    connect(m_networkAction, &QAction::triggered, this, &MainWindow::openNetworkStream);
    m_networkAction->setShortcut(QKeySequence("Ctrl+U"));

    m_clipAction = new QAction(tr("视频截取"), this);
    connect(m_clipAction, &QAction::triggered, this, &MainWindow::openClipDialog);
    m_clipAction->setShortcut(QKeySequence("Ctrl+K"));
    m_clipAction->setEnabled(false);

    QToolBar *tb = addToolBar(tr("主工具栏"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->setStyleSheet("QToolBar { background: #333; spacing: 4px; padding: 4px; "
                       "border: none; }"
                       "QToolButton { color: #ccc; background: #444; border: none; "
                       "padding: 5px 10px; border-radius: 4px; }"
                       "QToolButton:hover { background: #555; }"
                       "QToolButton:disabled { color: #666; background: #333; }"
                       "QToolButton::menu-indicator { image: none; }");

    tb->addAction(m_openAction);
    tb->addAction(m_networkAction);
    tb->addSeparator();
    tb->addAction(m_playAction);
    // tb->addAction(m_stopAction);  // 屏蔽停止按钮
    tb->addSeparator();
    tb->addAction(m_clipAction);
}

void MainWindow::connectSignals()
{
    // 播放器信号
    connect(m_player, &VideoPlayerWidget::mediaOpened,
            this, &MainWindow::onMediaOpened);
    connect(m_player, &VideoPlayerWidget::stateChanged,
            this, &MainWindow::onStateChanged);
    connect(m_player, &VideoPlayerWidget::positionChanged,
            this, &MainWindow::onPositionChanged);
    connect(m_player, &VideoPlayerWidget::durationChanged,
            this, &MainWindow::onDurationChanged);
    connect(m_player, &VideoPlayerWidget::hasVideoChanged,
            this, &MainWindow::onHasVideoChanged);
    // 点击播放画面 -> 暂停/播放切换
    connect(m_player, &VideoPlayerWidget::clicked,
            this, &MainWindow::togglePlay);

    // 进度滑块 - 安装事件过滤器以支持点击任意位置跳转
    m_seekSlider->installEventFilter(this);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &MainWindow::onSeek);
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this] {
        onSeek(m_seekSlider->value());
    });

    // 禁音按钮
    connect(m_muteBtn, &QPushButton::clicked, this, &MainWindow::toggleMute);

    // 音量滑块
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &MainWindow::onVolumeChanged);

    // 旋转按钮
    connect(m_rotateBtn, &QPushButton::clicked,
            this, &MainWindow::onRotate);

    // 菜单动作
    // 菜单动作已在 setupMenuBar() 的 addAction() 中连接，无需重复
    // connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);
    // connect(m_clipAction, &QAction::triggered, this, &MainWindow::openClipDialog);
    // connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlay);
    // connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopPlayback);

    // 键盘快进/快退定时器
    m_seekTimer = new QTimer(this);
    m_seekTimer->setInterval(SEEK_INTERVAL_MS);
    connect(m_seekTimer, &QTimer::timeout, this, &MainWindow::onSeekTimer);
}

// ----------------------------------------------------------------------------
// 槽函数
// ----------------------------------------------------------------------------
void MainWindow::openFile()
{
    // 防止重复进入
    if (m_isOpeningFile) return;
    m_isOpeningFile = true;

    QString path = QFileDialog::getOpenFileName(this, tr("打开媒体文件"),
                                                QString(),
                                                tr("所有媒体文件 (*.mp3 *.mp4 *.avi *.mkv *.flac *.aac *.wma *.wmv *.mov "
                                                   "*.webm *.flv *.wav *.ogg *.m4a *.m4v *.ts *.3gp);;"
                                                   "视频文件 (*.mp4 *.avi *.mkv *.wmv *.mov *.webm *.flv *.m4v *.ts);;"
                                                   "音频文件 (*.mp3 *.flac *.aac *.wma *.wav *.ogg *.m4a);;"
                                                   "所有文件 (*)"));

    m_isOpeningFile = false;

    if (path.isEmpty()) return;

    loadAndPlay(path);
}

void MainWindow::openFileFromArgs(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    loadAndPlay(filePath);
}

void MainWindow::loadAndPlay(const QString &path)
{
    // 检查是否为网络 URL - 下载后播放
    bool isNetworkUrl = path.startsWith("http://", Qt::CaseInsensitive) ||
                        path.startsWith("https://", Qt::CaseInsensitive) ||
                        path.startsWith("rtmp://", Qt::CaseInsensitive) ||
                        path.startsWith("rtsp://", Qt::CaseInsensitive);

    if (isNetworkUrl) {
        downloadAndPlay(path);
        return;
    }

    // 本地文件直接播放
    if (m_player->open(path)) {
        m_player->play();
        m_playAction->setEnabled(true);
        m_stopAction->setEnabled(true);
        m_seekSlider->setEnabled(true);
        // 保存源文件路径供截取功能使用
        m_player->setProperty("sourcePath", path);
        setWindowTitle(tr("%1 - BfPlayer").arg(QFileInfo(path).fileName()));

        // 新媒体打开后默认禁音
        if (!m_isMuted) {
            toggleMute();
        }
        // 确保音量为 0
        m_volumeSlider->setValue(0);
    }
}

void MainWindow::downloadAndPlay(const QString &url)
{
    // 检查 SSL 支持
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::warning(this, tr("SSL 不支持"),
            tr("当前 Qt 不支持 SSL 加密连接。\n"
               "请确保 OpenSSL 库已正确安装。\n"
               "SSL 库路径: %1").arg(QSslSocket::sslLibraryBuildVersionString()));
        return;
    }

    // 创建网络管理器
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);

    // 创建请求
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(30000); // 30秒超时

    // 发送请求
    QNetworkReply *reply = mgr->get(request);

    // 显示状态
    statusBar()->showMessage(tr("正在下载... %1").arg(url));

    // 使用 QPointer 保护 this 指针，防止窗口关闭后回调访问悬空指针
    QPointer<MainWindow> thisPtr(this);

    // 连接信号
    connect(reply, &QNetworkReply::downloadProgress,
            [thisPtr](qint64 bytesReceived, qint64 bytesTotal) {
        if (thisPtr && bytesTotal > 0) {
            int percent = static_cast<int>(bytesReceived * 100 / bytesTotal);
            thisPtr->statusBar()->showMessage(
                tr("正在下载... %1% (%2/%3 MB)").arg(percent)
                .arg(bytesReceived / 1024.0 / 1024.0, 0, 'f', 1)
                .arg(bytesTotal / 1024.0 / 1024.0, 0, 'f', 1));
        }
    });

    connect(reply, &QNetworkReply::finished, [thisPtr, url, reply, mgr]() {
        // 检查 this 指针是否仍然有效
        if (!thisPtr) {
            reply->deleteLater();
            mgr->deleteLater();
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            // 保存到临时文件，使用无扩展名让 FFmpeg 自动检测格式
            QString tempPath = QDir::tempPath() + "/bfplayer_temp_" +
                               QString::number(QDateTime::currentSecsSinceEpoch());

            QFile file(tempPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();

                // 播放下载的文件
                if (thisPtr->m_player->open(tempPath)) {
                    thisPtr->m_player->play();
                    thisPtr->m_playAction->setEnabled(true);
                    thisPtr->m_seekSlider->setEnabled(true);
                    thisPtr->m_player->setProperty("sourcePath", tempPath);
                    thisPtr->setWindowTitle(tr("%1 - BfPlayer").arg(tr("在线视频")));

                    // 新媒体打开后默认禁音
                    if (!thisPtr->m_isMuted) {
                        thisPtr->toggleMute();
                    }
                    thisPtr->m_volumeSlider->setValue(0);
                    thisPtr->statusBar()->showMessage(tr("就绪"), 3000);
                } else {
                    thisPtr->statusBar()->showMessage(tr("播放失败"));
                    QMessageBox::warning(thisPtr, tr("播放失败"), tr("无法播放下载的文件"));
                }
            } else {
                thisPtr->statusBar()->showMessage(tr("保存失败"));
                QMessageBox::warning(thisPtr, tr("保存失败"), tr("无法保存临时文件"));
            }
        } else {
            thisPtr->statusBar()->showMessage(tr("下载失败"));
            QMessageBox::warning(thisPtr, tr("下载失败"),
                                 tr("错误: %1\n\nURL: %2").arg(reply->errorString()).arg(url));
        }

        reply->deleteLater();
        mgr->deleteLater();
    });

    connect(reply, &QNetworkReply::sslErrors, [thisPtr, reply](const QList<QSslError> &) {
        // 自动忽略 SSL 证书错误（仅用于测试）
        if (thisPtr) {
            reply->ignoreSslErrors();
        }
    });
}

void MainWindow::openClipDialog()
{
    // 获取当前源文件路径
    QString sourcePath = m_player->property("sourcePath").toString();
    if (sourcePath.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先打开一个视频文件"));
        return;
    }

    // 检查是否为视频文件
    if (!m_player->hasVideo()) {
        QMessageBox::warning(this, tr("提示"), tr("当前文件不是视频，无法截取"));
        return;
    }

    ClipDialog dlg(sourcePath, m_player->duration(), m_player->position(), m_player, this);
    dlg.exec();  // 模态对话框，关闭后自动返回
}

void MainWindow::togglePlay()
{
    if (!m_player->isOpened()) return;

    if (m_player->isPlaying()) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

void MainWindow::stopPlayback()
{
    m_player->stop();
    m_seekSlider->setValue(0);
    updateTimeLabel(0, m_durationMs);
}

void MainWindow::onMediaOpened(bool success, const QString &message)
{
    if (!success) {
        QMessageBox::warning(this, tr("打开失败"), message);
    }
}

void MainWindow::onStateChanged(int state)
{
    switch (state) {
    case VideoPlayerThread::Playing:
        m_playAction->setText(tr("⏸ 暂停"));
        break;
    case VideoPlayerThread::Paused:
        m_playAction->setText(tr("▶ 播放"));
        break;
    default: // Stopped
        m_playAction->setText(tr("▶ 播放"));
        break;
    }
}

void MainWindow::onPositionChanged(qint64 position)
{
    // 更新 UI 层跟踪的位置
    m_currentPositionMs = position;
    if (m_seekSlider->maximum() > 0) {
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(static_cast<int>(position));
        m_seekSlider->blockSignals(false);
    }
    updateTimeLabel(position, m_durationMs);
}

void MainWindow::onDurationChanged(qint64 duration)
{
    m_durationMs = duration;
    m_seekSlider->blockSignals(true);
    m_seekSlider->setMaximum(static_cast<int>(duration));
    m_seekSlider->blockSignals(false);
}

void MainWindow::onHasVideoChanged(bool hasVideo)
{
    m_isVideo = hasVideo;
    m_clipAction->setEnabled(hasVideo);
    m_videoPlaceholder->setVisible(!hasVideo);
}

void MainWindow::onSeek(int value)
{
    m_currentPositionMs = static_cast<qint64>(value);
    m_player->seek(static_cast<qint64>(value));
    updateTimeLabel(static_cast<qint64>(value), m_durationMs);
}

void MainWindow::onVolumeChanged(int value)
{
    m_player->setVolume(value);
}

void MainWindow::onRotate()
{
    int cur = m_player->rotation();
    int next = (cur + 90) % 360;
    m_player->setRotation(next);
    m_rotateBtn->setText(tr("↻ %1°").arg(next));
}

// ----------------------------------------------------------------------------
// 拖放支持
// ----------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasUrls()) return;

    QList<QUrl> urls = mime->urls();
    if (urls.isEmpty()) return;

    QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;

    if (m_player->open(path)) {
        m_player->play();
        m_playAction->setEnabled(true);
        m_stopAction->setEnabled(true);
        m_seekSlider->setEnabled(true);
        m_player->setProperty("sourcePath", path);
        setWindowTitle(tr("%1 - BfPlayer").arg(QFileInfo(path).fileName()));
    }
}

// ----------------------------------------------------------------------------
// 工具函数
// ----------------------------------------------------------------------------
void MainWindow::updateTimeLabel(qint64 current, qint64 total)
{
    auto fmt = [](qint64 ms) -> QString {
        int totalSec = static_cast<int>(ms / 1000);
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        if (h > 0) return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
        return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    };
    m_timeLabel->setText(QString("%1 / %2").arg(fmt(current)).arg(fmt(total)));
}

// ----------------------------------------------------------------------------
// 禁音功能
// ----------------------------------------------------------------------------
void MainWindow::toggleMute()
{
    if (m_isMuted) {
        // 取消禁音，恢复音量
        m_isMuted = false;
        m_muteBtn->setText(tr("🔊"));
        m_muteBtn->setToolTip(tr("禁音"));
        m_muteBtn->setStyleSheet(
            "QPushButton { color: #ccc; background: #444; border: none; "
            "padding: 3px; border-radius: 4px; font-size: 14px; }"
            "QPushButton:hover { background: #555; }");
        m_volumeSlider->setValue(m_volumeBeforeMute);
    } else {
        // 禁音，保存当前音量
        m_isMuted = true;
        m_volumeBeforeMute = m_volumeSlider->value();
        m_muteBtn->setText(tr("🔇"));
        m_muteBtn->setToolTip(tr("取消禁音"));
        m_muteBtn->setStyleSheet(
            "QPushButton { color: #ff6b6b; background: #444; border: none; "
            "padding: 3px; border-radius: 4px; font-size: 14px; }"
            "QPushButton:hover { background: #555; }");
        m_volumeSlider->setValue(0);
    }
}

// ----------------------------------------------------------------------------
// 进度条点击跳转
// ----------------------------------------------------------------------------
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_seekSlider && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_seekSlider->isEnabled()) {
            // 根据点击位置计算对应的时间
            int sliderWidth = m_seekSlider->width();
            int pos = mouseEvent->pos().x();
            // 限制在有效范围内
            pos = qBound(0, pos, sliderWidth);
            // 计算对应的时间值
            qint64 duration = m_player->duration();
            if (duration > 0) {
                qint64 newPos = static_cast<qint64>(static_cast<qreal>(pos) / sliderWidth * duration);
                newPos = qBound(0LL, newPos, duration);
                m_player->seek(newPos);
                m_seekSlider->setValue(static_cast<int>(newPos));
                updateTimeLabel(newPos, duration);
            }
            // 返回 false 让事件继续传播，保持滑块拖动功能
            return false;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ----------------------------------------------------------------------------
// 键盘快进/快退
// ----------------------------------------------------------------------------
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // 只有播放器打开时才响应方向键
    if (!m_player->isOpened()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Left && m_seekDirection != -1) {
        m_seekDirection = -1;
        // 立即执行一次快退（3秒）
        doRelativeSeek(-SEEK_STEP_MS);
        // 延迟后开始连续快退
        QTimer::singleShot(SEEK_REPEAT_DELAY_MS, this, [this] {
            if (m_seekDirection == -1) {
                m_seekTimer->start();
            }
        });
        return;
    } else if (event->key() == Qt::Key_Right && m_seekDirection != 1) {
        m_seekDirection = 1;
        // 立即执行一次快进（3秒）
        doRelativeSeek(SEEK_STEP_MS);
        // 延迟后开始连续快进
        QTimer::singleShot(SEEK_REPEAT_DELAY_MS, this, [this] {
            if (m_seekDirection == 1) {
                m_seekTimer->start();
            }
        });
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        m_seekTimer->stop();
        m_seekDirection = 0;
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::onSeekTimer()
{
    if (m_seekDirection != 0) {
        doRelativeSeek(m_seekDirection * SEEK_STEP_MS);
    }
}

void MainWindow::doRelativeSeek(qint64 deltaMs)
{
    // 使用 UI 层跟踪的位置，而不是 m_player->position()
    // 因为暂停时工作线程不会更新位置
    qint64 newPos = m_currentPositionMs + deltaMs;
    newPos = qBound(0LL, newPos, m_durationMs);
    m_currentPositionMs = newPos;
    m_player->seek(newPos);
    updateTimeLabel(newPos, m_durationMs);
}

// ============================================================================
// 在线播放
// ============================================================================
void MainWindow::openNetworkStream()
{
    // 简单的 URL 输入对话框
    QDialog dlg(this);
    dlg.setWindowTitle(tr("在线播放"));
    dlg.setMinimumWidth(500);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QLabel *label = new QLabel(tr("请输入网络流地址（支持 HTTP/HLS/RTMP/RTSP 等）："), &dlg);
    layout->addWidget(label);

    QLineEdit *urlEdit = new QLineEdit(&dlg);
    urlEdit->setPlaceholderText(tr("例如：http://example.com/live/stream.m3u8"));
    // 每次打开都清空，不预填历史记录
    layout->addWidget(urlEdit);

    // 常用示例
    QLabel *exampleLabel = new QLabel(
        tr("支持的协议：HTTP/HTTPS (.mp4/.mkv)、HLS (.m3u8)、RTMP、RTSP、DASH (.mpd)"),
        &dlg);
    exampleLabel->setStyleSheet("color: #888; font-size: 11px;");
    exampleLabel->setWordWrap(true);
    layout->addWidget(exampleLabel);

    // 按钮
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
    QPushButton *okBtn = new QPushButton(tr("播放"), &dlg);
    okBtn->setDefault(true);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() == QDialog::Accepted) {
        QString url = urlEdit->text().trimmed();
        if (!url.isEmpty()) {
            loadAndPlay(url);
        }
    }
}
