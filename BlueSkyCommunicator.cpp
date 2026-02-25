#include "BlueSkyCommunicator.h"
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>

BlueSkyCommunicator::BlueSkyCommunicator(AircraftManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    m_udpSocket = new QUdpSocket(this);
    m_sendSocket = new QUdpSocket(this);
    
    // 连接信号槽：一有数据包进来，立刻处理
    connect(m_udpSocket, &QUdpSocket::readyRead, 
            this, &BlueSkyCommunicator::processPendingDatagrams);
}

void BlueSkyCommunicator::startListening(quint16 port) {
    // 【关键修复】必须绑定成功，否则收不到任何数据
    // QHostAddress::Any 表示监听所有网卡
    if (m_udpSocket->bind(QHostAddress::Any, port)) {
        qDebug() << "BlueSkyCommunicator: SUCCESS - Listening for traffic on port" << port;
    } else {
        qDebug() << "BlueSkyCommunicator: ERROR - Failed to bind port" << port << "(Is it already used?)";
    }
}

void BlueSkyCommunicator::sendOwnshipPosition(double lat, double lon, double alt_ft, double hdg, double speed_kts) {
    // 构造发给 Python Bridge 的 JSON
    QJsonObject json;
    json["type"] = "ownship_position";
    json["latitude"] = lat;
    json["longitude"] = lon;
    json["altitude"] = alt_ft;
    json["heading"] = hdg;
    json["speed"] = speed_kts;
    
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    
    // 发送到 49003 (Bridge 的命令接收端口)
    m_sendSocket->writeDatagram(data, QHostAddress::LocalHost, 49003); 
}

void BlueSkyCommunicator::processPendingDatagrams() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        QByteArray data = datagram.data();
        
        // --- [调试] 打印收到的原始数据 ---
        qDebug() << "BlueSkyCommunicator: UDP RECV (" << data.size() << " bytes)";
        qDebug() << "BlueSkyCommunicator: Raw data:" << data.left(200);
        
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "BlueSkyCommunicator: JSON Parse Error:" << parseError.errorString();
            qDebug() << "BlueSkyCommunicator: Error at position:" << parseError.offset;
            continue;
        }
        
        if (!doc.isObject()) {
            qDebug() << "BlueSkyCommunicator: JSON is not an object!";
            continue;
        }
        
        QJsonObject root = doc.object();
        QString type = root["type"].toString();
        qDebug() << "BlueSkyCommunicator: Message type:" << type;
        
        if (type == "aircraft_data" || type == "traffic_data") {
            QJsonArray list = root["data"].toArray();
            qDebug() << "BlueSkyCommunicator: Processing " << list.size() << " aircraft...";
            
            int trafficCount = 0;
            int ownshipCount = 0;

            for (const QJsonValue &val : list) {
                if (!val.isObject()) {
                    qDebug() << "BlueSkyCommunicator: Skipping non-object value in array";
                    continue;
                }
                
                QJsonObject acObj = val.toObject();
                AircraftData acData;
                
                acData.callsign = acObj["callsign"].toString();
                acData.latitude = acObj["latitude"].toDouble();
                acData.longitude = acObj["longitude"].toDouble();
                acData.altitude = acObj["altitude"].toDouble();
                acData.heading = acObj["heading"].toDouble();
                acData.speed = acObj["speed"].toDouble();
                acData.isOwnship = acObj["isOwnship"].toBool();
                acData.model = acObj["model"].toString();
                
                // 初始化其他字段
                acData.verticalSpeed = 0.0;
                acData.pitch = 0.0;
                acData.roll = 0.0;
                acData.yaw = acData.heading;
                acData.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
                
                qDebug() << "BlueSkyCommunicator: Parsed aircraft" << acData.callsign 
                         << "isOwnship:" << acData.isOwnship
                         << "lat:" << acData.latitude << "lon:" << acData.longitude
                         << "RECV:" << acData.callsign << "MODEL:" << acData.model;
                
                if (acData.isOwnship) {
                    // 本机数据由 XPlaneReceiver 提供，桥接只负责 traffic
                    qDebug() << "BlueSkyCommunicator: Ignored ownship from bridge" << acData.callsign;
                } else {
                    // 更新其他飞机数据
                    m_manager->updateAircraft(acData.callsign, acData);
                    trafficCount++;
                    qDebug() << "BlueSkyCommunicator: Updated traffic aircraft" << acData.callsign 
                             << "at" << acData.latitude << acData.longitude;
                }
            }
            
            qDebug() << "BlueSkyCommunicator: Summary - Ownship:" << ownshipCount 
                     << "Traffic:" << trafficCount;
        } else {
            qDebug() << "BlueSkyCommunicator: Unknown message type:" << type;
            qDebug() << "BlueSkyCommunicator: Available keys:" << root.keys();
        }
    }
}