#include "XPlaneReceiver.h"
#include <QByteArray>
#include <QDebug>
#include <QHostAddress>
#include <cstring> 

XPlaneReceiver::XPlaneReceiver(AircraftManager *manager, QObject *parent)
    : QObject(parent),
      m_socket(new QUdpSocket(this)),
      m_pollTimer(new QTimer(this)),
      m_manager(manager),
      m_xpcPort(49009),
      m_xpcHost(QHostAddress::LocalHost),
      m_isConnected(false),
      m_missedPackets(0) { // 初始化丢包计数器
      
    connect(m_pollTimer, &QTimer::timeout, this, &XPlaneReceiver::pollOwnship);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &XPlaneReceiver::onError);
}

bool XPlaneReceiver::startListening(int port) {
    m_xpcPort = port;
    if (m_socket->state() == QAbstractSocket::BoundState) {
        m_socket->close();
    }
    // 绑定本地端口用于接收 XPlaneConnect 回包
    if (!m_socket->bind(QHostAddress::Any, 0, QAbstractSocket::ReuseAddressHint)) {
        emit connectionStatusChanged(false);
        return false;
    }
    // 根据 XPlaneConnect 网络协议，先发送 CONN 报文，明确告知插件回包端口
    sendConnHandshake();
    m_isConnected = true;
    m_missedPackets = 0;
    emit connectionStatusChanged(true);
    
    if (!m_pollTimer->isActive()) {
        m_pollTimer->start(20); // 50Hz 查询频率
    }
    return true;
}

void XPlaneReceiver::stopListening() {
    if (m_socket) m_socket->close();
    m_isConnected = false;
    emit connectionStatusChanged(false);
    m_pollTimer->stop();
}

void XPlaneReceiver::pollOwnship() {
    // 即使暂时标记为断开，也要持续尝试重连
    const QStringList datarefs = {
        "sim/flightmodel/position/latitude",
        "sim/flightmodel/position/longitude",
        "sim/flightmodel/position/elevation",
        "sim/flightmodel/position/psi",
        "sim/flightmodel/position/theta",
        "sim/flightmodel/position/phi",
        "sim/flightmodel/position/groundspeed",
        "sim/flightmodel/position/vh_ind",
        "sim/graphics/view/view_heading",
        "sim/graphics/view/view_pitch",
        "sim/graphics/view/view_roll",
        "sim/graphics/view/field_of_view_deg",
        "sim/graphics/view/pilots_head_psi",
        "sim/graphics/view/pilots_head_the",
        "sim/graphics/view/pilots_head_phi"
    };

    QVector<QVector<float>> values;
    // 增加超时时间到 150ms，防止偶尔的网络抖动导致断连
    if (!sendGetDrefs(datarefs, values, 150)) {
        m_missedPackets++;
        // 只有连续丢包超过 10 次 (约0.5秒) 才认为真正断开
        if (m_missedPackets > 10 && m_isConnected) {
            m_isConnected = false;
            emit connectionStatusChanged(false);
            qDebug() << "XPlaneReceiver: Connection lost (Too many timeouts)";
        }
        return;
    }

    // 成功接收数据，重置计数器
    if (!m_isConnected) {
        m_isConnected = true;
        emit connectionStatusChanged(true);
        qDebug() << "XPlaneReceiver: Connection restored";
    }
    m_missedPackets = 0;

    if (values.size() < 8) return;

    // 解析数据
    double lat = values[0].isEmpty() ? 0.0 : values[0].first();
    double lon = values[1].isEmpty() ? 0.0 : values[1].first();
    double alt_m = values[2].isEmpty() ? 0.0 : values[2].first();
    double psi = values[3].isEmpty() ? 0.0 : values[3].first();    // Heading
    double theta = values[4].isEmpty() ? 0.0 : values[4].first();  // Pitch
    double phi = values[5].isEmpty() ? 0.0 : values[5].first();    // Roll
    double spd_ms = values[6].isEmpty() ? 0.0 : values[6].first();
    double vs_ms = values[7].isEmpty() ? 0.0 : values[7].first();
    double viewHeading = values.size() > 8 && !values[8].isEmpty() ? values[8].first() : 0.0;
    double viewPitch = values.size() > 9 && !values[9].isEmpty() ? values[9].first() : 0.0;
    double viewRoll = values.size() > 10 && !values[10].isEmpty() ? values[10].first() : 0.0;
    double viewFov = values.size() > 11 && !values[11].isEmpty() ? values[11].first() : 0.0;
    double headPsi = values.size() > 12 && !values[12].isEmpty() ? values[12].first() : 0.0;
    double headThe = values.size() > 13 && !values[13].isEmpty() ? values[13].first() : 0.0;
    double headPhi = values.size() > 14 && !values[14].isEmpty() ? values[14].first() : 0.0;

    bool hasView = values.size() > 11 && viewFov > 1.0;
    bool hasHead = values.size() > 14;

    double camHeading = hasView ? viewHeading : headPsi;
    double camPitch = hasView ? viewPitch : headThe;
    double camRoll = hasView ? viewRoll : headPhi;
    double camFov = viewFov;
    bool hasCam = hasView || hasHead;

    m_manager->updateOwnshipPosition(
        lat, lon, alt_m * 3.28084, // m -> ft
        psi,
        spd_ms * 1.94384, // ms -> kts
        theta, phi,
        vs_ms * 196.85,   // ms -> ft/min
        camHeading, camPitch, camRoll, camFov,
        hasCam
    );
    emit ownshipDataReceived();
}

