#ifndef XPLANERECEIVER_H
#define XPLANERECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include "AircraftManager.h"

/**
 * XPlaneReceiver - 基于 XPlaneConnect (XPC) 插件的 DataRef 读写
 *
 * 通过 XPC UDP 协议主动查询 / 写入 datarefs：
 * - GETD: 读取 datarefs
 * - DREF: 写入 datarefs
 *
 * 这样可以覆盖 X-Plane 官方范围内所有可写 dataref，
 * 避免被动接收 DATA 协议数据包。
 */
class XPlaneReceiver : public QObject {
    Q_OBJECT
    
public:
    explicit XPlaneReceiver(AircraftManager *manager, QObject *parent = nullptr);
    
    // 连接 XPlaneConnect 插件（默认端口 49009）
    bool startListening(int port = 49009);
    
    // 停止监听
    void stopListening();
    
    // 获取连接状态
    bool isConnected() const { return m_isConnected; }

    // 通过 XPlaneConnect 写 dataref（支持标量与数组）
    bool setDataRef(const QString &dataref, const QVector<float> &values);
    bool setDataRefValue(const QString &dataref, float value);
    
signals:
    void connectionStatusChanged(bool connected);
    void ownshipDataReceived();  // 当接收到本机数据时发出
    void errorOccurred(const QString &error);
    
private slots:
    void onError(QAbstractSocket::SocketError error);
    void pollOwnship();
    
private:
    QUdpSocket *m_socket;
    QTimer *m_pollTimer;
    AircraftManager *m_manager;
    int m_xpcPort;
    QHostAddress m_xpcHost;
    bool m_isConnected;
    int m_missedPackets;
    

    // 发送/接收 XPlaneConnect 报文
    bool sendGetDrefs(const QStringList &datarefs, QVector<QVector<float>> &outValues, int timeoutMs = 50);
    bool sendSetDref(const QString &dataref, const QVector<float> &values);
    QByteArray buildGetDCommand(const QStringList &datarefs) const;
    QByteArray buildDrefCommand(const QString &dataref, const QVector<float> &values) const;
    bool parseRespPacket(const QByteArray &packet, int expectedCount, QVector<QVector<float>> &outValues) const;
    void drainPendingDatagrams();
    void sendConnHandshake();
    
    // 临时存储当前数据包的数据
    struct CurrentPacket {
        double latitude;
        double longitude;
        double altitude_m;
        double heading;
        double pitch;
        double roll;
        double speed_ms;
        double verticalSpeed_ms;
        bool hasLat, hasLon, hasAlt, hasHdg, hasPitch, hasRoll, hasSpeed, hasVS;
        
        CurrentPacket() : latitude(0), longitude(0), altitude_m(0), heading(0),
                         pitch(0), roll(0), speed_ms(0), verticalSpeed_ms(0),
                         hasLat(false), hasLon(false), hasAlt(false), hasHdg(false),
                         hasPitch(false), hasRoll(false), hasSpeed(false), hasVS(false) {}
    } m_currentPacket;
};

#endif // XPLANERECEIVER_H

