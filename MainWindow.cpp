#include "MainWindow.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QApplication>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QKeyEvent>
#include <QEvent>
#include <QPushButton>
#include <QPainter>

// ==========================================
// 构造 / 析构
// ==========================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {

    m_aircraftManager = new AircraftManager(this);

    m_xplaneReceiver  = new XPlaneReceiver(m_aircraftManager, this);
    m_blueSkyComm     = new BlueSkyCommunicator(m_aircraftManager, this);
    m_networkReceiver = new NetworkReceiver(m_aircraftManager, this);
    m_displayWidget   = new TrafficDisplayWidget(m_aircraftManager, this);
    
    m_quitButton = nullptr;

    setupOverlayWindow();
    setupUI();
    connectSignals();

    m_xplaneReceiver->startListening(49001);
    m_blueSkyComm->startListening(49004); 

    m_keepOnTopTimer = new QTimer(this);
    connect(m_keepOnTopTimer, &QTimer::timeout, this, &MainWindow::onKeepOnTop);
    m_keepOnTopTimer->start(500);

    ensureOnTop();
}

MainWindow::~MainWindow() {}

// ==========================================
// 核心：Overlay Window 设置
// ==========================================
void MainWindow::setupOverlayWindow() {
    // 1. 设置标志
    Qt::WindowFlags flags = Qt::Window |
                           Qt::FramelessWindowHint |
                           Qt::WindowStaysOnTopHint |
                           Qt::WindowDoesNotAcceptFocus;
    setWindowFlags(flags);
    
    // 2. 设置属性
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    statusBar()->hide();

    // 🎯 彻底写死坐标：锁定中间投影仪！
    m_targetWallGeometry = QRect(3840, 0, 1920, 1080);
    this->setGeometry(m_targetWallGeometry);

    // 样式表确保背景透明，绘制由 paintEvent 接管
    setStyleSheet("QMainWindow { background: transparent; }");
}

void MainWindow::applyWallGeometry()
{
    // 每次应用坐标时，都强行使用 3840
    m_targetWallGeometry = QRect(3840, 0, 1920, 1080);
    setGeometry(m_targetWallGeometry);
}

// ==========================================
// 🎨 测试绘图：红色大十字靶心
// ==========================================
void MainWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int cx = w / 2;
    int cy = h / 2;

    // 1. 画一条沿着窗口边缘的黄色 10 像素粗框 (用来检查画面有没有被系统偷偷缩放或裁剪)
    painter.setPen(QPen(Qt::yellow, 10));
    painter.drawRect(0, 0, w, h);

    // 2. 画贯穿全屏的红色十字准星
    painter.setPen(QPen(Qt::red, 4));
    painter.drawLine(0, cy, w, cy);     // 水平线
    painter.drawLine(cx, 0, cx, h);     // 垂直线

    // 3. 画正中心的圆靶心
    painter.drawEllipse(cx - 100, cy - 100, 200, 200);
}

// ==========================================
// 置顶维护
// ==========================================
void MainWindow::ensureOnTop() {
    if (!isVisible()) {
        show();
    }
    
    Qt::WindowFlags flags = windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        flags |= Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
        applyWallGeometry();
    }
}