// ... (setDataRef, setDataRefValue, sendSetDref, buildDrefCommand 保持不变) ...

bool XPlaneReceiver::sendGetDrefs(const QStringList &datarefs, QVector<QVector<float>> &outValues, int timeoutMs) {
    QByteArray request = buildGetDCommand(datarefs);
    if (request.isEmpty()) return false;

    drainPendingDatagrams();
    m_socket->writeDatagram(request, m_xpcHost, static_cast<quint16>(m_xpcPort));

    // 使用 waitForReadyRead 进行同步等待
    if (!m_socket->waitForReadyRead(timeoutMs)) {
        return false;
    }

    while (m_socket->hasPendingDatagrams()) {
        QByteArray response;
        response.resize(m_socket->pendingDatagramSize());
        m_socket->readDatagram(response.data(), response.size());
        if (parseRespPacket(response, datarefs.size(), outValues)) {
            return true;
        }
    }
    return false;
}

// ... (buildGetDCommand, parseRespPacket, onError 保持不变) ...

// 下面这些函数需要保留在文件末尾
bool XPlaneReceiver::setDataRef(const QString &dataref, const QVector<float> &values) {
    if (dataref.isEmpty() || values.isEmpty()) return false;
    return sendSetDref(dataref, values);
}

bool XPlaneReceiver::setDataRefValue(const QString &dataref, float value) {
    return setDataRef(dataref, QVector<float>{value});
}

bool XPlaneReceiver::sendSetDref(const QString &dataref, const QVector<float> &values) {
    QByteArray request = buildDrefCommand(dataref, values);
    if (request.isEmpty()) return false;
    m_socket->writeDatagram(request, m_xpcHost, static_cast<quint16>(m_xpcPort));
    return true;
}

QByteArray XPlaneReceiver::buildGetDCommand(const QStringList &datarefs) const {
    QByteArray payload;
    payload.append("GETD", 4);
    payload.append(char(0x00));
    payload.append(static_cast<char>(datarefs.size()));
    for (const QString &dataref : datarefs) {
        QByteArray name = dataref.toUtf8();
        payload.append(static_cast<char>(name.size()));
        payload.append(name);
    }
    return payload;
}

QByteArray XPlaneReceiver::buildDrefCommand(const QString &dataref, const QVector<float> &values) const {
    QByteArray name = dataref.toUtf8();
    QByteArray payload;
    payload.append("DREF", 4);
    payload.append(char(0x00));
    payload.append(static_cast<char>(name.size()));
    payload.append(name);
    payload.append(static_cast<char>(values.size()));
    for (float value : values) {
        char bytes[4];
        std::memcpy(bytes, &value, 4);
        payload.append(bytes, 4);
    }
    return payload;
}

bool XPlaneReceiver::parseRespPacket(const QByteArray &packet, int expectedCount, QVector<QVector<float>> &outValues) const {
    Q_UNUSED(expectedCount);
    if (packet.size() < 6 || packet.left(4) != "RESP") return false;
    const quint8 count = static_cast<quint8>(packet[5]);
    if (count == 0) return false;
    int offset = 6;
    outValues.clear();
    for (int i = 0; i < count; ++i) {
        if (offset >= packet.size()) return false;
        quint8 valueCount = static_cast<quint8>(packet[offset++]);
        QVector<float> values;
        for (int v = 0; v < valueCount; ++v) {
            float value;
            if (offset + 4 > packet.size()) return false;
            std::memcpy(&value, packet.constData() + offset, 4);
            offset += 4;
            values.push_back(value);
        }
        outValues.push_back(values);
    }
    return true;
}

void XPlaneReceiver::drainPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        m_socket->readDatagram(nullptr, 0);
    }
}

void XPlaneReceiver::sendConnHandshake() {
    // XPlaneConnect CONN 报文:
    // 0-3: 'C','O','N','N'
    // 4:   padding byte (0)
    // 5-6: uint16 little-endian，本客户端希望接收回包的端口
    quint16 localPort = m_socket->localPort();
    if (localPort == 0) {
        return;
    }

    QByteArray payload;
    payload.reserve(7);
    payload.append("CONN", 4);
    payload.append(char(0x00));  // padding

    // 按 XPlaneConnect 规范使用 little-endian：低字节在前，高字节在后
    payload.append(char(localPort & 0xFF));
    payload.append(char((localPort >> 8) & 0xFF));

    m_socket->writeDatagram(payload, m_xpcHost, static_cast<quint16>(m_xpcPort));
}

void XPlaneReceiver::onError(QAbstractSocket::SocketError error) {
    // 仅记录日志，不立即断开，依靠超时逻辑处理断开
    qDebug() << "XPlaneReceiver Socket Error:" << error;
}
