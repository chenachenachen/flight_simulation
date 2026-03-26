
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
// CPA Predictive Thresholds
// =========================================================
const double METERS_PER_NM = 1852.0;
const double FT_TO_METERS = 0.3048;
const double KNOTS_TO_MPS = 0.514444;

const double CPA_WARN_TIME = 45.0; 
const double CPA_WARN_DIST = 1.0 * METERS_PER_NM; 
const double CPA_WARN_ALT  = 600.0 * FT_TO_METERS;

const double CPA_CAUT_TIME = 60.0; 
const double CPA_CAUT_DIST = 2.0 * METERS_PER_NM;
const double CPA_CAUT_ALT  = 800.0 * FT_TO_METERS;

const double OVERRIDE_WARN_DIST = 0.5 * METERS_PER_NM;
const double OVERRIDE_WARN_ALT  = 300.0 * FT_TO_METERS;

// =========================================================
// Color Settings & Helper Functions
// =========================================================
const QColor COL_WARN_CORE(255, 60, 60, 255);
const QColor COL_WARN_GLOW(255, 0, 0, 140);
const QColor COL_CAUT_CORE(255, 210, 0, 255);
const QColor COL_CAUT_GLOW(255, 170, 0, 140);
const QColor COL_ADVS_CORE(0, 255, 255, 255);
const QColor COL_ADVS_GLOW(0, 255, 255, 110);

QString getTechFont() { return "Arial"; }

enum TargetCategory { CAT_FIXED_WING, CAT_HEAVY, CAT_ROTORCRAFT, CAT_UAV };

TargetCategory getTargetCategory(const QString &model) {
    QString m = model.toUpper();
    if (m.startsWith("B74") || m.startsWith("B77") || m.contains("HEAVY")) return CAT_HEAVY;
    if (m.contains("HELI") || m.startsWith("R44") || m.startsWith("AS3")) return CAT_ROTORCRAFT;
    if (m.contains("UAV") || m.contains("MQ")) return CAT_UAV;
    return CAT_FIXED_WING;
}

struct RenderOrder { QString key; double distSq; };
struct ThreatData { QVector3D vec; int level; double dist; QString label; };

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

void drawSolidTunnel(QPainter &painter, const QPointF &startP, const QPointF &endP, double startW, double endW, const QColor &baseColor, int animOffset) {
    QVector2D vec(endP - startP);
    if (vec.length() < 2.0) return;
    QVector2D dir = vec.normalized();
    QVector2D perp(-dir.y(), dir.x());
    double totalLen = vec.length();
    double chevronSpacing = 45.0; 
    double phase = (animOffset % (int)chevronSpacing);

    painter.setBrush(Qt::NoBrush); 
    for (double d = phase; d < totalLen; d += chevronSpacing) {
        double t = d / totalLen; 
        QPointF cPos = startP + (dir.toPointF() * d);
        double currW = startW * (1.0 - t) + endW * t;

        QPointF frontPt = cPos + (dir.toPointF() * (currW / 2.5)); 
        QPointF leftPt  = cPos - (perp.toPointF() * currW / 2.0);  
        QPointF rightPt = cPos + (perp.toPointF() * currW / 2.0);  

        QPainterPath path;
        path.moveTo(leftPt); path.lineTo(frontPt); path.lineTo(rightPt);

        int globalAlpha = std::max(0, (int)(255 * (1.0 - t))); 
        QColor glowColor(255, 40, 40); glowColor.setAlpha(globalAlpha * 0.5); 
        QColor coreColor(255, 180, 180); coreColor.setAlpha(globalAlpha * 0.9);

        QPen glowPen(glowColor, 12); glowPen.setJoinStyle(Qt::RoundJoin); glowPen.setCapStyle(Qt::RoundCap);
        painter.setPen(glowPen); painter.drawPath(path);

        QPen corePen(coreColor, 2); corePen.setJoinStyle(Qt::RoundJoin); corePen.setCapStyle(Qt::RoundCap);
        painter.setPen(corePen); painter.drawPath(path);
    }
}

// =========================================================
// 核心：三联屏渲染器配置 (提取自您 X-Plane 设置的数据)
// =========================================================
struct MonitorConfig {
    double yawOffset;    // 屏幕偏航角
    double pitchOffset;  // 屏幕俯仰角
    double fov;          // 屏幕水平FOV
};