// ==========================================
// UI 设置
// ==========================================
void MainWindow::setupUI() {
    setWindowTitle("Qt X-Plane Overlay");

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setAttribute(Qt::WA_TranslucentBackground, true);
    centralWidget->setFocusPolicy(Qt::NoFocus);  
    centralWidget->setStyleSheet("background: transparent;");
    setCentralWidget(centralWidget);
    
    centralWidget->installEventFilter(this);
    installEventFilter(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *statusLayout = new QHBoxLayout();

    QLabel *xplaneStatus   = new QLabel("XP: --", this);
    QLabel *blueskyStatus  = new QLabel("BS: UDP", this);
    
    xplaneStatus->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    xplaneStatus->setFocusPolicy(Qt::NoFocus);
    blueskyStatus->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    blueskyStatus->setFocusPolicy(Qt::NoFocus);

    QString style =
        "QLabel { background-color: rgba(0,0,0,150); color: white; "
        "padding: 4px; border-radius: 4px; font-weight: bold;}";

    xplaneStatus->setStyleSheet(style);
    blueskyStatus->setStyleSheet(style);

    statusLayout->addWidget(xplaneStatus);
    statusLayout->addWidget(blueskyStatus);
    statusLayout->addStretch();
    statusLayout->setContentsMargins(20, 20, 20, 20); 

    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(m_displayWidget, 1);
    
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(20, 0, 20, 20);
    
    m_quitButton = new QPushButton("Quit", this);
    m_quitButton->setFocusPolicy(Qt::StrongFocus);  
    m_quitButton->setAttribute(Qt::WA_TransparentForMouseEvents, false);  
    m_quitButton->setMinimumSize(120, 40);
    m_quitButton->setMaximumSize(120, 40);
    
    QString buttonStyle =
        "QPushButton { "
        "background-color: rgba(0, 0, 0, 180); color: #ff4444; "
        "border: 2px solid #ff4444; border-radius: 6px; "
        "font-weight: bold; font-size: 14px; padding: 6px; }"
        "QPushButton:hover { background-color: rgba(255, 68, 68, 100); color: white; }"
        "QPushButton:pressed { background-color: rgba(255, 68, 68, 150); }";
    m_quitButton->setStyleSheet(buttonStyle);
    
    connect(m_quitButton, &QPushButton::clicked, this, &MainWindow::requestQuit);
    
    bottomLayout->addWidget(m_quitButton);
    bottomLayout->addStretch();
    
    mainLayout->addLayout(bottomLayout);

    connect(m_xplaneReceiver, &XPlaneReceiver::connectionStatusChanged,
            [xplaneStatus](bool ok) {
                xplaneStatus->setText(ok ? "XP: OK" : "XP: --");
            });
}

// ==========================================
// 定时
// ==========================================
void MainWindow::onKeepOnTop() {
    if (!isVisible()) {
        show();
    }
    ensureOnTop();
    applyWallGeometry(); // 持续维持 3840 坐标
}

// ==========================================
// 信号
// ==========================================
void MainWindow::connectSignals() {
    connect(m_xplaneReceiver, &XPlaneReceiver::ownshipDataReceived,
            this, &MainWindow::onXPlaneDataReceived);
}

void MainWindow::onXPlaneDataReceived() {
    AircraftData *ownship = m_aircraftManager->getOwnship();
    m_blueSkyComm->sendOwnshipPosition(
        ownship->latitude,
        ownship->longitude,
        ownship->altitude,
        ownship->heading,
        ownship->speed);
}

// ==========================================
// 事件：🚀 核心魔法 - 延迟瞬移
// ==========================================
void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    
    // 很多系统在窗口刚显示时会锁定在主屏，我们等 100ms 后一脚把它踢到中间投影仪！
    QTimer::singleShot(100, this, [this]() {
        applyWallGeometry();
    });

    ensureOnTop();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
}

void MainWindow::requestQuit() {
    QApplication::quit();
}

void MainWindow::hideEvent(QHideEvent *event) {
    QTimer::singleShot(10, this, [this]() {
        if (!isVisible()) {
            show();
            ensureOnTop();
        }
    });
    QMainWindow::hideEvent(event);
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
}

// 事件过滤器
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        return false;
    }
    
    if (event->type() == QEvent::ShortcutOverride) {
        return false; 
    }
    
    QWidget *cw = centralWidget();
    if (obj == cw && cw && 
        (event->type() == QEvent::MouseButtonPress || 
         event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonDblClick)) {
        
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        
        if (m_quitButton && m_quitButton->isVisible() && m_quitButton->parentWidget()) {
            QPoint widgetPos = mouseEvent->pos(); 
            QPoint buttonPos = m_quitButton->pos();  
            QRect buttonRect(buttonPos, m_quitButton->size());
            
            if (buttonRect.contains(widgetPos)) {
                QPoint buttonLocalPos = widgetPos - buttonPos;
                QMouseEvent *buttonEvent = new QMouseEvent(
                    mouseEvent->type(),
                    buttonLocalPos,
                    mouseEvent->globalPos(),
                    mouseEvent->button(),
                    mouseEvent->buttons(),
                    mouseEvent->modifiers()
                );
                QApplication::postEvent(m_quitButton, buttonEvent);
                return true;  
            }
        }
        return true;  
    }
    
    if (event->type() == QEvent::WindowActivate) {
        return false;  
    }
    
    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::event(QEvent *event) {
    if (event->type() == QEvent::KeyPress || 
        event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::ShortcutOverride ||
        event->type() == QEvent::Shortcut) {
        return false;  
    }
    return QMainWindow::event(event);
}


