#ifndef AIRCRAFTMANAGER_H
#define AIRCRAFTMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QDateTime>

// =========================================================
// Aircraft Data Structure (关键数据结构)
// =========================================================
// 将结构体直接定义在此处，确保所有包含此头文件的模块都能看到最新的定义
struct AircraftData {
    QString callsign;
    double latitude;
    double longitude;
    double altitude;   // feet
    double heading;
    double speed;      // knots
    
    // 姿态数据
    double pitch = 0.0;
    double roll = 0.0;
    double yaw = 0.0;
    double verticalSpeed = 0.0;
    
    // --- 【核心修复】 必须包含 model 字段 ---
    // 这是实现 Target Categorization (方框/圆/倒三角) 的关键
    QString model;     // e.g. "B747", "R44", "MQ9"
    // -------------------------------------

    bool isOwnship = false;
    
    // 视图相关参数 (仅本机有效)
    bool hasView = false;
    double viewHeading = 0.0;
    double viewPitch = 0.0;
    double viewRoll = 0.0;
    double viewFov = 60.0;

    // 时间戳 (用于清除超时飞机)
    qint64 lastUpdateMs = 0;
};

// =========================================================
// ✈️ Aircraft Manager Class
// =========================================================
class AircraftManager : public QObject {
    Q_OBJECT
    
public:
    explicit AircraftManager(QObject *parent = nullptr);
    
    // 获取或创建飞机
    AircraftData* getOrCreateAircraft(const QString &callsign);
    
    // 获取所有飞机
    QMap<QString, AircraftData> getAllAircraft() const { return m_aircrafts; }
    
    // 获取本机
    AircraftData* getOwnship() { return &m_ownship; }
    
    // 更新飞机数据
    void updateAircraft(const QString &callsign, const AircraftData &data);
    
    // 更新本机控制信息
    void updateOwnshipControl(double heading, double speed, double altitude);
    
    // 更新本机位置数据（从X-Plane接收）
    void updateOwnshipPosition(double lat, double lon, double alt,
                                double heading, double speed,
                                double pitch = 0, double roll = 0, double vs = 0,
                                double viewHeading = 0, double viewPitch = 0,
                                double viewRoll = 0, double viewFov = 0,
                                bool hasView = false);
    
    // 清除飞机 (如果超时)
    void removeAircraft(const QString &callsign);
    
signals:
    void aircraftUpdated(const QString &callsign);
    void ownshipUpdated();
    void newAircraftAdded(const QString &callsign);
    
private:
    QMap<QString, AircraftData> m_aircrafts;
    AircraftData m_ownship;
};

#endif // AIRCRAFTMANAGER_H

