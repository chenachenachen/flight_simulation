#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QPaintEvent>
#include <QPainter>
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
    QRect m_targetWallGeometry;              
    
    void setupUI();
    void connectSignals();
    
    // 数据流处理
    void onXPlaneDataReceived();  
    
    // 透明覆盖窗口设置
    void setupOverlayWindow();   
    void ensureOnTop();          
    void applyWallGeometry();    
    
    void requestQuit();          
    
    bool eventFilter(QObject *obj, QEvent *event) override;
    
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;  
    void changeEvent(QEvent *event) override;     
    bool event(QEvent *event) override;           
    
    // 🎯 核心测试：重写绘图事件，用于画红色大十字
    void paintEvent(QPaintEvent *event) override;
    
private slots:
    void onKeepOnTop();          
};

#endif
