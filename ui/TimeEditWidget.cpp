#include "TimeEditWidget.h"

#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QStyle>

/// 补零显示的 SpinBox：始终显示至少2位数字（如 00、05、12）
class PaddedSpinBox : public QSpinBox
{
public:
    explicit PaddedSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {}
protected:
    QString textFromValue(int value) const override
    {
        return QString("%1").arg(value, 2, 10, QChar('0'));
    }
};

TimeEditWidget::TimeEditWidget(QWidget *parent)
    : QWidget(parent)
{
    // 设置控件容器背景为浅色
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#f5f5f5"));
    setPalette(pal);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_hourSpin = new PaddedSpinBox(this);
    m_hourSpin->setRange(0, 999);
    m_hourSpin->setFixedWidth(50);
    m_hourSpin->setAlignment(Qt::AlignRight);

    m_minSpin = new PaddedSpinBox(this);
    m_minSpin->setRange(0, 59);
    m_minSpin->setFixedWidth(40);
    m_minSpin->setAlignment(Qt::AlignRight);

    m_secSpin = new PaddedSpinBox(this);
    m_secSpin->setRange(0, 59);
    m_secSpin->setFixedWidth(40);
    m_secSpin->setAlignment(Qt::AlignRight);

    // 补零显示
    m_hourSpin->setWrapping(false);
    m_minSpin->setWrapping(false);
    m_secSpin->setWrapping(false);

    layout->addWidget(m_hourSpin);
    layout->addWidget(new QLabel(":", this));
    layout->addWidget(m_minSpin);
    layout->addWidget(new QLabel(":", this));
    layout->addWidget(m_secSpin);
    layout->addStretch();

    // 浅色风格（与对话框背景协调）
    setStyleSheet(
        "QSpinBox { border: 1px solid #bbb; background: #ffffff; color: #333; "
        "padding: 4px; border-radius: 3px; }"
        "QSpinBox:focus { border-color: #4fc3f7; }"
        "QSpinBox::up-button { width: 16px; }"
        "QSpinBox::down-button { width: 16px; }"
        "QLabel { color: #666; font-weight: bold; }");
}

qint64 TimeEditWidget::totalMs() const
{
    int h = m_hourSpin->value();
    int m = m_minSpin->value();
    int s = m_secSpin->value();
    return static_cast<qint64>(h) * 3600000LL + static_cast<qint64>(m) * 60000LL + static_cast<qint64>(s) * 1000LL;
}

void TimeEditWidget::setTotalMs(qint64 ms)
{
    // 阻塞信号避免重复触发
    m_hourSpin->blockSignals(true);
    m_minSpin->blockSignals(true);
    m_secSpin->blockSignals(true);

    int totalSec = static_cast<int>(ms / 1000);
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;

    m_hourSpin->setValue(h);
    m_minSpin->setValue(m);
    m_secSpin->setValue(s);

    m_hourSpin->blockSignals(false);
    m_minSpin->blockSignals(false);
    m_secSpin->blockSignals(false);

    emit timeChanged(totalMs());
}

void TimeEditWidget::setMaximum(qint64 maxMs)
{
    int totalSec = static_cast<int>(maxMs / 1000);
    int maxH = totalSec / 3600;
    m_hourSpin->setMaximum(maxH);
}

void TimeEditWidget::onValueChanged()
{
    // 确保分钟和秒不超过 59
    if (m_minSpin->value() > 59) m_minSpin->setValue(59);
    if (m_secSpin->value() > 59) m_secSpin->setValue(59);
    emit timeChanged(totalMs());
}
