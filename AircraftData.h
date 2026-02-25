#ifndef AIRCRAFTDATA_H
#define AIRCRAFTDATA_H

#include <QString>
#include <QObject>
#include <QtGlobal>   // 为 qint64 提供定义

// 飞机基本数据结构
struct AircraftData {
    QString callsign;        // 飞机识别号
    double latitude;         // 纬度
    double longitude;        // 经度
    double altitude;         // 高度 (英尺)
    double heading;          // 航向 (度数 0-360)
    double speed;            // 地速 (节)
    double verticalSpeed;    // 垂直速度
    bool isOwnship;          // 是否为本机
    
    // X-Plane姿态数据
    double pitch;            // 俯仰角 (度)
    double roll;             // 滚转角 (度)
    double yaw;              // 偏航角 (度，通常等于heading)

    // 机型信息（可选）
    QString model;           // 机型/型号字符串

    // 视角数据（用于屏幕投影）
    double viewHeading;      // 相机/视角航向
    double viewPitch;        // 相机/视角俯仰
    double viewRoll;         // 相机/视角滚转
    double viewFov;          // 相机水平视场角 (deg)
    bool hasView;            // 是否有有效视角数据
    
    // 用于显示的屏幕坐标 (由主窗口计算)
    double screenX;
    double screenY;
    
    // 最近一次从网络收到该飞机数据的时间戳 (毫秒)
    qint64 lastUpdateMs;
    
    AircraftData() 
        : latitude(0), longitude(0), altitude(0), 
          heading(0), speed(0), verticalSpeed(0), 
          isOwnship(false), pitch(0), roll(0), yaw(0),
          viewHeading(0), viewPitch(0), viewRoll(0), viewFov(0), hasView(false),
          model(""),
          screenX(0), screenY(0), lastUpdateMs(0) {}
};

#endif

