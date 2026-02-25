#ifndef TRAFFICDISPLAYWIDGET_H
#define TRAFFICDISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>
#include "AircraftManager.h"

class TrafficDisplayWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit TrafficDisplayWidget(AircraftManager *manager, QWidget *parent = nullptr);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    
private slots:
    void onAircraftUpdated(const QString &callsign);
    void onOwnshipUpdated();
    void onRefreshTimer();
    
private:
    AircraftManager *m_manager;
    QTimer *m_refreshTimer;
    
    // 绘制函数
    void drawBackground(QPainter &painter);
    void drawAircrafts(QPainter &painter);  // 绘制BlueSky交通飞机（红色方框）
    // drawOwnship 已移除 - 本机由X-Plane界面显示
    void drawInfo(QPainter &painter);
    
    // 坐标转换
    QPointF worldToScreen(double lat, double lon);
    
    // 缩放因子
    double m_scale;
};

#endif

