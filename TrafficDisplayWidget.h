#ifndef TRAFFICDISPLAYWIDGET_H
#define TRAFFICDISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QUdpSocket>
#include "AircraftManager.h" // 假设你原本的包含文件是这个

// =========================================================
// 界面模式枚举
// =========================================================
enum InterfaceMode {
    MODE_BASELINE_A = 1,        // 界面 A: 传统 2D 框，无预测
    MODE_PROPOSED_B_GLOW = 2,   // 界面 B (设计 1): 光晕核心 3D 隧道
    MODE_PROPOSED_B_RIBBED = 3  // 界面 B (设计 2): 半透明渐变带骨架 3D 隧道
};

class TrafficDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrafficDisplayWidget(AircraftManager *manager, QWidget *parent = nullptr);
    ~TrafficDisplayWidget() = default;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private slots:
    void onRefreshTimer();
    void onAircraftUpdated(const QString &id);
    void onOwnshipUpdated();
    
    // 专门用于接收 Python 遥控器 UDP 指令的槽函数
    void onCommandReceived();

private:
    AircraftManager *m_manager;
    QTimer *m_refreshTimer;
    
    // 界面控制相关变量
    QUdpSocket *m_cmdSocket;
    InterfaceMode m_currentMode;
};

#endif // TRAFFICDISPLAYWIDGET_H
