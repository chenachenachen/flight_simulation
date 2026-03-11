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
// CPA Predictive Thresholds (Table 3.3)
// =========================================================
const double METERS_PER_NM = 1852.0;
const double FT_TO_METERS = 0.3048;
const double KNOTS_TO_MPS = 0.514444;

// Warning - Red
const double CPA_WARN_TIME = 45.0; // seconds    
const double CPA_WARN_DIST = 1.0 * METERS_PER_NM; 
const double CPA_WARN_ALT  = 600.0 * FT_TO_METERS;

// Caution - Amber
const double CPA_CAUT_TIME = 60.0; // seconds
const double CPA_CAUT_DIST = 2.0 * METERS_PER_NM;
const double CPA_CAUT_ALT  = 800.0 * FT_TO_METERS;

// Over ride - Absolute Near-Range Safety Backup
const double OVERRIDE_WARN_DIST = 0.5 * METERS_PER_NM;
const double OVERRIDE_WARN_ALT  = 300.0 * FT_TO_METERS;

// =========================================================
// Color Settings
// =========================================================
const QColor COL_WARN_CORE(255, 60, 60, 255);
const QColor COL_WARN_GLOW(255, 0, 0, 140);
const QColor COL_CAUT_CORE(255, 210, 0, 255);
const QColor COL_CAUT_GLOW(255, 170, 0, 140);
const QColor COL_ADVS_CORE(0, 255, 255, 255);
const QColor COL_ADVS_GLOW(0, 255, 255, 110);

QString getTechFont() { return "Arial"; }

// =========================================================
// Target Categorization
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

struct ThreatData {
    QVector3D vec;
    int level;
    double dist;
    QString label;
};

// =========================================================
// Dual-Pass Rendering
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
// 3D Tunnel Prediction Drawing (Simple Layer - Optical Advanced Quality)
// =========================================================
void drawSolidTunnel(QPainter &painter, const QPointF &startP, const QPointF &endP,
    double startW, double endW, const QColor &baseColor, int animOffset) {
    QVector2D vec(endP - startP);
    if (vec.length() < 2.0) return;
    QVector2D dir = vec.normalized();
    QVector2D perp(-dir.y(), dir.x());

    double totalLen = vec.length();
    double chevronSpacing = 45.0; 
    double phase = (animOffset % (int)chevronSpacing);

    painter.setBrush(Qt::NoBrush); 

    // 🌟 开启加法混合模式 (Additive Blending)，让它看起来像真实的光束！
    // 注意：如果这行导致在白云背景下看不清，可以注释掉这行，保留下面的颜色优化即可
    // painter.setCompositionMode(QPainter::CompositionMode_Plus); 

    for (double d = phase; d < totalLen; d += chevronSpacing) {
    double t = d / totalLen; 
    QPointF cPos = startP + (dir.toPointF() * d);
    double currW = startW * (1.0 - t) + endW * t;

    QPointF frontPt = cPos + (dir.toPointF() * (currW / 2.5)); 
    QPointF leftPt  = cPos - (perp.toPointF() * currW / 2.0);  
    QPointF rightPt = cPos + (perp.toPointF() * currW / 2.0);  

    QPainterPath path;
    path.moveTo(leftPt);
    path.lineTo(frontPt);
    path.lineTo(rightPt);

    // --------------------------------------------
    // 🌟 调色秘籍：分离“核心光”与“边缘晕”
    // --------------------------------------------
    int globalAlpha = std::max(0, (int)(255 * (1.0 - t))); 

    // 1. 光晕 (Glow)：颜色稍微偏暗/偏橘，极其柔和，透明度低
    QColor glowColor(255, 40, 40); // 略微收敛的红
    glowColor.setAlpha(globalAlpha * 0.5); // 光晕透明度压低，避免糊成一团

    // 2. 核心 (Core)：颜色极亮（近乎粉白），透明度高
    QColor coreColor(255, 180, 180); // 泛白的核心光！这是高级感的关键！
    coreColor.setAlpha(globalAlpha * 0.9);

    // --------------------------------------------
    // 🌟 笔触优化：使用圆角与圆帽
    // --------------------------------------------
    // 外层光晕画笔 (加粗到8像素，且端点和转角圆滑)
    QPen glowPen(glowColor, 12);
    glowPen.setJoinStyle(Qt::RoundJoin); 
    glowPen.setCapStyle(Qt::RoundCap);
    painter.setPen(glowPen);
    painter.drawPath(path);

    // 内层核心画笔 (2像素细白线)
    QPen corePen(coreColor, 2);
    corePen.setJoinStyle(Qt::RoundJoin);
    corePen.setCapStyle(Qt::RoundCap);
    painter.setPen(corePen);
    painter.drawPath(path);
    }

// 恢复默认渲染模式（如果上面开启了 CompositionMode_Plus 的话必须加这行）
// painter.setCompositionMode(QPainter::CompositionMode_SourceOver); 
}

