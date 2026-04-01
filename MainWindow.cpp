#include "MainWindow.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <algorithm>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QKeyEvent>
#include <QShortcut>
#include <QEvent>
#include <QPushButton>
#include <QPainter>  // 🎯 新增：用于画十字准星

#ifdef Q_OS_MACOS
#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace {

/**
 * 【绝对物理单屏模式】
 * 彻底抛弃 Windows 的主屏幕识别逻辑！
 * 强制指定 X=3840，直接盖在中间那台投影仪上！
 */
static QRect computeRightWallGeometry()
{
    // 强制锁定：1920x1080，起点在 X=3840
    return QRect(3840, 0, 1920, 1080);
}

} // namespace

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
    Qt::WindowFlags flags = Qt::Window |
                           Qt::FramelessWindowHint |      
                           Qt::WindowStaysOnTopHint |     
                           Qt::WindowDoesNotAcceptFocus;  
    
    setWindowFlags(flags);
    
    setAttribute(Qt::WA_TranslucentBackground, true);     
    setAttribute(Qt::WA_ShowWithoutActivating, true);     
    setAttribute(Qt::WA_NoSystemBackground, true);        
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    statusBar()->hide();
    
    // 获取绝对物理坐标 (3840, 0)
    m_targetWallGeometry = computeRightWallGeometry();
    setGeometry(m_targetWallGeometry);
    
    setStyleSheet("QMainWindow { background: transparent; }");
    
#ifdef Q_OS_MACOS
    QTimer::singleShot(100, this, [this]() {
        WId winId = this->winId();
        if (winId) {
            void* nsView = reinterpret_cast<void*>(winId);
            SEL windowSel = sel_registerName("window");
            typedef void* (*GetWindowFunc)(void*, SEL);
            void* nsWindow = ((GetWindowFunc)objc_msgSend)(nsView, windowSel);
            
            if (nsWindow) {
                SEL respondsToSel = sel_registerName("respondsToSelector:");
                typedef bool (*RespondsToSelectorFunc)(void*, SEL, SEL);
                
                SEL setCanBecomeKeySel = sel_registerName("setCanBecomeKey:");
                if (respondsToSel && setCanBecomeKeySel) {
                    bool responds = ((RespondsToSelectorFunc)objc_msgSend)(nsWindow, respondsToSel, setCanBecomeKeySel);
                    if (responds) {
                        typedef void (*SetCanBecomeKeyFunc)(void*, SEL, bool);
                        ((SetCanBecomeKeyFunc)objc_msgSend)(nsWindow, setCanBecomeKeySel, false);
                    }
                }
                
                SEL setCanBecomeMainSel = sel_registerName("setCanBecomeMain:");
                if (respondsToSel && setCanBecomeMainSel) {
                    bool responds = ((RespondsToSelectorFunc)objc_msgSend)(nsWindow, respondsToSel, setCanBecomeMainSel);
                    if (responds) {
                        typedef void (*SetCanBecomeMainFunc)(void*, SEL, bool);
                        ((SetCanBecomeMainFunc)objc_msgSend)(nsWindow, setCanBecomeMainSel, false);
                    }
                }
                
                SEL orderFrontSel = sel_registerName("orderFront:");
                if (orderFrontSel) {
                    typedef void (*OrderFrontFunc)(void*, SEL, void*);
                    ((OrderFrontFunc)objc_msgSend)(nsWindow, orderFrontSel, nullptr);
                }
                
                SEL setHidesOnDeactivateSel = sel_registerName("setHidesOnDeactivate:");
                if (setHidesOnDeactivateSel) {
                    typedef void (*SetHidesOnDeactivateFunc)(void*, SEL, bool);
                    ((SetHidesOnDeactivateFunc)objc_msgSend)(nsWindow, setHidesOnDeactivateSel, false);
                }
                
                SEL setLevelSel = sel_registerName("setLevel:");
                if (setLevelSel) {
                    typedef void (*SetLevelFunc)(void*, SEL, long);
                    ((SetLevelFunc)objc_msgSend)(nsWindow, setLevelSel, 2002);
                }
                
                SEL setCollectionBehaviorSel = sel_registerName("setCollectionBehavior:");
                if (setCollectionBehaviorSel) {
                    typedef void (*SetCollectionBehaviorFunc)(void*, SEL, unsigned long);
                    ((SetCollectionBehaviorFunc)objc_msgSend)(nsWindow, setCollectionBehaviorSel, 17);
                }
            }
        }
    });
#endif
}

void MainWindow::applyWallGeometry()
{
    if (m_targetWallGeometry.isValid())
        setGeometry(m_targetWallGeometry);
}

