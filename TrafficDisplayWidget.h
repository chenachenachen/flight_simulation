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
};

#endif

