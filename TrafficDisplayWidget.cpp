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
#include <QDebug>
#include <QLinearGradient>
#include <QPolygonF>

// =========================================================
// CPA Predictive Thresholds (Strictly aligned with Thesis Table)
// =========================================================
const double METERS_PER_NM = 1852.0;
const double FT_TO_METERS = 0.3048;
const double KNOTS_TO_MPS = 0.514444;

// Advisory - Cyan (Tactical Horizon)
const double CPA_ADVS_TIME = 120.0; // seconds
const double ADVS_MAX_DIST = 15.0 * METERS_PER_NM; 
const double ADVS_MAX_ALT  = 3000.0 * FT_TO_METERS;

// Caution - Amber
const double CPA_CAUT_TIME = 60.0; // seconds
const double CPA_CAUT_DIST = 3.0 * METERS_PER_NM; // 原为 2.0，依据论文更新为雷达间隔 3.0
const double CPA_CAUT_ALT  = 800.0 * FT_TO_METERS;

// Warning - Red
const double CPA_WARN_TIME = 30.0; // 原为 45.0，依据 TCAS RA 更新为 30.0
const double CPA_WARN_DIST = 1.5 * METERS_PER_NM; // 原为 1.0，依据 LoWC 更新为 1.5
const double CPA_WARN_ALT  = 600.0 * FT_TO_METERS;

// Override - Absolute Near-Range Safety Backup (DMOD)
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

// =========================================================
// Dual-Pass Rendering Helpers
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
// 3D Tunnel Design 1: Glow Core (Optical Advanced)
// =========================================================
void drawSolidTunnel_Glow(QPainter &painter, const QPointF &startP, const QPointF &endP,
    double startW, double endW, const QColor &baseColor, int animOffset) {
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
        QColor glowColor(255, 40, 40, globalAlpha * 0.5); 
        QColor coreColor(255, 180, 180, globalAlpha * 0.9);

        QPen glowPen(glowColor, 12);
        glowPen.setJoinStyle(Qt::RoundJoin); glowPen.setCapStyle(Qt::RoundCap);
        painter.setPen(glowPen); painter.drawPath(path);

        QPen corePen(coreColor, 2);
        corePen.setJoinStyle(Qt::RoundJoin); corePen.setCapStyle(Qt::RoundCap);
        painter.setPen(corePen); painter.drawPath(path);
    }
}

// =========================================================
// 3D Tunnel Design 2: Ribbed Semi-Transparent
// =========================================================
void drawSolidTunnel_Ribbed(QPainter &painter, const QPointF &startP, const QPointF &endP,
                  double startW, double endW, const QColor &baseColor, int animOffset) {
    QVector2D vec(endP - startP);
    if (vec.length() < 2.0) return;
    QVector2D dir = vec.normalized();
    QVector2D perp(-dir.y(), dir.x());

    QPointF sL = startP - (perp.toPointF() * startW / 2.0);
    QPointF sR = startP + (perp.toPointF() * startW / 2.0);
    QPointF eL = endP - (perp.toPointF() * endW / 2.0);
    QPointF eR = endP + (perp.toPointF() * endW / 2.0);

    QLinearGradient fillGrad(startP, endP);
    QColor startFill = baseColor; startFill.setAlpha(60); 
    QColor endFill = baseColor;   endFill.setAlpha(0);    
    fillGrad.setColorAt(0.0, startFill); fillGrad.setColorAt(1.0, endFill);

    QLinearGradient edgeGrad(startP, endP);
    QColor startEdge = baseColor; startEdge.setAlpha(220);
    QColor endEdge = baseColor;   endEdge.setAlpha(0);
    edgeGrad.setColorAt(0.0, startEdge); edgeGrad.setColorAt(1.0, endEdge);

    painter.setPen(Qt::NoPen);
    painter.setBrush(fillGrad);
    QPolygonF bandPoly; bandPoly << sL << sR << eR << eL;
    painter.drawPolygon(bandPoly);

    painter.setPen(QPen(QBrush(edgeGrad), 4));
    painter.drawLine(sL, eL); painter.drawLine(sR, eR);

    double ribSpacing = 40.0;
    double phase = (animOffset % 40);
    
    painter.setBrush(Qt::NoBrush);
    for (double d = phase; d < vec.length(); d += ribSpacing) {
        double t = d / vec.length();
        QPointF cPos = startP + (dir.toPointF() * d);
        double currW = (startW * (1.0 - t) + endW * t);
        
        QColor ribColor = baseColor; 
        ribColor.setAlpha(180 * (1.0 - t)); 
        painter.setPen(QPen(ribColor, 4));
        
        QPointF leftPt = cPos - (perp.toPointF() * currW / 2.0);
        QPointF rightPt = cPos + (perp.toPointF() * currW / 2.0);
        QPointF centerPt = cPos + (dir.toPointF() * (currW / 2.0)); 
        
        QPainterPath chevron;
        chevron.moveTo(leftPt); chevron.lineTo(centerPt); chevron.lineTo(rightPt);
        painter.drawPath(chevron);
    }
}

