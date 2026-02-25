#ifndef NETWORKRECEIVER_H
#define NETWORKRECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include "AircraftManager.h"

class NetworkReceiver : public QObject {
    Q_OBJECT
    
public:
    explicit NetworkReceiver(AircraftManager *manager, QObject *parent = nullptr);
    
    // 连接到BlueSky服务器
    bool connectToServer(const QString &host, int port);
    
    // 断开连接
    void disconnect();
    
    // 发送本机操作命令
    void sendOwnshipCommand(double heading, double speed, double altitude);
    
signals:
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString &error);
    
private slots:
    void onDataReceived();
    void onError(QAbstractSocket::SocketError error);
    
private:
    QUdpSocket *m_socket;
    AircraftManager *m_manager;
    QString m_serverHost;
    int m_serverPort;
    
    // 解析接收到的数据
    void parseData(const QByteArray &data);
};

#endif

