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

namespace {

/**
 * 【极速单屏模式】
 * 直接获取 Windows 的主屏幕（通常也就是您三联屏正中间的那块）。
 * 不再进行任何多屏宽度合并！
 */
static QRect computeRightWallGeometry()
{
    // 获取当前的主显示器（X-Plane 单屏运行时通常都在这里）
    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary) {
        return primary->geometry(); 
    }
    // 兜底分辨率
    return QRect(0, 0, 1920, 1080);
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

    // ==========================================================
    // 🎯 核心魔法：打破 Windows 跨屏限制
    // ==========================================================
    
    // 强制先生成底层操作系统的窗口句柄 (极其关键，否则无法跨屏)
    this->create(); 

    // 获取所有屏幕，并按 X 坐标从左到右严格排序
    QList<QScreen *> screens = QGuiApplication::screens();
    std::sort(screens.begin(), screens.end(), [](QScreen *a, QScreen *b) {
        return a->geometry().x() < b->geometry().x();
    });

    // 侦察：打印当前 Windows 真实的屏幕排布
    // (因为关闭了 Display PRO，您之前的 3840 坐标很可能已经变了！)
    qDebug() << "========== 屏幕侦察报告 ==========";
    for (int i = 0; i < screens.size(); ++i) {
        qDebug() << "排序索引" << i << ":" << screens[i]->name() << "真实坐标:" << screens[i]->geometry();
    }

    // 选取目标屏幕 (假设：索引 0 是主控屏，1 是左投影，2 是中投影，3 是右投影)
    // 我们强制抓取索引为 2 的屏幕（从左往右数第 3 块）
    QScreen *targetScreen = screens.first(); // 兜底
    if (screens.size() >= 3) {
        targetScreen = screens[2]; 
    }
    
    qDebug() << "🎯 强行降落目标:" << targetScreen->name() << targetScreen->geometry();

    // 将底层渲染引擎死死绑定到这块屏幕上
    if (this->windowHandle()) {
        this->windowHandle()->setScreen(targetScreen);
    }

    // 设置几何坐标
    m_targetWallGeometry = targetScreen->geometry();
    this->setGeometry(m_targetWallGeometry);

    // ==========================================================
    // 🔍 显影测试液：给窗口涂上绿色半透明底和红色边框
    // ==========================================================
    setStyleSheet("QMainWindow { background: rgba(0, 255, 0, 60); border: 10px solid red; }");
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
    
    // 确保窗口标志正确
    Qt::WindowFlags flags = windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        flags |= Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
        applyWallGeometry(); // Windows 上 setWindowFlags 会重建窗口，几何会丢
    }
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



