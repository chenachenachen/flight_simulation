#ifndef BLUESKYCOMMUNICATOR_H
#define BLUESKYCOMMUNICATOR_H

#include <QObject>
#include <QUdpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "AircraftManager.h"

class BlueSkyCommunicator : public QObject {
    Q_OBJECT
public:
    explicit BlueSkyCommunicator(AircraftManager *manager, QObject *parent = nullptr);
    
    // 启动监听 (默认端口 49004)
    void startListening(quint16 port = 49004);
    
    // 发送本机位置给 Python Bridge (默认端口 49003)
    void sendOwnshipPosition(double lat, double lon, double alt_ft, double hdg, double speed_kts);

private slots:
    void processPendingDatagrams();

private:
    QUdpSocket *m_udpSocket;     // 接收交通数据 (Port 49004)
    QUdpSocket *m_sendSocket;    // 发送本机数据 (Port 49003)
    AircraftManager *m_manager;
};

#endif // BLUESKYCOMMUNICATOR_H

