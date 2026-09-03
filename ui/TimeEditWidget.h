#ifndef TIMEEDITWIDGET_H
#define TIMEEDITWIDGET_H

#include <QWidget>

class QSpinBox;
class QLabel;

/**
 * @brief 时:分:秒 格式的时间输入控件
 *
 * 提供 HH:MM:SS 格式的时间输入，支持超过 24 小时。
 * 可获取/设置总毫秒数。
 */
class TimeEditWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimeEditWidget(QWidget *parent = nullptr);

    /// 获取当前时间（毫秒）
    qint64 totalMs() const;
    /// 设置当前时间（毫秒）
    void setTotalMs(qint64 ms);

    /// 设置最大允许时间（毫秒）
    void setMaximum(qint64 maxMs);

signals:
    void timeChanged(qint64 totalMs);

private slots:
    void onValueChanged();

private:
    QSpinBox *m_hourSpin;
    QSpinBox *m_minSpin;
    QSpinBox *m_secSpin;
};

#endif // TIMEEDITWIDGET_H