const double FOV_SCALE = 1.0; 
const double PITCH_OFFSET = 0.0;
const double YAW_OFFSET = 0.0;
const double ROLL_OFFSET = 0.0;

// =========================================================
// Main Widget Constructor & UDP Logic
// =========================================================
TrafficDisplayWidget::TrafficDisplayWidget(AircraftManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager), m_currentMode(MODE_PROPOSED_B_GLOW) { // 默认启动模式为 3
    
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

    // 启动 UDP 遥控器端口监听
    m_cmdSocket = new QUdpSocket(this);
    if (m_cmdSocket->bind(QHostAddress::Any, 8888)) {
        qDebug() << "✅ AR UI Remote Control Active (Port 8888). Default Mode: Core Glow Tunnel (2).";
    }
    connect(m_cmdSocket, &QUdpSocket::readyRead, this, &TrafficDisplayWidget::onCommandReceived);
}

// 接收 UDP 遥控器指令
void TrafficDisplayWidget::onCommandReceived() {
    while (m_cmdSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_cmdSocket->pendingDatagramSize());
        m_cmdSocket->readDatagram(datagram.data(), datagram.size());
        QString msg = QString::fromUtf8(datagram).trimmed();

        if (msg.startsWith("SYS_MODE:")) {
            int cmd = msg.split(":")[1].toInt();
            if (cmd == 1) {
                m_currentMode = MODE_BASELINE_A;
                qDebug() << ">>> Switch: MODE_BASELINE_A (2D Boxes)";
            } else if (cmd == 2) {
                m_currentMode = MODE_PROPOSED_B_GLOW;
                qDebug() << ">>> Switch: MODE_PROPOSED_B_GLOW (Core Glow Tunnel)";
            } else if (cmd == 3) {
                m_currentMode = MODE_PROPOSED_B_RIBBED;
                qDebug() << ">>> Switch: MODE_PROPOSED_B_RIBBED (Ribbed Tunnel)";
            }
            update(); // 强制画面刷新
        }
    }
}