// ==========================================
// 🎯 终极物理靶心：绘制贯穿全屏的十字
// ==========================================
void MainWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event); // 保留透明背景
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 画笔：亮绿色，3像素粗
    painter.setPen(QPen(Qt::green, 3)); 
    
    int w = width();
    int h = height();
    int cx = w / 2;
    int cy = h / 2;
    
    // 画横向和纵向的贯穿线
    painter.drawLine(0, cy, w, cy);
    painter.drawLine(cx, 0, cx, h);
    
    // 正中心画一个圆圈，充当瞄准器
    painter.drawEllipse(cx - 50, cy - 50, 100, 100);
}

// ==========================================
// 置顶维护
// ==========================================
void MainWindow::ensureOnTop() {
    if (!isVisible()) {
        show();
    }
    
#ifdef Q_OS_MACOS
    WId winId = this->winId();
    if (winId) {
        void* nsView = reinterpret_cast<void*>(winId);
        SEL windowSel = sel_registerName("window");
        typedef void* (*GetWindowFunc)(void*, SEL);
        void* nsWindow = ((GetWindowFunc)objc_msgSend)(nsView, windowSel);
        if (nsWindow) {
            SEL orderFrontSel = sel_registerName("orderFront:");
            if (orderFrontSel) {
                typedef void (*OrderFrontFunc)(void*, SEL, void*);
                ((OrderFrontFunc)objc_msgSend)(nsWindow, orderFrontSel, nullptr);
            }
        }
    }
#endif
    
    Qt::WindowFlags flags = windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        flags |= Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
        applyWallGeometry(); 
    }
}


// ==========================================
// UI
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
    
    // 左下角退出按钮
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(20, 0, 20, 20);
    
    m_quitButton = new QPushButton("Quit", this);
    m_quitButton->setFocusPolicy(Qt::StrongFocus);  
    m_quitButton->setAttribute(Qt::WA_TransparentForMouseEvents, false);  
    m_quitButton->setMinimumSize(120, 40);
    m_quitButton->setMaximumSize(120, 40);
    
    QString buttonStyle =
        "QPushButton { "
        "background-color: rgba(0, 0, 0, 180); "
        "color: #ff4444; "
        "border: 2px solid #ff4444; "
        "border-radius: 6px; "
        "font-weight: bold; "
        "font-size: 14px; "
        "padding: 6px; "
        "}"
        "QPushButton:hover { "
        "background-color: rgba(255, 68, 68, 100); "
        "color: white; "
        "}"
        "QPushButton:pressed { "
        "background-color: rgba(255, 68, 68, 150); "
        "}";
    m_quitButton->setStyleSheet(buttonStyle);
    
    connect(m_quitButton, &QPushButton::clicked, this, &MainWindow::requestQuit);
    
    bottomLayout->addWidget(m_quitButton);
    bottomLayout->addStretch();
    
    mainLayout->addLayout(bottomLayout);

    connect(m_xplaneReceiver, &XPlaneReceiver::connectionStatusChanged,
            [xplaneStatus](bool ok) {
                xplaneStatus->setText(ok ? "XP: OK" : "XP: --");
            });

    installEventFilter(this);
}

// ==========================================
// 定时
// ==========================================
void MainWindow::onKeepOnTop() {
    if (!isVisible()) {
        show();
    }
    ensureOnTop();
    
    QTimer::singleShot(50, this, [this]() {
        if (!isVisible()) {
            show();
            ensureOnTop();
        }
    });
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
// 事件
// ==========================================
void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    
    // 🎯 核心魔法：延迟传送！
    // 强行把刚生成的窗口踹到 X=3840 的屏幕上，打破 Windows 的限制
    QTimer::singleShot(100, this, [this]() {
        this->setGeometry(3840, 0, 1920, 1080);
    });

    QTimer::singleShot(500, this, [this]() {
        this->setGeometry(3840, 0, 1920, 1080);
        ensureOnTop();
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
    if (event->type() == QEvent::WindowStateChange || 
        event->type() == QEvent::ActivationChange) {
        
        if (isActiveWindow()) {
#ifdef Q_OS_MACOS
            WId winId = this->winId();
            if (winId) {
                void* nsView = reinterpret_cast<void*>(winId);
                SEL windowSel = sel_registerName("window");
                typedef void* (*GetWindowFunc)(void*, SEL);
                void* nsWindow = ((GetWindowFunc)objc_msgSend)(nsView, windowSel);
                if (nsWindow) {
                    SEL resignKeyWindowSel = sel_registerName("resignKeyWindow");
                    if (resignKeyWindowSel) {
                        typedef void (*ResignKeyWindowFunc)(void*, SEL);
                        ((ResignKeyWindowFunc)objc_msgSend)(nsWindow, resignKeyWindowSel);
                    }
                }
            }
#endif
        }
    }
    
    QMainWindow::changeEvent(event);
}

// 事件过滤器：处理键盘穿透和选择性鼠标穿透
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


