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

// ★ 改动1：加入 Windows 原生 API 头文件
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/**
 * 【多屏定位】
 * 按 X 坐标排序所有屏幕，取索引2（从左往右第3块）作为中间投影仪。
 * 根据您的屏幕布局：
 *   screens[0] = 主控屏   Position 0,0
 *   screens[1] = 左投影   Position 1920,0
 *   screens[2] = 中投影   Position 3840,0  ← 目标
 *   screens[3] = 右投影   Position 5760,0
 */
static QRect computeTargetScreenGeometry()
{
    QList<QScreen *> screens = QGuiApplication::screens();

    // 按 X 坐标从左到右排序
    std::sort(screens.begin(), screens.end(), [](QScreen *a, QScreen *b) {
        return a->geometry().x() < b->geometry().x();
    });

    // 打印侦察报告，方便调试
    qDebug() << "========== 屏幕侦察报告 ==========";
    for (int i = 0; i < screens.size(); ++i) {
        qDebug() << "排序索引" << i << ":" << screens[i]->name()
                 << "坐标:" << screens[i]->geometry();
    }

    // 取索引2（中间投影仪）；屏幕不足时依次降级
    QScreen *target = nullptr;
    if (screens.size() >= 3) {
        target = screens[2];
    } else if (screens.size() >= 2) {
        target = screens[1];
    } else {
        target = QGuiApplication::primaryScreen();
    }

    qDebug() << "🎯 目标屏幕:" << target->name() << target->geometry();
    return target->geometry();
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
    
    // 初始化退出按钮指针为nullptr，避免未初始化访问
    m_quitButton = nullptr;

    setupOverlayWindow();
    setupUI();
    connectSignals();

    // 连接 XPlaneConnect 插件（与模拟器一致：192.168.0.22:49001）
    m_xplaneReceiver->startListening(49001);
    // 连接到BlueSky bridge（接收traffic数据）
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
    // 1. 设置窗口标志 —— 只设这一次，之后绝不再调用 setWindowFlags！
    Qt::WindowFlags flags = Qt::Window |
                           Qt::FramelessWindowHint |      // 无边框
                           Qt::WindowStaysOnTopHint |     // 始终置顶
                           Qt::WindowDoesNotAcceptFocus;  // 不接受焦点（关键：让键盘穿透）
    setWindowFlags(flags);
    
    // 2. 设置窗口属性（跨平台）
    setAttribute(Qt::WA_TranslucentBackground, true);     // 透明背景
    setAttribute(Qt::WA_ShowWithoutActivating, true);     // 显示但不激活
    setAttribute(Qt::WA_NoSystemBackground, true);        // 无系统背景
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    // 3. 隐藏状态栏
    statusBar()->hide();

    // ★ 改动2：强制创建底层句柄，再绑定目标屏幕，确保坐标正确
    this->create();

    // 计算目标屏幕几何（按X坐标排序取索引2）
    m_targetWallGeometry = computeTargetScreenGeometry();

    // 将底层渲染引擎绑定到目标屏幕
    QList<QScreen *> screens = QGuiApplication::screens();
    std::sort(screens.begin(), screens.end(), [](QScreen *a, QScreen *b) {
        return a->geometry().x() < b->geometry().x();
    });
    QScreen *targetScreen = (screens.size() >= 3) ? screens[2]
                          : (screens.size() >= 2) ? screens[1]
                          : QGuiApplication::primaryScreen();

    if (this->windowHandle()) {
        this->windowHandle()->setScreen(targetScreen);
    }

    // 设置几何坐标（在 show() 之前完成）
    this->setGeometry(m_targetWallGeometry);

    // 4. 样式表确保透明
    setStyleSheet("QMainWindow { background: transparent; }");
}

void MainWindow::applyWallGeometry()
{
    if (m_targetWallGeometry.isValid())
        setGeometry(m_targetWallGeometry);
}

// ==========================================
// 置顶维护
// ==========================================
void MainWindow::ensureOnTop() {
    if (!isVisible()) {
        show();
    }

    // ★ 改动3：用 Windows 原生 SetWindowPos 置顶+定位，完全不动 setWindowFlags
    // 这样不会触发窗口重建，坐标永远不会丢失
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(this->winId());
    ::SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        m_targetWallGeometry.x(),
        m_targetWallGeometry.y(),
        m_targetWallGeometry.width(),
        m_targetWallGeometry.height(),
        SWP_SHOWWINDOW | SWP_NOACTIVATE   // 显示但不抢焦点
    );
#else
    // 非 Windows 平台保留原逻辑
    Qt::WindowFlags f = windowFlags();
    if (!(f & Qt::WindowStaysOnTopHint)) {
        f |= Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
        applyWallGeometry();
    }
#endif
}

// ==========================================
// UI
// ==========================================
void MainWindow::setupUI() {
    setWindowTitle("Qt X-Plane Overlay");

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setAttribute(Qt::WA_TranslucentBackground, true);
    // 注意：不在centralWidget级别设置WA_TransparentForMouseEvents
    // 因为退出按钮需要接收鼠标事件，通过事件过滤器实现选择性穿透
    centralWidget->setFocusPolicy(Qt::NoFocus);  
    centralWidget->setStyleSheet("background: transparent;");
    setCentralWidget(centralWidget);
    
    // 为centralWidget安装事件过滤器，实现选择性鼠标穿透
    centralWidget->installEventFilter(this);
    
    // 关键：为MainWindow本身也安装事件过滤器，确保键盘事件被拦截
    installEventFilter(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *statusLayout = new QHBoxLayout();

    QLabel *xplaneStatus   = new QLabel("XP: --", this);
    QLabel *blueskyStatus  = new QLabel("BS: UDP", this);
    
    // 设置标签为鼠标穿透，不拦截事件
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
    applyWallGeometry();
#ifdef Q_OS_WIN
    // 部分 Windows 驱动/多 GPU 在首次 show 后才给出最终虚拟桌面坐标
    QTimer::singleShot(0, this, [this]() { applyWallGeometry(); });
#endif
    ensureOnTop();
    
    QTimer::singleShot(50, this, [this]() {
        if (!isVisible()) {
            show();
        }
        ensureOnTop();
    });
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
}

void MainWindow::requestQuit() {
    QApplication::quit();
}

// ★ 改动4：hideEvent 删掉强制 show 的 timer
// 原来的强制 show 会和 X-Plane 全屏抢焦点，造成闪烁和位置错乱
// onKeepOnTop() 每 500ms 已经在做同样的保活工作，不需要重复
void MainWindow::hideEvent(QHideEvent *event) {
    QMainWindow::hideEvent(event);
}

void MainWindow::changeEvent(QEvent *event) {
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