// Compass Drawing (outside of view) 
// Future work: Implement this function
void draw3DThreatSphere(QPainter &painter, int screenW, int screenH, QList<ThreatData> &threats) { }

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

    // Blinking Modulation Function (Eq 4.9)
    static int frameCounter = 0; frameCounter++;
    bool flashOn = (frameCounter % 20) < 12;

    AircraftData *own = m_manager->getOwnship();
    if (!own) return;

    int w = width(); 
    int h = height();
    int centerX = w / 2; 
    int centerY = h / 2;

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
        
        // 1. Relative Normalization (Eq 4.4)
        double dN = (ac.latitude - own->latitude) * 111120.0;
        double dE = (ac.longitude - own->longitude) * 111120.0 * std::cos(own->latitude * M_PI / 180.0);
        double dD = (own->altitude - ac.altitude) * FT_TO_METERS; 
        double slantRange = std::sqrt(dN*dN + dE*dE + dD*dD); 
        int dAlt100 = (int)((ac.altitude - own->altitude) / 100.0);
        
        if (slantRange < 10.0) continue;

        // Perspective Projection (Eq 4.5)
        double x3 = cThe * cPsi * dN + cThe * sPsi * dE - sThe * dD; 
        double y3 = (sPhi * sThe * cPsi - cPhi * sPsi) * dN + (sPhi * sThe * sPsi + cPhi * cPsi) * dE + sPhi * cThe * dD; 
        double z3 = (cPhi * sThe * cPsi + sPhi * sPsi) * dN + (cPhi * sThe * sPsi - sPhi * cPsi) * dE + cPhi * cThe * dD; 

        // 2. Kinematic CPA Model & Relative Velocity Calculation
        double ownSpd_ms = own->speed * KNOTS_TO_MPS;
        double acSpd_ms  = ac.speed * KNOTS_TO_MPS;
        
        double v_own_N = ownSpd_ms * std::cos(own->heading * M_PI / 180.0);
        double v_own_E = ownSpd_ms * std::sin(own->heading * M_PI / 180.0);
        double v_ac_N  = acSpd_ms * std::cos(ac.heading * M_PI / 180.0);
        double v_ac_E  = acSpd_ms * std::sin(ac.heading * M_PI / 180.0);

        // Delta V components (Relative Motion Frame)
        double dv_N = v_ac_N - v_own_N;
        double dv_E = v_ac_E - v_own_E;
        double dv_sq = dv_N * dv_N + dv_E * dv_E;

        double t_CPA = -1.0; 
        if (dv_sq > 0.001) t_CPA = -(dE * dv_E + dN * dv_N) / dv_sq; 

        double d_CPA = slantRange; 
        if (t_CPA > 0) {
            double cpa_N = dN + dv_N * t_CPA;
            double cpa_E = dE + dv_E * t_CPA;
            d_CPA = std::sqrt(cpa_N * cpa_N + cpa_E * cpa_E);
        }

        // 3. Hierarchical Alerting Logic (Algorithm 2)
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
        bool isOnScreen = false;
        double screenX = -9999, screenY = -9999;
        
        if (x3 > 5.0) {
            screenX = centerX + (y3 / x3) * focalLengthX;
            screenY = centerY + (z3 / x3) * focalLengthY;
            if (screenX >= 0 && screenX <= w && screenY >= 0 && screenY <= h) isOnScreen = true;
        }

        if (!isOnScreen) {
            if (alertLevel >= 2) offScreenThreats.append({ QVector3D(y3, x3, z3), alertLevel, slantRange, ac.model });
        } 
        else {
            // =========================================================
            // 4. 预测轨迹渲染 (Relative Linear Extrapolation)
            // =========================================================
            // 触发逻辑 Render_tunnel = (v_abs > 60 kts) AND (AlertLevel >= 2)
            if (alertLevel >= 2 && ac.speed > 60.0 && t_CPA > 0) {
                 double dv_len = std::sqrt(dv_sq);
                 double dN_p = dN;
                 double dE_p = dE;
                 
                 if (dv_len > 0.1) {
                    // 修改：将预测线缩短为只显示未来 12 秒的轨迹，防止飞出屏幕
                    double maxLookAhead = 12.0; 
                    double scaleTime = maxLookAhead; 
                    
                    // 如果马上就要撞了 (t_CPA < 12)，就只画到撞击点
                    if (t_CPA > 0 && t_CPA < maxLookAhead) {
                        scaleTime = t_CPA; 
                    }
                    
                    // 保证预测终点不会穿透物理摄像机面
                    double maxTravel = slantRange - 300.0; 
                    if (maxTravel < 0) maxTravel = 0;
                    if (scaleTime * dv_len > maxTravel) scaleTime = maxTravel / dv_len;
                    
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

                     if (alertLevel == 3) {
                         // Warning Stage: Volumetric 3D Tunnel
                        // 1. Calculate the accurate screen width of the starting point
                        double startVisualSize = 30.0 / x3 * focalLengthX;
                        double startW = std::max(24.0, std::min(200.0, startVisualSize));
                        
                        // 2. Calculate the accurate screen width of the predicted end point
                        double endVisualSize = 30.0 / x3_p * focalLengthX;
                        double endW = std::max(24.0, std::min(200.0, endVisualSize));

                        // Warning Stage: Volumetric 3D Tunnel
                        drawSolidTunnel(painter, startPoint, endPoint, 
                                        startW, endW, coreColor, frameCounter * 3);
                     } else {
                        // Caution Stage: Glowing Dashed Line with Fading
                        QLinearGradient fadeGrad(startPoint, endPoint);
                        fadeGrad.setColorAt(0.0, coreColor);
                        fadeGrad.setColorAt(1.0, QColor(coreColor.red(), coreColor.green(), coreColor.blue(), 0));

                        QLinearGradient glowGrad(startPoint, endPoint);
                        glowGrad.setColorAt(0.0, glowColor);
                        glowGrad.setColorAt(1.0, QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 0));

                        QVector<qreal> dashPattern; 
                        dashPattern << 15 << 10; 

                        QPen dashGlowPen(QBrush(glowGrad), 8);
                        dashGlowPen.setStyle(Qt::SolidLine);  
                        
                        QPen dashCorePen(QBrush(fadeGrad), 3);  
                        dashCorePen.setDashPattern(dashPattern);

                        painter.setPen(dashGlowPen); painter.drawLine(startPoint, endPoint);
                        painter.setPen(dashCorePen); painter.drawLine(startPoint, endPoint);
                        
                        // arrow tip
                        QVector2D dir(endPoint - startPoint);
                        if (dir.length() > 1.0) {
                            dir.normalize();
                            QVector2D perp(-dir.y(), dir.x());
                            const double arrowLen = 22.0; const double arrowWidth = 10.0;
                            QPointF tip = endPoint; QPointF base = tip - dir.toPointF() * arrowLen;
                            QPointF left = base + perp.toPointF() * arrowWidth; QPointF right = base - perp.toPointF() * arrowWidth;
                            QPainterPath arrowPath; arrowPath.moveTo(tip); arrowPath.lineTo(left); arrowPath.lineTo(right); arrowPath.closeSubpath();
                            painter.setPen(Qt::NoPen); 
                            
                            QColor arrowColor = coreColor; arrowColor.setAlpha(120); // arrow semi-transparent to blend with dashed line
                            painter.setBrush(arrowColor); 
                            painter.drawPath(arrowPath);
                        }
                    }
                 }
            }

            // =========================================================
            // 5. Categorical Geometry (Table 3.2)
            // =========================================================
            double visualSize = 30.0 / x3 * focalLengthX;
            int boxSize = std::max(24, std::min(200, (int)visualSize));
            QRectF boxRect(screenX - boxSize/2, screenY - boxSize/2, boxSize, boxSize);
            
            if (cat == CAT_ROTORCRAFT) {
                drawGlowEllipse(painter, boxRect, coreColor, glowColor, 2);
            } else if (cat == CAT_UAV) {
                QPolygonF tri;
                tri << boxRect.topLeft() << boxRect.topRight() << QPointF(boxRect.center().x(), boxRect.bottom());
                painter.setPen(QPen(glowColor, 6)); painter.setBrush(Qt::NoBrush); painter.drawPolygon(tri);
                painter.setPen(QPen(coreColor, 2)); painter.drawPolygon(tri);
            } else {
                drawGlowRect(painter, boxRect, coreColor, glowColor, 4, 2);
                if (cat == CAT_HEAVY) drawGlowRect(painter, boxRect.adjusted(6, 6, -6, -6), coreColor, glowColor, 2, 2);
            }
            painter.setBrush(coreColor); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(screenX, screenY), 2, 2);

            // Alphanumeric Tags  
            QColor textColor = coreColor; textColor.setAlpha(240); // 提高文字不透明度
                        painter.setFont(QFont(getTechFont(), 14, QFont::Black)); 
            painter.setPen(textColor);
            
            QString topText = ac.model.isEmpty() ? "TARGET" : ac.model;
            painter.drawText(QRectF(screenX-100, screenY-boxSize/2-30, 200, 25), Qt::AlignCenter|Qt::AlignBottom, topText);
            
            QString distText = QString("%1 NM").arg(slantRange/METERS_PER_NM, 0, 'f', 1);
            QString altSign = (dAlt100 >= 0) ? "▲" : "▼";
            QString altText = QString("%1 %2").arg(altSign).arg(std::abs(dAlt100));
            
            painter.setFont(QFont(getTechFont(), 14, QFont::Bold));
            painter.drawText(QRectF(screenX-100, screenY+boxSize/2+6, 200, 20), Qt::AlignCenter, distText);
            
            painter.setFont(QFont(getTechFont(), 14, QFont::Bold));
            painter.drawText(QRectF(screenX-100, screenY+boxSize/2+26, 200, 20), Qt::AlignCenter, altText);

            // 只有当有碰撞风险 (Warning) 且 t_CPA 有效时才显示
            if (alertLevel == 3 && t_CPA > 0 && t_CPA < 999.0) {
                QString tcpaVal = QString("%1 s").arg((int)t_CPA);
                QString dcpaVal = QString("%1 NM").arg(d_CPA / METERS_PER_NM, 0, 'f', 2);

                // 定义网格尺寸
                int rowH = 22;           // 每行的高度
                int boxH = rowH * 2;     // 只有两行，总高度
                int boxW = 120;          // 总宽度
                int labelW = 55;         // 左侧 Label 的宽度
                int valW = boxW - labelW;// 右侧 Value 的宽度
                
                int dataX = screenX + boxSize/2 + 20; 
                int dataY = screenY - 10; // 去掉表头后，Y轴稍微往下压一点居中

                // ---------------------------------------------------
                // 2. 双色调底色渲染 (无边框色块拼贴)
                // ---------------------------------------------------
                painter.setPen(Qt::NoPen); // 🌟 核心：无描边！
                
                QColor bgLabel = QColor(0, 0, 0, 180);       // 左侧 Label 黑底
                QColor bgValue = QColor(40, 40, 40, 120);    // 右侧 Value 灰底

                painter.setBrush(bgLabel); painter.drawRect(QRectF(dataX, dataY, labelW, rowH));
                painter.setBrush(bgValue); painter.drawRect(QRectF(dataX + labelW, dataY, valW, rowH));
                painter.setBrush(bgLabel); painter.drawRect(QRectF(dataX, dataY + rowH, labelW, rowH));
                painter.setBrush(bgValue); painter.drawRect(QRectF(dataX + labelW, dataY + rowH, valW, rowH));

                painter.setBrush(coreColor); // 颜色跟随当前报警级别 (红/黄)
                painter.drawRect(QRectF(dataX, dataY, 3, boxH)); // 3个像素宽的彩色长条

                painter.setPen(coreColor);
                painter.setFont(QFont(getTechFont(), 11, QFont::Bold));
                
                painter.drawText(QRectF(dataX + 8, dataY, labelW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, "T-CPA");
                painter.drawText(QRectF(dataX + labelW + 8, dataY, valW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, tcpaVal);
                painter.drawText(QRectF(dataX + 8, dataY + rowH, labelW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, "D-CPA");
                painter.drawText(QRectF(dataX + labelW + 8, dataY + rowH, valW - 8, rowH), Qt::AlignLeft | Qt::AlignVCenter, dcpaVal);
            }
        }

    }
}
void TrafficDisplayWidget::onRefreshTimer() { update(); }
void TrafficDisplayWidget::onAircraftUpdated(const QString &) { update(); }
void TrafficDisplayWidget::onOwnshipUpdated() { update(); }
void TrafficDisplayWidget::keyPressEvent(QKeyEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mousePressEvent(QMouseEvent *e) { e->ignore(); }
void TrafficDisplayWidget::mouseMoveEvent(QMouseEvent *e) { e->ignore(); }
