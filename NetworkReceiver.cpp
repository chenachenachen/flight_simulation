#include "NetworkReceiver.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QDateTime>

NetworkReceiver::NetworkReceiver(AircraftManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager), m_socket(nullptr), m_serverPort(0) {
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &NetworkReceiver::onDataReceived);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &NetworkReceiver::onError);
}

bool NetworkReceiver::connectToServer(const QString &host, int port) {
    m_serverHost = host;
    m_serverPort = port;
    
    // 绑定本地端口接收数据（从Python桥接程序接收）
    // 注意：这是接收端口，Python桥接程序会发送数据到这里
    if (!m_socket->bind(QHostAddress::Any, port)) {
        emit errorOccurred(QString("Failed to bind to port %1").arg(port));
        return false;
    }
    
    emit connectionStatusChanged(true);
    return true;
}

void NetworkReceiver::disconnect() {
    if (m_socket) {
        m_socket->close();
    }
    emit connectionStatusChanged(false);
}

void NetworkReceiver::onDataReceived() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(datagram.data(), datagram.size());
        
        parseData(datagram);
    }
}

void NetworkReceiver::parseData(const QByteArray &data) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON parse error: %1").arg(error.errorString()));
        return;
    }
    
    QJsonObject root = doc.object();
    if (root["type"].toString() == "aircraft_data") {
        QJsonArray aircraftArray = root["data"].toArray();
        
        // Handle batched data
        int batchIndex = root["batch_index"].toInt(-1);
        int totalBatches = root["total_batches"].toInt(-1);
        
        // If this is the first batch, optionally clear existing data
        // (or you can accumulate all batches)
        if (batchIndex == 0) {
            // Optionally clear old data when receiving first batch
            // For now, we'll just update/merge
        }
        
        for (const QJsonValue &value : aircraftArray) {
            QJsonObject acObj = value.toObject();
            AircraftData acData;
            
            acData.callsign = acObj["callsign"].toString();
            acData.latitude = acObj["latitude"].toDouble();
            acData.longitude = acObj["longitude"].toDouble();
            acData.altitude = acObj["altitude"].toDouble();
            acData.heading = acObj["heading"].toDouble();
            acData.speed = acObj["speed"].toDouble();
            acData.isOwnship = acObj["isOwnship"].toBool();
            acData.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
            
            if (acData.isOwnship) {
                // 更新本机数据 - 保留所有字段
                AircraftData *ownship = m_manager->getOwnship();
                ownship->callsign = acData.callsign;
                ownship->latitude = acData.latitude;
                ownship->longitude = acData.longitude;
                ownship->altitude = acData.altitude;
                ownship->heading = acData.heading;
                ownship->speed = acData.speed;
                ownship->verticalSpeed = acData.verticalSpeed;
                ownship->isOwnship = true;  // 确保保持本机标记
                emit m_manager->ownshipUpdated();
            } else {
                // 更新其他飞机数据
                m_manager->updateAircraft(acData.callsign, acData);
            }
        }
        
        // If this is the last batch, you could emit a signal that all data is received
        if (totalBatches > 0 && batchIndex == totalBatches - 1) {
            // All batches received
        }
    }
}

void NetworkReceiver::sendOwnshipCommand(double heading, double speed, double altitude) {
    if (!m_socket || m_serverHost.isEmpty() || m_serverPort == 0) {
        return;
    }
    
    // 构造控制命令JSON
    // 发送到桥接程序的命令端口（端口+1）
    QJsonObject cmdObj;
    cmdObj["type"] = "control_command";
    cmdObj["heading"] = heading;
    cmdObj["speed"] = speed;
    cmdObj["altitude"] = altitude;
    
    QJsonDocument doc(cmdObj);
    QByteArray data = doc.toJson();
    
    // 发送到桥接程序的命令端口
    m_socket->writeDatagram(data, QHostAddress(m_serverHost), m_serverPort + 1);
}

void NetworkReceiver::onError(QAbstractSocket::SocketError error) {
    emit errorOccurred(QString("Socket error: %1").arg(error));
}

