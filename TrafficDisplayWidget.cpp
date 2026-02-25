#include "TrafficDisplayWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPen>
#include <QPainterPath>
#include <QColor>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <QVector2D>
#include <QVector3D>

// =========================================================
// 📐 物理常量 & 预测性告警阈值 (CPA Predictive Thresholds)
// =========================================================
const double METERS_PER_NM = 1852.0;
const double FT_TO_METERS = 0.3048;
const double KNOTS_TO_MPS = 0.514444;

// 预测性警告阈值 (Warning - 红色)
const double CPA_WARN_TIME = 45.0; // 秒
const double CPA_WARN_DIST = 1.0 * METERS_PER_NM; 
const double CPA_WARN_ALT  = 600.0 * FT_TO_METERS;

// 预测性提示阈值 (Caution - 琥珀色)
const double CPA_CAUT_TIME = 60.0; // 秒
const double CPA_CAUT_DIST = 2.0 * METERS_PER_NM;
const double CPA_CAUT_ALT  = 800.0 * FT_TO_METERS;

// 绝对近距离盲区兜底 (Override)
const double OVERRIDE_WARN_DIST = 0.5 * METERS_PER_NM;
const double OVERRIDE_WARN_ALT  = 300.0 * FT_TO_METERS;

// =========================================================
// 🎨 视觉风格与颜色设定
// =========================================================
const QColor COL_WARN_CORE(255, 60, 60, 255);
const QColor COL_WARN_GLOW(255, 0, 0, 140);
const QColor COL_CAUT_CORE(255, 210, 0, 255);
const QColor COL_CAUT_GLOW(255, 170, 0, 140);
const QColor COL_ADVS_CORE(0, 255, 255, 255);
const QColor COL_ADVS_GLOW(0, 255, 255, 110);
const QColor COL_PRIORITY_CUE(255, 255, 255, 230); // 航权优先级提示线

QString getTechFont() { return "Arial"; }

// =========================================================
// 🔑 核心定义：目标机型分类 (Target Categorization)
// =========================================================
enum TargetCategory {
    CAT_FIXED_WING, 
    CAT_HEAVY,      
    CAT_ROTORCRAFT, 
    CAT_UAV         
};

TargetCategory getTargetCategory(const QString &model) {
    QString m = model.toUpper();
    if (m.startsWith("B74") || m.startsWith("B77") || m.contains("HEAVY")) return CAT_HEAVY;
    if (m.contains("HELI") || m.startsWith("R44") || m.startsWith("AS3")) return CAT_ROTORCRAFT;
    if (m.contains("UAV") || m.contains("MQ")) return CAT_UAV;
    return CAT_FIXED_WING;
}

struct RenderOrder {
    QString key;
    double distSq;
};

// 威胁数据结构，用于渲染视野外罗盘
struct ThreatData {
    QVector3D vec;
    int level;
    double dist;
    QString label;
};

// =========================================================
// 🖌️ 辅助绘制函数
// =========================================================
void drawGlowLine(QPainter &painter, const QPointF &p1, const QPointF &p2, const QColor &core, const QColor &glow, int width = 2) {
    painter.setPen(QPen(glow, width + 4)); painter.drawLine(p1, p2);
    painter.setPen(QPen(core, width));     painter.drawLine(p1, p2);
}

