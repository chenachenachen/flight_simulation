#include "AircraftManager.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>

AircraftManager::AircraftManager(QObject *parent)
    : QObject(parent) {
    m_ownship.isOwnship = true;
    m_ownship.callsign = "OWN001";
    qDebug() << "AircraftManager: Initialized";

    // 🌟🌟🌟 核心修复：自动垃圾回收器 (Garbage Collection) 🌟🌟🌟
    // 每 500 毫秒巡检一次内存中的飞机列表
    QTimer *gcTimer = new QTimer(this);
    connect(gcTimer, &QTimer::timeout, this, [this]() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QStringList staleKeys;
        
        // 找出所有超时未更新的飞机（判定超时阈值为 1000 毫秒）
        for (auto it = m_aircrafts.begin(); it != m_aircrafts.end(); ++it) {
            if (!it.value().isOwnship && (now - it.value().lastUpdateMs > 1000)) {
                staleKeys.append(it.key());
            }
        }
        
        // 如果发现了超时幽灵飞机，彻底清除它们
        if (!staleKeys.isEmpty()) {
            for (const QString &key : staleKeys) {
                qDebug() << "AircraftManager: GC removed stale ghost aircraft ->" << key;
                m_aircrafts.remove(key);
            }
            // 触发全局重绘，瞬间清空屏幕上的残留红框/黄线
            emit aircraftUpdated(""); 
        }
    });
    gcTimer->start(500);
}

AircraftData* AircraftManager::getOrCreateAircraft(const QString &callsign) {
    if (!m_aircrafts.contains(callsign)) {
        AircraftData newAircraft;
        newAircraft.callsign = callsign;
        newAircraft.isOwnship = false;
        // 创建时赋予初始时间戳
        newAircraft.lastUpdateMs = QDateTime::currentMSecsSinceEpoch(); 
        m_aircrafts[callsign] = newAircraft;
        emit newAircraftAdded(callsign);
    }
    return &m_aircrafts[callsign];
}

void AircraftManager::updateAircraft(const QString &callsign, const AircraftData &data) {
    // 确保不是本机数据
    if (data.isOwnship) {
        return;
    }
    
    bool isNew = !m_aircrafts.contains(callsign);
    m_aircrafts[callsign] = data;
    m_aircrafts[callsign].isOwnship = false; // 确保标记正确
    
    // 🌟🌟🌟 关键步骤：每次收到该飞机的更新，刷新它的“存活时间戳”
    m_aircrafts[callsign].lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
    
    if (isNew) {
        emit newAircraftAdded(callsign);
    }
    emit aircraftUpdated(callsign);
}

void AircraftManager::updateOwnshipControl(double heading, double speed, double altitude) {
    m_ownship.heading = heading;
    m_ownship.speed = speed;
    m_ownship.altitude = altitude;
    emit ownshipUpdated();
}

void AircraftManager::updateOwnshipPosition(double lat, double lon, double alt,
                                            double heading, double speed,
                                            double pitch, double roll, double vs,
                                            double viewHeading, double viewPitch,
                                            double viewRoll, double viewFov,
                                            bool hasView) {
    m_ownship.latitude = lat;
    m_ownship.longitude = lon;
    m_ownship.altitude = alt;
    m_ownship.heading = heading;
    m_ownship.speed = speed;
    m_ownship.pitch = pitch;
    m_ownship.roll = roll;
    m_ownship.verticalSpeed = vs;
    m_ownship.yaw = heading;  // yaw通常等于heading
    if (hasView) {
        m_ownship.viewHeading = viewHeading;
        m_ownship.viewPitch = viewPitch;
        m_ownship.viewRoll = viewRoll;
        m_ownship.viewFov = viewFov;
        m_ownship.hasView = true;
    } else {
        m_ownship.hasView = false;
    }
    m_ownship.isOwnship = true;
    emit ownshipUpdated();
}

void AircraftManager::removeAircraft(const QString &callsign) {
    if (m_aircrafts.contains(callsign)) {
        m_aircrafts.remove(callsign);
        qDebug() << "AircraftManager: Explicitly removed aircraft" << callsign;
    }
}

// 兼容之前加的函数，防止报错，但核心清理工作已经由定时器(GC)接管了
void AircraftManager::syncActiveAircrafts(const QStringList &activeCallsigns) {
    Q_UNUSED(activeCallsigns);
}

