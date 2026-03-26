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
    NetworkReceiver *m_networkReceiver;      
    XPlaneReceiver *m_xplaneReceiver;        
    BlueSkyCommunicator *m_blueSkyComm;      
    TrafficDisplayWidget *m_displayWidget;
    QTimer *m_keepOnTopTimer;                
    QPushButton *m_quitButton;               
    
    void setupUI();
    void setupOverlayWindow();   // 配置窗口为透明覆盖模式
    
private slots:
    void ensureOnTop();          // 定时检查并保持置顶
    void onXPlaneDataReceived(); // X-Plane数据到达时
    
protected:
    // 重写事件处理，确保窗口始终置顶
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;  
};

#endif