// 按照物理顺序：Monitor 1 (左), Monitor 2 (中), Monitor 3 (右)
const MonitorConfig MONITORS[3] = {
    {-66.67, 6.79, 97.11}, // 左屏
    {-0.63,  6.98, 97.11}, // 中屏
    {66.38,  7.91, 97.11}  // 右屏
};

TrafficDisplayWidget::TrafficDisplayWidget(AircraftManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager) {
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    
    connect(m_manager, &AircraftManager::ownshipUpdated, this, QOverload<>::of(&QWidget::update));
    connect(m_manager, QOverload<const QString &>::of(&AircraftManager::aircraftUpdated), this, QOverload<>::of(&QWidget::update));
    
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    m_refreshTimer->start(20); 
}

void TrafficDisplayWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width(); 
    int h = height();
    
    // 计算每块屏幕的物理宽度 (假设三块屏幕等宽拼接)
    int monWidth = w / 3; 

    // =========================================================
    // 🎯 测试与标定：为三块屏幕分别绘制独立准星
    // =========================================================
    for (int i = 0; i < 3; i++) {
        int cx = (i * monWidth) + (monWidth / 2);
        int cy = h / 2;

        QPen redPen(Qt::red, 3);
        painter.setPen(redPen);
        painter.drawLine(cx - 100, cy, cx + 100, cy); 
        painter.drawLine(cx, cy - 100, cx, cy + 100); 
        
        painter.setPen(Qt::yellow);
        painter.setFont(QFont("Arial", 16, QFont::Bold));
        painter.drawText(cx + 10, cy + 30, QString("Mon %1 | Yaw: %2").arg(i+1).arg(MONITORS[i].yawOffset));
    }

    AircraftData *own = m_manager->getOwnship();
    if (!own) return;

    static int frameCounter = 0; frameCounter++;
    bool flashOn = (frameCounter % 20) < 12;

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

    // =========================================================
    // 👁️ 三视锥多通道渲染引擎 (Multi-Frustum Rendering)
    // =========================================================
    for (int m = 0; m < 3; m++) {
        // 【关键黑科技】：设置裁剪区域。确保左屏的计算结果绝不会越界画到中屏去，实现物理级无缝拼接！
        painter.setClipRect(m * monWidth, 0, monWidth, h);

        double currentYawOffset = MONITORS[m].yawOffset;
        double currentPitchOffset = MONITORS[m].pitchOffset;
        double currentFov = MONITORS[m].fov;

        double fovHoriz = currentFov * M_PI / 180.0;
        double focalLengthX = (monWidth / 2.0) / std::tan(fovHoriz / 2.0);
        double aspect = (double)monWidth / (double)h;
        double fovVert = 2.0 * std::atan(std::tan(fovHoriz / 2.0) / aspect);
        double focalLengthY = (h / 2.0) / std::tan(fovVert / 2.0);
        
        // 这一块屏幕专用的摄像机姿态矩阵
        double hdg = ((own->hasView ? own->viewHeading : own->heading) + currentYawOffset) * M_PI / 180.0;
        double pit = ((own->hasView ? own->viewPitch : own->pitch) + currentPitchOffset) * M_PI / 180.0;
        double rol = ((own->hasView ? own->viewRoll : own->roll) + 0.0) * M_PI / 180.0;
        
        double cPsi = std::cos(hdg), sPsi = std::sin(hdg);
        double cThe = std::cos(pit), sThe = std::sin(pit);
        double cPhi = std::cos(rol), sPhi = std::sin(rol);

        for (const auto &item : renderList) {
            const AircraftData &ac = aircrafts[item.key];
            
            double dN = (ac.latitude - own->latitude) * 111120.0;
            double dE = (ac.longitude - own->longitude) * 111120.0 * std::cos(own->latitude * M_PI / 180.0);
            double dD = (own->altitude - ac.altitude) * FT_TO_METERS; 
            double slantRange = std::sqrt(dN*dN + dE*dE + dD*dD); 
            int dAlt100 = (int)((ac.altitude - own->altitude) / 100.0);
            
            if (slantRange < 10.0) continue;

            // 转换到当前屏幕摄像机坐标系下
            double x3 = cThe * cPsi * dN + cThe * sPsi * dE - sThe * dD; 
            double y3 = (sPhi * sThe * cPsi - cPhi * sPsi) * dN + (sPhi * sThe * sPsi + cPhi * cPsi) * dE + sPhi * cThe * dD; 
            double z3 = (cPhi * sThe * cPsi + sPhi * sPsi) * dN + (cPhi * sThe * sPsi - sPhi * cPsi) * dE + cPhi * cThe * dD; 

            // 计算相对速度与 CPA
            double ownSpd_ms = own->speed * KNOTS_TO_MPS;
            double acSpd_ms  = ac.speed * KNOTS_TO_MPS;
            double v_own_N = ownSpd_ms * std::cos(own->heading * M_PI / 180.0);
            double v_own_E = ownSpd_ms * std::sin(own->heading * M_PI / 180.0);
            double v_ac_N  = acSpd_ms * std::cos(ac.heading * M_PI / 180.0);
            double v_ac_E  = acSpd_ms * std::sin(ac.heading * M_PI / 180.0);

            double dv_N = v_ac_N - v_own_N;
            double dv_E = v_ac_E - v_own_E;
            double dv_sq = dv_N * dv_N + dv_E * dv_E;

            double t_CPA = -1.0; 
            if (dv_sq > 0.001) t_CPA = -(dE * dv_E + dN * dv_N) / dv_sq; 

            double d_CPA = slantRange; 
            if (t_CPA > 0) {
                d_CPA = std::sqrt(std::pow(dN + dv_N * t_CPA, 2) + std::pow(dE + dv_E * t_CPA, 2));
            }

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

            TargetCategory cat = getTargetCategory(ac.model);
            
            // 如果目标在当前摄像机前方
            if (x3 > 5.0) {
                // 计算在【当前单块屏幕】里的局部坐标 (Local Coordinates)
                double localScreenX = (monWidth / 2.0) + (y3 / x3) * focalLengthX;
                double localScreenY = (h / 2.0) + (z3 / x3) * focalLengthY; 
                
                // 转换到【横跨三屏的大画布】上的全局坐标 (Global Coordinates)
                double globalScreenX = (m * monWidth) + localScreenX;
                double globalScreenY = localScreenY;

                // 为了防止性能浪费，只绘制靠近当前屏幕的目标（稍微给一点冗余余量，防止被错误裁剪）
                if (localScreenX >= -200 && localScreenX <= monWidth + 200) {
                    
                    // 1. 绘制预测轨迹 (Tunnel)
                    if (alertLevel >= 2 && ac.speed > 60.0 && t_CPA > 0) {
                        double scaleTime = (t_CPA > 0 && t_CPA < 12.0) ? t_CPA : 12.0; 
                        
                        double dN_p = dN + dv_N * scaleTime;
                        double dE_p = dE + dv_E * scaleTime;
                        
                        double x3_p = cThe * cPsi * dN_p + cThe * sPsi * dE_p - sThe * dD;
                        double y3_p = (sPhi * sThe * cPsi - cPhi * sPsi) * dN_p + (sPhi * sThe * sPsi + cPhi * cPsi) * dE_p + sPhi * cThe * dD;
                        double z3_p = (cPhi * sThe * cPsi + sPhi * sPsi) * dN_p + (cPhi * sThe * sPsi - sPhi * cPsi) * dE_p + cPhi * cThe * dD;
                        
                        if (x3_p > 5.0) {
                            double localScreenX_p = (monWidth / 2.0) + (y3_p / x3_p) * focalLengthX;
                            double localScreenY_p = (h / 2.0) + (z3_p / x3_p) * focalLengthY; 
                            double globalScreenX_p = (m * monWidth) + localScreenX_p;
                            
                            QPointF startPoint(globalScreenX, globalScreenY);
                            QPointF endPoint(globalScreenX_p, localScreenY_p);

                            if (alertLevel == 3) {
                                double startVisualSize = 30.0 / x3 * focalLengthX;
                                double startW = std::max(24.0, std::min(200.0, startVisualSize));
                                double endVisualSize = 30.0 / x3_p * focalLengthX;
                                double endW = std::max(24.0, std::min(200.0, endVisualSize));
                                drawSolidTunnel(painter, startPoint, endPoint, startW, endW, coreColor, frameCounter * 3);
                            } else {
                                QLinearGradient glowGrad(startPoint, endPoint);
                                glowGrad.setColorAt(0.0, glowColor); glowGrad.setColorAt(1.0, Qt::transparent);
                                QPen dashGlowPen(QBrush(glowGrad), 8); dashGlowPen.setStyle(Qt::SolidLine);  
                                painter.setPen(dashGlowPen); painter.drawLine(startPoint, endPoint);
                            }
                        }
                    }

                    // 2. 绘制目标框 (Box Geometry)
                    double visualSize = 30.0 / x3 * focalLengthX;
                    int boxSize = std::max(24, std::min(200, (int)visualSize));
                    QRectF boxRect(globalScreenX - boxSize/2, globalScreenY - boxSize/2, boxSize, boxSize);
                    
                    if (cat == CAT_ROTORCRAFT) drawGlowEllipse(painter, boxRect, coreColor, glowColor, 2);
                    else if (cat == CAT_UAV) { /* UAV logic */ }
                    else {
                        drawGlowRect(painter, boxRect, coreColor, glowColor, 4, 2);
                        if (cat == CAT_HEAVY) drawGlowRect(painter, boxRect.adjusted(6, 6, -6, -6), coreColor, glowColor, 2, 2);
                    }
                    painter.setBrush(coreColor); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(globalScreenX, globalScreenY), 2, 2);

                    // 3. 绘制文字标签
                    QColor textColor = coreColor; textColor.setAlpha(240); 
                    painter.setFont(QFont(getTechFont(), 14, QFont::Black)); 
                    painter.setPen(textColor);
                    painter.drawText(QRectF(globalScreenX-100, globalScreenY-boxSize/2-30, 200, 25), Qt::AlignCenter|Qt::AlignBottom, ac.model.isEmpty() ? "TARGET" : ac.model);
                    painter.drawText(QRectF(globalScreenX-100, globalScreenY+boxSize/2+6, 200, 20), Qt::AlignCenter, QString("%1 NM").arg(slantRange/METERS_PER_NM, 0, 'f', 1));
                    
                    if (alertLevel == 3 && t_CPA > 0 && t_CPA < 999.0) {
                        int rowH = 22; int boxH = rowH * 2; int boxW = 120; int labelW = 55; int valW = boxW - labelW;
                        int dataX = globalScreenX + boxSize/2 + 20; 
                        int dataY = globalScreenY - 10; 

                        painter.setPen(Qt::NoPen); 
                        painter.setBrush(QColor(0, 0, 0, 180)); painter.drawRect(QRectF(dataX, dataY, labelW, rowH)); painter.drawRect(QRectF(dataX, dataY + rowH, labelW, rowH));
                        painter.setBrush(QColor(40, 40, 40, 120)); painter.drawRect(QRectF(dataX + labelW, dataY, valW, rowH)); painter.drawRect(QRectF(dataX + labelW, dataY + rowH, valW, rowH));
                        painter.setBrush(coreColor); painter.drawRect(QRectF(dataX, dataY, 3, boxH)); 

                        painter.setPen(coreColor); painter.setFont(QFont(getTechFont(), 11, QFont::Bold));
                        painter.drawText(QRectF(dataX + 8, dataY, labelW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, "T-CPA");
                        painter.drawText(QRectF(dataX + labelW + 8, dataY, valW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, QString("%1 s").arg((int)t_CPA));
                    }
                }
            }
        }
    } // 多视锥循环结束
    
    // 恢复裁剪遮罩
    painter.setClipping(false);
}

void TrafficDisplayWidget::onRefreshTimer() { update(); }
void TrafficDisplayWidget::onAircraftUpdated(const QString &) { update(); }
void TrafficDisplayWidget::onOwnshipUpdated() { update(); }
void TrafficDisplayWidget::keyPressEvent(QKeyEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mousePressEvent(QMouseEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mouseMoveEvent(QMouseEvent *e) { e->ignore(); }
