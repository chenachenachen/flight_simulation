#include "AircraftManager.h"
#include <QDebug>

AircraftManager::AircraftManager(QObject *parent)
    : QObject(parent) {
    m_ownship.isOwnship = true;
    m_ownship.callsign = "OWN001";
    qDebug() << "AircraftManager: Initialized";
}

AircraftData* AircraftManager::getOrCreateAircraft(const QString &callsign) {
    if (!m_aircrafts.contains(callsign)) {
        AircraftData newAircraft;
        newAircraft.callsign = callsign;
        newAircraft.isOwnship = false;
        m_aircrafts[callsign] = newAircraft;
        emit newAircraftAdded(callsign);
    }
    return &m_aircrafts[callsign];
}

void AircraftManager::updateAircraft(const QString &callsign, const AircraftData &data) {
    // 确保不是本机数据
    if (data.isOwnship) {
        qDebug() << "AircraftManager: Skipping ownship in updateAircraft:" << callsign;
        return;
    }
    
    bool isNew = !m_aircrafts.contains(callsign);
    m_aircrafts[callsign] = data;
    m_aircrafts[callsign].isOwnship = false; // 确保标记正确
    
    qDebug() << "AircraftManager: updateAircraft" << callsign 
             << (isNew ? "(NEW)" : "(UPDATE)")
             << "Total aircraft now:" << m_aircrafts.size();
    
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
    }
}

