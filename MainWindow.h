#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include "AircraftManager.h"
#include "NetworkReceiver.h"
#include "XPlaneReceiver.h"
#include "BlueSkyCommunicator.h"
#include "TrafficDisplayWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
private:
    AircraftManager *m_aircraftManager;
    NetworkReceiver *m_networkReceiver;      // 保留用于向后兼容（可选）
    XPlaneReceiver *m_xplaneReceiver;        // 接收X-Plane数据
    BlueSkyCommunicator *m_blueSkyComm;      // 与BlueSky双向通信
    TrafficDisplayWidget *m_displayWidget;
    QTimer *m_keepOnTopTimer;                // 定时器：保持窗口置顶
    QPushButton *m_quitButton;               // 退出按钮（左下角）
    
    void setupUI();
    void connectSignals();
    
    // 数据流处理
    void onXPlaneDataReceived();  // X-Plane数据到达时
    
    // 透明覆盖窗口设置
    void setupOverlayWindow();   // 配置窗口为透明覆盖模式
    void ensureOnTop();          // 确保窗口始终置顶
    
    // 退出控制
    void requestQuit();          // 请求退出程序（通过快捷键）
    
    // 事件过滤：完全忽略键盘事件，让它们穿透
    bool eventFilter(QObject *obj, QEvent *event) override;
    
protected:
    // 重写事件处理，确保窗口始终置顶
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;  // 允许正常关闭
    void changeEvent(QEvent *event) override;     // 处理窗口状态变化，防止激活
    // 重写键盘事件处理，完全忽略，让事件穿透
    bool event(QEvent *event) override;           // 重写event()方法，在事件分发前拦截键盘事件
    
private slots:
    void onKeepOnTop();          // 定时检查并保持置顶
};

#endif