// =========================================================
// Main Rendering Loop
// =========================================================
void TrafficDisplayWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    static int frameCounter = 0; frameCounter++;
    bool flashOn = (frameCounter % 20) < 12;

    AircraftData *own = m_manager->getOwnship();
    
    // 如果没有收到飞机数据，直接退出，保持全透明
    if (!own || (std::abs(own->latitude) < 0.001 && std::abs(own->longitude) < 0.001)) {
        return; 
    }

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
        
        double dN = (ac.latitude - own->latitude) * 111120.0;
        double dE = (ac.longitude - own->longitude) * 111120.0 * std::cos(own->latitude * M_PI / 180.0);
        double dD = (own->altitude - ac.altitude) * FT_TO_METERS; 
        double slantRange = std::sqrt(dN*dN + dE*dE + dD*dD); 
        int dAlt100 = (int)((ac.altitude - own->altitude) / 100.0);
        
        if (slantRange < 10.0) continue;

        double x3 = cThe * cPsi * dN + cThe * sPsi * dE - sThe * dD; 
        double y3 = (sPhi * sThe * cPsi - cPhi * sPsi) * dN + (sPhi * sThe * sPsi + cPhi * cPsi) * dE + sPhi * cThe * dD; 
        double z3 = (cPhi * sThe * cPsi + sPhi * sPsi) * dN + (cPhi * sThe * sPsi - sPhi * cPsi) * dE + cPhi * cThe * dD; 

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
            double cpa_N = dN + dv_N * t_CPA;
            double cpa_E = dE + dv_E * t_CPA;
            d_CPA = std::sqrt(cpa_N * cpa_N + cpa_E * cpa_E);
        }

        double absVertSep = std::abs(dD);

        bool inSpatialEnvelope = (slantRange <= ADVS_MAX_DIST && absVertSep <= ADVS_MAX_ALT);
        bool isTacticalTime = (t_CPA > 0 && t_CPA <= CPA_ADVS_TIME);
        bool isCloseProximity = (slantRange <= 5.0 * METERS_PER_NM); // 物理上非常近，即便背向飞行也应保留作为态势感知

        if (!inSpatialEnvelope || (!isTacticalTime && !isCloseProximity)) {
            continue; // 彻底跳过该飞机的渲染，保持屏幕纯净
        }

        // 2. 警报层级判定 (严格按照论文 Table 1 自下而上覆盖)
        int alertLevel = 1; // 经过上面的过滤，活下来的默认是 Advisory (Cyan)

        // 判定 Caution (Amber)
        if (t_CPA > 0 && t_CPA <= CPA_CAUT_TIME && d_CPA <= CPA_CAUT_DIST && absVertSep <= CPA_CAUT_ALT) { 
            alertLevel = 2; 
        }

        // 判定 Warning (Red) 及其绝对兜底 (DMOD Override)
        if (slantRange <= OVERRIDE_WARN_DIST && absVertSep <= OVERRIDE_WARN_ALT) { 
            alertLevel = 3; 
        } else if (t_CPA > 0 && t_CPA <= CPA_WARN_TIME && d_CPA <= CPA_WARN_DIST && absVertSep <= CPA_WARN_ALT) { 
            alertLevel = 3; 
        }


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
            // 预测轨迹与隧道渲染 (仅非 Baseline 模式生效)
            // =========================================================
            if (m_currentMode != MODE_BASELINE_A && alertLevel >= 2  && t_CPA > 0) {
                 double dv_len = std::sqrt(dv_sq);
                 double dN_p = dN; double dE_p = dE;
                 
                 if (dv_len > 0.1) {
                    double maxLookAhead = 12.0; 
                    double scaleTime = (t_CPA > 0 && t_CPA < maxLookAhead) ? t_CPA : maxLookAhead; 
                    double maxTravel = slantRange - 300.0; 
                    if (maxTravel < 0) maxTravel = 0;
                    if (scaleTime * dv_len > maxTravel) scaleTime = maxTravel / dv_len;
                    
                    dN_p = dN + dv_N * scaleTime; dE_p = dE + dv_E * scaleTime;
                }
                 
                 double x3_p = cThe * cPsi * dN_p + cThe * sPsi * dE_p - sThe * dD;
                 double y3_p = (sPhi * sThe * cPsi - cPhi * sPsi) * dN_p + (sPhi * sThe * sPsi + cPhi * cPsi) * dE_p + sPhi * cThe * dD;
                 double z3_p = (cPhi * sThe * cPsi + sPhi * sPsi) * dN_p + (cPhi * sThe * sPsi - sPhi * cPsi) * dE_p + cPhi * cThe * dD;
                 
                 if (x3_p > 5.0) {
                     double screenX_p = centerX + (y3_p / x3_p) * focalLengthX;
                     double screenY_p = centerY + (z3_p / x3_p) * focalLengthY; 
                     QPointF startPoint(screenX, screenY); QPointF endPoint(screenX_p, screenY_p);

                     if (alertLevel == 3) {
                        double startW = std::max(24.0, std::min(200.0, 30.0 / x3 * focalLengthX));
                        double endW = std::max(24.0, std::min(200.0, 30.0 / x3_p * focalLengthX));

                        // 根据子模式选择画 Glow 还是 Ribbed
                        if (m_currentMode == MODE_PROPOSED_B_GLOW) {
                            drawSolidTunnel_Glow(painter, startPoint, endPoint, startW, endW, coreColor, frameCounter * 3);
                        } else if (m_currentMode == MODE_PROPOSED_B_RIBBED) {
                            drawSolidTunnel_Ribbed(painter, startPoint, endPoint, startW, endW, coreColor, frameCounter * 3);
                        }
                     } else {
                        // Caution Stage
                        QLinearGradient fadeGrad(startPoint, endPoint);
                        fadeGrad.setColorAt(0.0, coreColor); fadeGrad.setColorAt(1.0, QColor(coreColor.red(), coreColor.green(), coreColor.blue(), 0));
                        QLinearGradient glowGrad(startPoint, endPoint);
                        glowGrad.setColorAt(0.0, glowColor); glowGrad.setColorAt(1.0, QColor(glowColor.red(), glowColor.green(), glowColor.blue(), 0));

                        QPen dashGlowPen(QBrush(glowGrad), 8); dashGlowPen.setStyle(Qt::SolidLine);  
                        QPen dashCorePen(QBrush(fadeGrad), 3); QVector<qreal> dashPattern; dashPattern << 15 << 10; dashCorePen.setDashPattern(dashPattern);
                        painter.setPen(dashGlowPen); painter.drawLine(startPoint, endPoint);
                        painter.setPen(dashCorePen); painter.drawLine(startPoint, endPoint);
                        
                        QVector2D dir(endPoint - startPoint);
                        if (dir.length() > 1.0) {
                            dir.normalize(); QVector2D perp(-dir.y(), dir.x());
                            QPointF tip = endPoint; QPointF base = tip - dir.toPointF() * 22.0;
                            QPainterPath arrowPath; arrowPath.moveTo(tip); arrowPath.lineTo(base + perp.toPointF() * 10.0); arrowPath.lineTo(base - perp.toPointF() * 10.0); arrowPath.closeSubpath();
                            QColor arrowColor = coreColor; arrowColor.setAlpha(120); painter.setBrush(arrowColor); painter.setPen(Qt::NoPen); painter.drawPath(arrowPath);
                        }
                    }
                 }
            }

            // =========================================================
            // 分类几何框体 (Mode A 只有 2D，Mode B/C 有 3D)
            // =========================================================
            double visualSize = 30.0 / x3 * focalLengthX;
            int boxSize = std::max(24, std::min(200, (int)visualSize));
            QRectF boxRect(screenX - boxSize/2, screenY - boxSize/2, boxSize, boxSize);
            
            if (m_currentMode == MODE_BASELINE_A) {
                // Baseline 模式永远只画基本的 2D 框
                drawGlowRect(painter, boxRect, coreColor, glowColor, 4, 2);
            } else {
                if (cat == CAT_ROTORCRAFT) {
                    drawGlowEllipse(painter, boxRect, coreColor, glowColor, 2);
                } else if (cat == CAT_UAV) {
                    QPolygonF tri; tri << boxRect.topLeft() << boxRect.topRight() << QPointF(boxRect.center().x(), boxRect.bottom());
                    painter.setPen(QPen(glowColor, 6)); painter.setBrush(Qt::NoBrush); painter.drawPolygon(tri);
                    painter.setPen(QPen(coreColor, 2)); painter.drawPolygon(tri);
                } else {
                    drawGlowRect(painter, boxRect, coreColor, glowColor, 4, 2);
                    if (cat == CAT_HEAVY) drawGlowRect(painter, boxRect.adjusted(6, 6, -6, -6), coreColor, glowColor, 2, 2);
                }
            }
            painter.setBrush(coreColor); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(screenX, screenY), 2, 2);

            // =========================================================
            // 文本信息渲染
            // =========================================================
            QColor textColor = coreColor; textColor.setAlpha(240); 
            painter.setFont(QFont(getTechFont(), 14, QFont::Black)); 
            painter.setPen(textColor);
            
            QString topText = ac.model.isEmpty() ? "TARGET" : ac.model;
            painter.drawText(QRectF(screenX-100, screenY-boxSize/2-30, 200, 25), Qt::AlignCenter|Qt::AlignBottom, topText);
            
            QString distText = QString("%1 NM").arg(slantRange/METERS_PER_NM, 0, 'f', 1);
            QString altSign = (dAlt100 >= 0) ? "▲" : "▼";
            QString altText = QString("%1 %2").arg(altSign).arg(std::abs(dAlt100));
            
            painter.setFont(QFont(getTechFont(), 14, QFont::Bold));
            painter.drawText(QRectF(screenX-100, screenY+boxSize/2+6, 200, 20), Qt::AlignCenter, distText);
            painter.drawText(QRectF(screenX-100, screenY+boxSize/2+26, 200, 20), Qt::AlignCenter, altText);

            // T-CPA 面板 (仅限非 Baseline 模式)
            if (m_currentMode != MODE_BASELINE_A && alertLevel == 3 && t_CPA > 0 && t_CPA < 999.0) {
                QString tcpaVal = QString("%1 s").arg((int)t_CPA);
                QString dcpaVal = QString("%1 NM").arg(d_CPA / METERS_PER_NM, 0, 'f', 2);

                int dataX = screenX + boxSize/2 + 20; 
                int dataY = screenY - 10; 

                painter.setPen(Qt::NoPen); 
                QColor bgLabel = QColor(0, 0, 0, 180);       
                QColor bgValue = QColor(40, 40, 40, 120);    

                painter.setBrush(bgLabel); painter.drawRect(QRectF(dataX, dataY, 55, 22));
                painter.setBrush(bgValue); painter.drawRect(QRectF(dataX + 55, dataY, 65, 22));
                painter.setBrush(bgLabel); painter.drawRect(QRectF(dataX, dataY + 22, 55, 22));
                painter.setBrush(bgValue); painter.drawRect(QRectF(dataX + 55, dataY + 22, 65, 22));

                painter.setBrush(coreColor); 
                painter.drawRect(QRectF(dataX, dataY, 3, 44)); 

                painter.setPen(coreColor);
                painter.setFont(QFont(getTechFont(), 11, QFont::Bold));
                painter.drawText(QRectF(dataX + 8, dataY, 47, 22), Qt::AlignLeft | Qt::AlignVCenter, "T-CPA");
                painter.drawText(QRectF(dataX + 63, dataY, 57, 22), Qt::AlignLeft | Qt::AlignVCenter, tcpaVal);
                painter.drawText(QRectF(dataX + 8, dataY + 22, 47, 22), Qt::AlignLeft | Qt::AlignVCenter, "D-CPA");
                painter.drawText(QRectF(dataX + 63, dataY + 22, 57, 22), Qt::AlignLeft | Qt::AlignVCenter, dcpaVal);
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