void drawGlowRect(QPainter &painter, const QRectF &rect, const QColor &core, const QColor &glow, int radius = 4, int width = 2) {
    painter.setPen(QPen(glow, width + 6)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(rect, radius, radius);
    painter.setPen(QPen(core, width));     painter.drawRoundedRect(rect, radius, radius);
}

void drawGlowEllipse(QPainter &painter, const QRectF &rect, const QColor &core, const QColor &glow, int width = 2) {
    painter.setPen(QPen(glow, width + 6)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(rect);
    painter.setPen(QPen(core, width));     painter.drawEllipse(rect);
}

// =========================================================
// 🔮 3D 隧道预测绘制 (3D Tunnel)
// =========================================================
void drawSolidTunnel(QPainter &painter, const QPointF &startP, const QPointF &endP,
                  double startW, double endW, const QColor &baseColor, int animOffset) {
    QVector2D vec(endP - startP);
    if (vec.length() < 2.0) return;
    QVector2D dir = vec.normalized();
    QVector2D perp(-dir.y(), dir.x());

    QPointF sL = startP - (perp.toPointF() * startW / 2.0);
    QPointF sR = startP + (perp.toPointF() * startW / 2.0);
    QPointF eL = endP - (perp.toPointF() * endW / 2.0);
    QPointF eR = endP + (perp.toPointF() * endW / 2.0);

    QColor fillColor = baseColor; fillColor.setAlpha(45);
    QColor edgeColor = baseColor; edgeColor.setAlpha(200);

    painter.setPen(QPen(edgeColor, 2)); painter.setBrush(fillColor);
    QPolygonF bandPoly; bandPoly << sL << sR << eR << eL;
    painter.drawPolygon(bandPoly);
    painter.drawLine(sL, eL); painter.drawLine(sR, eR);

    // 绘制流动肋条
    double ribSpacing = 40.0;
    double phase = (animOffset % 40);
    QColor ribColor = baseColor; ribColor.setAlpha(120);
    painter.setPen(QPen(ribColor, 1));

    for (double d = phase; d < vec.length(); d += ribSpacing) {
        double t = d / vec.length();
        QPointF cPos = startP + (dir.toPointF() * d);
        double currW = (startW * (1.0 - t) + endW * t);
        painter.drawLine(cPos - (perp.toPointF() * currW / 2.0), cPos + (perp.toPointF() * currW / 2.0));
    }

    // 绘制端点箭头
    double capW = endW * 0.7;
    QPointF tip = endP + (dir.toPointF() * capW * 0.35);
    QPointF cap1 = endP - (dir.toPointF() * 6) + (perp.toPointF() * capW / 2.0);
    QPointF cap2 = endP - (dir.toPointF() * 6) - (perp.toPointF() * capW / 2.0);
    painter.setPen(QPen(edgeColor, 2));
    painter.drawLine(cap1, tip);
    painter.drawLine(cap2, tip);
}

// =========================================================
// 🧭 3D 视野外罗盘 (Holographic Threat Sphere)
// =========================================================
void draw3DThreatSphere(QPainter &painter, int screenW, int screenH, QList<ThreatData> &threats) {
    if (threats.isEmpty()) return;

    std::sort(threats.begin(), threats.end(), [](const ThreatData &a, const ThreatData &b) {
        if (a.level != b.level) return a.level > b.level; 
        return a.dist < b.dist; 
    });
    while(threats.size() > 3) threats.removeLast(); 

    double radius = 70.0;
    double cx = screenW - radius - 50; 
    double cy = screenH - radius - 50;
    double camPitch = 60.0 * M_PI / 180.0; 

    auto project = [&](double x, double y, double z) -> QPointF {
        double py = -y * std::sin(camPitch) - z * std::cos(camPitch); 
        return QPointF(x + cx, py + cy);
    };

    painter.save();
    QPointF nose = project(0, 15, 0);
    QPointF lWing = project(-12, -10, 0);
    QPointF rWing = project(12, -10, 0);
    QPointF tail = project(0, -8, 0);
    
    QPainterPath jetPath;
    jetPath.moveTo(nose); jetPath.lineTo(rWing); jetPath.lineTo(tail); jetPath.lineTo(lWing); jetPath.closeSubpath();
    
    QColor gridColor(200, 255, 255, 30);
    painter.setPen(QPen(gridColor, 1));
    painter.setBrush(QColor(0, 20, 30, 100)); 
    painter.drawEllipse(QPointF(cx, cy), radius, radius * std::sin(camPitch)); 
    painter.setPen(QPen(Qt::cyan, 1));
    painter.setBrush(QColor(0, 255, 255, 80));
    painter.drawPath(jetPath);

    for (const auto &t : threats) {
        double vx = t.vec.x(); double vy = t.vec.y(); double vz = -t.vec.z(); 
        QVector3D dir(vx, vy, vz); dir.normalize(); dir *= (radius * 0.9); 
        QPointF startP = project(0, 0, 0); 
        QPointF endP = project(dir.x(), dir.y(), dir.z()); 
        QPointF dropP = project(dir.x(), dir.y(), 0); 

        QColor mainColor = (t.level == 3) ? COL_WARN_CORE : COL_CAUT_CORE;
        int lineWidth = (vy < 0) ? 3 : 2; 
        
        painter.setPen(QPen(mainColor, 1, Qt::DotLine)); painter.drawLine(endP, dropP);
        QColor dimColor = mainColor; dimColor.setAlpha(100);
        painter.setPen(QPen(dimColor, 1)); painter.drawLine(startP, dropP);
        painter.setPen(QPen(mainColor, lineWidth)); painter.drawLine(dropP, endP);
        painter.setBrush(mainColor); painter.setPen(Qt::NoPen);
        double bulbSize = (vy < 0) ? 5.0 : 3.5; 
        painter.drawEllipse(endP, bulbSize, bulbSize);
    }
    painter.setPen(QColor(200,200,200,180));
    painter.setFont(QFont(getTechFont(), 7, QFont::Bold));
    QPointF fwdText = project(0, radius + 10, 0);
    painter.drawText(QRectF(fwdText.x()-15, fwdText.y()-10, 30, 20), Qt::AlignCenter, "Fwd");
    painter.restore();
}

// =========================================================
// 🎛️ 主类实现 (TrafficDisplayWidget)
// =========================================================
const double FOV_SCALE = 1.0; 
const double PITCH_OFFSET = 0.0;
const double YAW_OFFSET = 0.0;
const double ROLL_OFFSET = 0.0;

TrafficDisplayWidget::TrafficDisplayWidget(AircraftManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager) {
    setMinimumSize(1024, 768);
    setFocusPolicy(Qt::NoFocus);
    
#ifdef Q_OS_WIN
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
#endif

#ifdef Q_OS_MAC
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#endif

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setStyleSheet("background: transparent;");
    
    // 3. 信号与槽连接
    connect(m_manager, &AircraftManager::ownshipUpdated, this, QOverload<>::of(&QWidget::update));
    connect(m_manager, QOverload<const QString &>::of(&AircraftManager::aircraftUpdated), this, QOverload<>::of(&QWidget::update));
    
    // 4. 定时器刷新
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    m_refreshTimer->start(20); 
}

void TrafficDisplayWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    static int frameCounter = 0; frameCounter++;
    bool flashOn = (frameCounter % 20) < 12;

    AircraftData *own = m_manager->getOwnship();
    if (!own) return;

    int w = width(); int h = height();
    int centerX = w / 2; int centerY = h / 2;

    // 1. 本机信息 HUD 面板
    {
        int startX = 20; int startY = 20; int boxW = 150; int rowH = 20; int headerH = 22;
        QString fName = getTechFont();
        QFont headerFont(fName, 12, QFont::Bold); QFont labelFont(fName, 9, QFont::Bold); QFont valFont(fName, 11, QFont::Bold); 
        QColor headerColor(255, 255, 255, 220); QColor labelColor(200, 200, 200, 180); QColor valColor(255, 255, 255, 255);

        painter.setPen(headerColor); painter.setFont(headerFont);
        painter.drawText(QRect(startX, startY, boxW, headerH), Qt::AlignLeft | Qt::AlignVCenter, "OWNSHIP");
        painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
        painter.drawLine(startX, startY + headerH + 2, startX + boxW, startY + headerH + 2);

        auto drawRow = [&](int row, const QString &l, const QString &v) {
            int y = startY + headerH + 6 + row * rowH;
            painter.setFont(labelFont); painter.setPen(labelColor); painter.drawText(QRect(startX, y, boxW/2, rowH), Qt::AlignLeft|Qt::AlignVCenter, l);
            painter.setFont(valFont); painter.setPen(valColor); painter.drawText(QRect(startX+boxW/2, y, boxW/2, rowH), Qt::AlignRight|Qt::AlignVCenter, v);
        };
        drawRow(0, "HDG", QString::number((int)own->heading));
        drawRow(1, "ALT", QString::number((int)own->altitude));
        drawRow(2, "SPD", QString::number((int)own->speed));
    }

    // 获取并排序目标机
    auto aircrafts = m_manager->getAllAircraft();
    QList<RenderOrder> renderList;
    for (auto it = aircrafts.begin(); it != aircrafts.end(); ++it) {
        if (it.value().isOwnship) continue;
        if (std::abs(it.value().latitude) < 0.001) continue;
        double dN = (it.value().latitude - own->latitude);
        double dE = (it.value().longitude - own->longitude);
        renderList.append({it.key(), dN*dN + dE*dE});
    }
    std::sort(renderList.begin(), renderList.end(), [](const RenderOrder &a, const RenderOrder &b) { return a.distSq > b.distSq; });

    // 视角投影数学计算
    double fovDeg = (own->hasView && own->viewFov > 1.0) ? own->viewFov : 60.0;
    double fovHoriz = fovDeg * FOV_SCALE * M_PI / 180.0;
    double focalLengthX = centerX / std::tan(fovHoriz / 2.0);
    double aspect = (w > 0) ? (static_cast<double>(w) / static_cast<double>(h)) : 1.0;
    double fovVert = 2.0 * std::atan(std::tan(fovHoriz / 2.0) / aspect);
    double focalLengthY = centerY / std::tan(fovVert / 2.0);
    
    double hdg = ((own->hasView ? own->viewHeading : own->heading) + YAW_OFFSET) * M_PI / 180.0;
    double pit = ((own->hasView ? own->viewPitch : own->pitch) + PITCH_OFFSET) * M_PI / 180.0;
    double rol = ((own->hasView ? own->viewRoll : own->roll) + ROLL_OFFSET) * M_PI / 180.0;
    double cPsi = std::cos(hdg), sPsi = std::sin(hdg);
    double cThe = std::cos(pit), sThe = std::sin(pit);
    double cPhi = std::cos(rol), sPhi = std::sin(rol);

    QList<ThreatData> offScreenThreats;

    for (const auto &item : renderList) {
        const AircraftData &ac = aircrafts[item.key];
        
        // 🧮 1. 绝对坐标与距离计算 (Relative Position)
        double dN = (ac.latitude - own->latitude) * 111111.0;
        double dE = (ac.longitude - own->longitude) * 111111.0 * std::cos(own->latitude * M_PI / 180.0);
        double dD = (own->altitude - ac.altitude) * FT_TO_METERS; 
        double slantRange = std::sqrt(dN*dN + dE*dE + dD*dD); 
        int dAlt100 = (int)((ac.altitude - own->altitude) / 100.0);
        
        if (slantRange < 10.0) continue;

        // 本机坐标系投影
        double x3 = cThe * cPsi * dN + cThe * sPsi * dE - sThe * dD; 
        double y3 = (sPhi * sThe * cPsi - cPhi * sPsi) * dN + (sPhi * sThe * sPsi + cPhi * cPsi) * dE + sPhi * cThe * dD; 
        double z3 = (cPhi * sThe * cPsi + sPhi * sPsi) * dN + (cPhi * sThe * sPsi - sPhi * cPsi) * dE + cPhi * cThe * dD; 

        // 🚨 2. CPA 告警预测计算 (Relative Velocity & Time to Conflict)
        double ownSpd_ms = own->speed * KNOTS_TO_MPS;
        double acSpd_ms  = ac.speed * KNOTS_TO_MPS;
        double ownHdg_rad = own->heading * M_PI / 180.0;
        double acHdg_rad  = ac.heading * M_PI / 180.0;

        // 计算双方绝对速度分量
        double v_own_N = ownSpd_ms * std::cos(ownHdg_rad);
        double v_own_E = ownSpd_ms * std::sin(ownHdg_rad);
        double v_ac_N  = acSpd_ms * std::cos(acHdg_rad);
        double v_ac_E  = acSpd_ms * std::sin(acHdg_rad);

        // 计算相对运动速度矢量 (\Delta V)
        double dv_N = v_ac_N - v_own_N;
        double dv_E = v_ac_E - v_own_E;
        double dv_sq = dv_N * dv_N + dv_E * dv_E;

        // 计算到达最近点时间 (t_CPA)
        double t_CPA = -1.0; 
        if (dv_sq > 0.001) { 
            t_CPA = -(dE * dv_E + dN * dv_N) / dv_sq; 
        }

        // 计算预计擦肩距离 (D_CPA)
        double d_CPA = slantRange; 
        if (t_CPA > 0) {
            double cpa_N = dN + dv_N * t_CPA;
            double cpa_E = dE + dv_E * t_CPA;
            d_CPA = std::sqrt(cpa_N * cpa_N + cpa_E * cpa_E);
        }

        // 判定告警等级 (Alert Level)
        double absVertSep = std::abs(dD);
        int alertLevel = 1; 

        if (slantRange <= OVERRIDE_WARN_DIST && absVertSep <= OVERRIDE_WARN_ALT) { alertLevel = 3; }
        else if (t_CPA > 0 && t_CPA <= CPA_WARN_TIME && d_CPA <= CPA_WARN_DIST && absVertSep <= CPA_WARN_ALT) { alertLevel = 3; }
        else if (t_CPA > 0 && t_CPA <= CPA_CAUT_TIME && d_CPA <= CPA_CAUT_DIST && absVertSep <= CPA_CAUT_ALT) { alertLevel = 2; }

        QColor coreColor, glowColor;
        switch(alertLevel) {
            case 3: coreColor = flashOn ? COL_WARN_CORE : QColor(180,0,0,220); glowColor = COL_WARN_GLOW; break;
            case 2: coreColor = COL_CAUT_CORE; glowColor = COL_CAUT_GLOW; break;
            default: coreColor = COL_ADVS_CORE; glowColor = COL_ADVS_GLOW; break;
        }

        // =========================================================
        // 🚥 3. 航权优先级判定 (Right-of-way Priority Cue)
        // =========================================================
        bool intruderHasPriority = false;
        TargetCategory cat = getTargetCategory(ac.model);

        double rel_bearing_rad = std::atan2(dE, dN) - ownHdg_rad;
        while (rel_bearing_rad > M_PI) rel_bearing_rad -= 2.0 * M_PI;
        while (rel_bearing_rad < -M_PI) rel_bearing_rad += 2.0 * M_PI;
        double rel_bearing_deg = rel_bearing_rad * 180.0 / M_PI; 
        
        double own_bearing_from_ac_rad = std::atan2(-dE, -dN) - acHdg_rad;
        while (own_bearing_from_ac_rad > M_PI) own_bearing_from_ac_rad -= 2.0 * M_PI;
        while (own_bearing_from_ac_rad < -M_PI) own_bearing_from_ac_rad += 2.0 * M_PI;
        double own_bearing_from_ac_deg = own_bearing_from_ac_rad * 180.0 / M_PI; 

        if (std::abs(rel_bearing_deg) <= 15.0) {
            // 对头相遇扇区
            intruderHasPriority = false;
        } else if (std::abs(own_bearing_from_ac_deg) > 110.0) { 
            // 尾随超越扇区 (被超越者优先)
            intruderHasPriority = true;
        } else if (rel_bearing_deg > 15.0 && rel_bearing_deg <= 110.0) {
            // 右侧切入扇区 (右侧优先)
            intruderHasPriority = true;
        } else {
            // 左侧切入扇区
            intruderHasPriority = false;
        }
        
        // 赋予高灵活性/特殊机型最高优先权
        if (cat == CAT_ROTORCRAFT) { intruderHasPriority = true; }

        // =========================================================
        // 📺 4. 屏幕渲染投影控制（取消边缘截断）
        // =========================================================
        bool isOnScreen = false;
        double screenX = -9999, screenY = -9999;
        if (x3 > 5.0) {
            screenX = centerX + (y3 / x3) * focalLengthX;
            screenY = centerY + (z3 / x3) * focalLengthY;

            // 修复：取消边缘截断。如果超出屏幕，直接判定为不在屏幕内，交由 3D 罗盘显示
            if (screenX >= 0 && screenX <= w && screenY >= 0 && screenY <= h) {
                isOnScreen = true;
            }
        }

        if (!isOnScreen) {
            if (alertLevel >= 2) offScreenThreats.append({ QVector3D(y3, x3, z3), alertLevel, slantRange, ac.model });
        } 
        else {
            // --- 核心视觉呈现：预测相对轨迹与航权提示 ---
            if (alertLevel >= 2) {
                 double dv_len = std::sqrt(dv_sq);
                 double dN_p = dN;
                 double dE_p = dE;
                 
                 if (dv_len > 0.1) {
                     // 🚨 修复 3D 隧道消失：防止投影点穿透摄像机导致 x3_p <= 0 而被裁剪
                     double scaleTime = (t_CPA > 0 && t_CPA < 60) ? t_CPA : 30.0; 
                     
                     // 保证预测终点（隧道开口）始终在挡风玻璃前方至少 300 米处
                     double maxTravel = slantRange - 300.0; 
                     if (maxTravel < 0) maxTravel = 0;
                     if (scaleTime * dv_len > maxTravel) {
                         scaleTime = maxTravel / dv_len;
                     }
                     
                     dN_p = dN + dv_N * scaleTime;
                     dE_p = dE + dv_E * scaleTime;
                 }
                 
                 double x3_p = cThe * cPsi * dN_p + cThe * sPsi * dE_p - sThe * dD;
                 double y3_p = (sPhi * sThe * cPsi - cPhi * sPsi) * dN_p + (sPhi * sThe * sPsi + cPhi * cPsi) * dE_p + sPhi * cThe * dD;
                 double z3_p = (cPhi * sThe * cPsi + sPhi * sPsi) * dN_p + (cPhi * sThe * sPsi - sPhi * cPsi) * dE_p + cPhi * cThe * dD;
                 
                 if (x3_p > 5.0) {
                     double screenX_p = centerX + (y3_p / x3_p) * focalLengthX;
                     double screenY_p = centerY + (z3_p / x3_p) * focalLengthY; 
                     
                     QPointF startPoint(screenX, screenY);
                     QPointF endPoint(screenX_p, screenY_p);

                     // 警告层级 -> 绘制 3D 红色隧道
                     if (alertLevel == 3) {
                         drawSolidTunnel(painter, startPoint, endPoint, 
                                         60.0 * focalLengthX / x3, 60.0 * focalLengthX / x3_p, coreColor, frameCounter * 3);
                     } 
                     // 提示层级 -> 绘制 单线预测箭头
                     else {
                         drawGlowLine(painter, startPoint, endPoint, coreColor, glowColor, 2);

                         QVector2D dir(endPoint - startPoint);
                         if (dir.length() > 1.0) {
                             dir.normalize();
                             QVector2D perp(-dir.y(), dir.x());
                             const double arrowLen = 16.0; const double arrowWidth = 7.0;
                             QPointF tip = endPoint; QPointF base = tip - dir.toPointF() * arrowLen;
                             QPointF left = base + perp.toPointF() * arrowWidth; QPointF right = base - perp.toPointF() * arrowWidth;
                             QPainterPath arrowPath; arrowPath.moveTo(tip); arrowPath.lineTo(left); arrowPath.lineTo(right); arrowPath.closeSubpath();
                             painter.setPen(Qt::NoPen); painter.setBrush(coreColor); painter.drawPath(arrowPath);
                         }
                     }
                     
                     // 附加层级 -> 绘制航权白色提示线
                     if (intruderHasPriority) {
                         QVector2D dir(endPoint - startPoint);
                         if (dir.length() > 5.0) {
                             dir.normalize();
                             QVector2D perp(-dir.y(), dir.x()); 
                             double offsetDist = 12.0; 
                             
                             QPointF pStart = startPoint + (perp.toPointF() * offsetDist);
                             QPointF pEnd = endPoint + (perp.toPointF() * offsetDist);
                             
                             painter.setPen(QPen(COL_PRIORITY_CUE, 3, Qt::SolidLine));
                             painter.drawLine(pStart, pEnd);
                         }
                     }
                 }
            }

            // --- 目标机型标识与文本数据 ---
            double visualSize = 30.0 / x3 * focalLengthX;
            int boxSize = std::max(24, std::min(200, (int)visualSize));
            QRectF boxRect(screenX - boxSize/2, screenY - boxSize/2, boxSize, boxSize);
            
            if (cat == CAT_ROTORCRAFT) {
                drawGlowEllipse(painter, boxRect, coreColor, glowColor, 2);
                painter.setBrush(coreColor); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(screenX, screenY), 3, 3);
            } else {
                drawGlowRect(painter, boxRect, coreColor, glowColor, 4, 2);
                if (cat == CAT_HEAVY) drawGlowRect(painter, boxRect.adjusted(6, 6, -6, -6), coreColor, glowColor, 2, 2);
                painter.setBrush(coreColor); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(screenX, screenY), 2, 2);
            }

            painter.setFont(QFont(getTechFont(), 11, QFont::Bold)); painter.setPen(coreColor);
            QString topText = ac.model.isEmpty() ? "TARGET" : ac.model;
            painter.drawText(QRectF(screenX-80, screenY-boxSize/2-25, 160, 25), Qt::AlignCenter|Qt::AlignBottom, topText);
            
            QString distText = QString("%1 NM").arg(slantRange/METERS_PER_NM, 0, 'f', 1);
            QString altSign = (dAlt100 >= 0) ? "▲" : "▼";
            QString altText = QString("%1 %2").arg(altSign).arg(std::abs(dAlt100));
            
            painter.drawText(QRectF(screenX-80, screenY+boxSize/2+4, 160, 18), Qt::AlignCenter, distText);
            painter.setFont(QFont(getTechFont(), 10, QFont::Bold));
            painter.drawText(QRectF(screenX-80, screenY+boxSize/2+22, 160, 18), Qt::AlignCenter, altText);
        }
    }

    // 绘制脱离屏幕外的目标的 3D 雷达球
    draw3DThreatSphere(painter, w, h, offScreenThreats);
}

void TrafficDisplayWidget::onRefreshTimer() { update(); }
void TrafficDisplayWidget::onAircraftUpdated(const QString &) { update(); }
void TrafficDisplayWidget::onOwnshipUpdated() { update(); }
void TrafficDisplayWidget::keyPressEvent(QKeyEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mousePressEvent(QMouseEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mouseMoveEvent(QMouseEvent *e) { e->ignore(); }
