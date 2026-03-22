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
#ifdef Q_OS_MACOS
#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace {

/**
 * 与 Immersive Display 等多横排显示器一致：最左为主控屏，其余屏合并为一块大矩形。
 * 使用 Qt 虚拟桌面坐标，避免 main.cpp 里写死 1920 与 DPI 不一致。
 */
static QRect computeRightWallGeometry()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return QRect(1920, 0, 1920 * 3, 1080);

    QList<QScreen *> sorted = screens;
    std::sort(sorted.begin(), sorted.end(), [](const QScreen *a, const QScreen *b) {
        const int la = a->geometry().left();
        const int lb = b->geometry().left();
        if (la != lb)
            return la < lb;
        return a->geometry().top() < b->geometry().top();
    });

    if (sorted.size() == 1)
        return sorted.first()->geometry();

    QRect u;
    for (int i = 1; i < sorted.size(); ++i)
        u = u.united(sorted[i]->geometry());
    return u;
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
    // Bridge发送traffic数据到49004端口（避免与command port 49003冲突）
    // Bridge的command port是49003（接收ownship），qt_port是49004（发送traffic）
    m_blueSkyComm->startListening(49004);    // 设置发送ownship数据到bridge的command port（49001或49002）
    // 注意：这需要BlueSkyCommunicator支持单独的发送端口
    // 目前先发送到11000，bridge需要监听11000接收ownship数据

    m_keepOnTopTimer = new QTimer(this);
    connect(m_keepOnTopTimer, &QTimer::timeout, this, &MainWindow::onKeepOnTop);
    m_keepOnTopTimer->start(500);

    // 注意：由于窗口设置为NoFocus和WindowDoesNotAcceptFocus，键盘事件会穿透
    // 退出功能通过左下角的退出按钮实现
    // QShortcut 已移除，因为它们可能会拦截键盘事件，影响键盘穿透功能

    ensureOnTop();
}

MainWindow::~MainWindow() {}

// ==========================================
// 核心：Overlay Window 设置（已修复 macOS 消失问题）
// ==========================================
void MainWindow::setupOverlayWindow() {
    // 1. 设置窗口标志（跨平台通用方法）
    // 临时移除 Qt::Tool，看是否影响显示
    Qt::WindowFlags flags = Qt::Window |
                           Qt::FramelessWindowHint |      // 无边框
                           Qt::WindowStaysOnTopHint |     // 始终置顶
                           Qt::WindowDoesNotAcceptFocus;  // 不接受焦点（关键：让键盘穿透）
                           // Qt::Tool;                   // 工具窗口（临时移除，测试显示问题）
    
    setWindowFlags(flags);
    
    // 2. 设置窗口属性（跨平台）
    setAttribute(Qt::WA_TranslucentBackground, true);     // 透明背景
    setAttribute(Qt::WA_ShowWithoutActivating, true);     // 显示但不激活
    setAttribute(Qt::WA_NoSystemBackground, true);        // 无系统背景
    
    // 不获取焦点，让键盘输入穿透到下方应用
    setFocusPolicy(Qt::NoFocus);
    
    // 关键：窗口本身不完全穿透鼠标事件，因为退出按钮需要接收鼠标事件
    // 但通过事件过滤器实现选择性穿透
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    // 注意：事件过滤器在setupUI()之后安装，因为需要m_quitButton已初始化
    
    // 3. 隐藏状态栏（避免占用空间导致偏移）
    statusBar()->hide();
    
    // 4. 外接拼接墙：合并除最左屏外的所有显示器（勿用 primaryScreen 铺满主屏，会盖住 main.cpp 的 setGeometry）
    m_targetWallGeometry = computeRightWallGeometry();
    setGeometry(m_targetWallGeometry);
    
    // 4. 样式表确保透明
    setStyleSheet("QMainWindow { background: transparent; }");
    
#ifdef Q_OS_MACOS
    // macOS特定：使用NSWindow API实现真正的键盘穿透
    // 延迟执行，确保窗口句柄已创建
    QTimer::singleShot(100, this, [this]() {
        WId winId = this->winId();
        if (winId) {
            void* nsView = reinterpret_cast<void*>(winId);
            
            // 1. 获取NSWindow
            SEL windowSel = sel_registerName("window");
            typedef void* (*GetWindowFunc)(void*, SEL);
            void* nsWindow = ((GetWindowFunc)objc_msgSend)(nsView, windowSel);
            
            if (nsWindow) {
                qDebug() << "macOS API setup: nsWindow obtained";
                
                // 2. 注意：不完全忽略鼠标事件，因为退出按钮需要接收鼠标事件
                // 改为在需要穿透的区域通过事件过滤器实现
                // 不设置 setIgnoresMouseEvents，让窗口可以接收鼠标事件
                
                // 3. 注意：QNSWindow 不支持 setCanBecomeKey: 和 setCanBecomeMain:
                // 这些方法在 Qt 的窗口类中不存在，跳过这些调用
                
                // 4. 关键：尝试设置窗口不接受键盘事件
                // 使用 respondsToSelector 检查方法是否存在
                SEL respondsToSel = sel_registerName("respondsToSelector:");
                typedef bool (*RespondsToSelectorFunc)(void*, SEL, SEL);
                
                // 尝试设置窗口不能成为key window（不接受键盘焦点）
                SEL setCanBecomeKeySel = sel_registerName("setCanBecomeKey:");
                if (respondsToSel && setCanBecomeKeySel) {
                    bool responds = ((RespondsToSelectorFunc)objc_msgSend)(nsWindow, respondsToSel, setCanBecomeKeySel);
                    if (responds) {
                        typedef void (*SetCanBecomeKeyFunc)(void*, SEL, bool);
                        ((SetCanBecomeKeyFunc)objc_msgSend)(nsWindow, setCanBecomeKeySel, false);
                        qDebug() << "macOS API setup: setCanBecomeKey(false) called - window cannot become key window";
                    } else {
                        qDebug() << "macOS API setup: setCanBecomeKey: not supported by QNSWindow";
                    }
                }
                
                // 尝试设置窗口不能成为main window
                SEL setCanBecomeMainSel = sel_registerName("setCanBecomeMain:");
                if (respondsToSel && setCanBecomeMainSel) {
                    bool responds = ((RespondsToSelectorFunc)objc_msgSend)(nsWindow, respondsToSel, setCanBecomeMainSel);
                    if (responds) {
                        typedef void (*SetCanBecomeMainFunc)(void*, SEL, bool);
                        ((SetCanBecomeMainFunc)objc_msgSend)(nsWindow, setCanBecomeMainSel, false);
                        qDebug() << "macOS API setup: setCanBecomeMain(false) called - window cannot become main window";
                    } else {
                        qDebug() << "macOS API setup: setCanBecomeMain: not supported by QNSWindow";
                    }
                }
                
                // 5. 确保窗口可见并置顶
                SEL orderFrontSel = sel_registerName("orderFront:");
                if (orderFrontSel) {
                    typedef void (*OrderFrontFunc)(void*, SEL, void*);
                    ((OrderFrontFunc)objc_msgSend)(nsWindow, orderFrontSel, nullptr);
                    qDebug() << "macOS API setup: orderFront called";
                }
                
                // 6. 确保窗口不被隐藏
                SEL setHidesOnDeactivateSel = sel_registerName("setHidesOnDeactivate:");
                if (setHidesOnDeactivateSel) {
                    typedef void (*SetHidesOnDeactivateFunc)(void*, SEL, bool);
                    ((SetHidesOnDeactivateFunc)objc_msgSend)(nsWindow, setHidesOnDeactivateSel, false);
                    qDebug() << "macOS API setup: setHidesOnDeactivate(false) called";
                }
                
                // 7. 设置窗口级别为最高（ScreenSaver级别）
                SEL setLevelSel = sel_registerName("setLevel:");
                if (setLevelSel) {
                    typedef void (*SetLevelFunc)(void*, SEL, long);
                    ((SetLevelFunc)objc_msgSend)(nsWindow, setLevelSel, 2002);
                    qDebug() << "macOS API setup: setLevel(2002) called";
                }
                
                // 8. 设置窗口集合行为（在所有Spaces显示）
                SEL setCollectionBehaviorSel = sel_registerName("setCollectionBehavior:");
                if (setCollectionBehaviorSel) {
                    typedef void (*SetCollectionBehaviorFunc)(void*, SEL, unsigned long);
                    ((SetCollectionBehaviorFunc)objc_msgSend)(nsWindow, setCollectionBehaviorSel, 17);
                    qDebug() << "macOS API setup: setCollectionBehavior(17) called";
                }
            } else {
                qDebug() << "macOS API setup: Failed to get nsWindow";
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
// 置顶维护
// ==========================================
void MainWindow::ensureOnTop() {
    // 确保窗口显示
    if (!isVisible()) {
        qDebug() << "ensureOnTop: Window not visible, calling show()";
        show();
    }
    
    // 关键：不使用raise()，因为它会改变焦点
    // 使用macOS API的orderFront:来保持窗口在最前面，但不改变焦点
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
#else
    // 非macOS平台，使用raise()（但应该避免）
    // raise();
#endif
    
    // 确保窗口标志正确
    Qt::WindowFlags flags = windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        qDebug() << "ensureOnTop: Adding WindowStaysOnTopHint";
        flags |= Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
        applyWallGeometry(); // Windows 上 setWindowFlags 会重建窗口，几何会丢
    }
    
    // 只在窗口状态变化时输出调试信息（减少日志噪音）
    // qDebug() << "ensureOnTop: Window flags =" << flags << ", isVisible() =" << isVisible();
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
    centralWidget->setFocusPolicy(Qt::NoFocus);  // 不获取焦点
    centralWidget->setStyleSheet("background: transparent;");
    setCentralWidget(centralWidget);
    
    // 为centralWidget安装事件过滤器，实现选择性鼠标穿透
    // 退出按钮区域不穿透，其他区域穿透
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
    statusLayout->setContentsMargins(20, 20, 20, 20);  //ƒ少顶部边距，避免偏移

    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(m_displayWidget, 1);
    
    // 左下角退出按钮
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(20, 0, 20, 20);
    
    m_quitButton = new QPushButton("Quit", this);
    m_quitButton->setFocusPolicy(Qt::StrongFocus);  // 按钮可以接收焦点和鼠标事件
    m_quitButton->setAttribute(Qt::WA_TransparentForMouseEvents, false);  // 按钮区域不穿透鼠标
    m_quitButton->setMinimumSize(120, 40);
    m_quitButton->setMaximumSize(120, 40);
    
    // 按钮样式：半透明背景，红色文字
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

    
    // 在UI设置完成后，安装主窗口事件过滤器（此时m_quitButton已初始化）
    // 主窗口事件过滤器处理键盘事件
    installEventFilter(this);
}

// ==========================================
// 定时
// ==========================================
void MainWindow::onKeepOnTop() {
    // 强制保持窗口显示和置顶，但不改变焦点
    if (!isVisible()) {
        show();
    }
    // 关键：不使用raise()，因为它会改变焦点
    // 只使用macOS API的orderFront:来保持窗口在最前面，但不改变焦点
    ensureOnTop();
    
    // 额外确保：如果窗口被隐藏，立即重新显示
    QTimer::singleShot(50, this, [this]() {
        if (!isVisible()) {
            show();
            // 不使用raise()，避免改变焦点
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
    // 确保窗口显示并置顶，但不改变焦点
    ensureOnTop();
    // 关键：不使用raise()，避免改变焦点
    
    // 延迟再次确保窗口显示（防止被立即隐藏）
    QTimer::singleShot(50, this, [this]() {
        if (!isVisible()) {
            show();
            // 不使用raise()，避免改变焦点
        }
        ensureOnTop();
    });
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // 允许程序退出
    QMainWindow::closeEvent(event);
}

void MainWindow::requestQuit() {
    // 请求退出程序
    QApplication::quit();
}

void MainWindow::hideEvent(QHideEvent *event) {
    // 防止窗口被隐藏，立即重新显示
    QTimer::singleShot(10, this, [this]() {
        if (!isVisible()) {
            show();
            // 关键：不使用raise()，避免改变焦点
            ensureOnTop();
            // 关键：不调用activateWindow()，避免窗口获得焦点
        }
    });
    QMainWindow::hideEvent(event);
}

void MainWindow::changeEvent(QEvent *event) {
    // 处理窗口状态变化，防止窗口被激活
    if (event->type() == QEvent::WindowStateChange || 
        event->type() == QEvent::ActivationChange) {
        qDebug() << "changeEvent: Window state changed, isActiveWindow() =" << isActiveWindow();
        
        // 如果窗口被激活，立即取消激活并切换焦点回之前的窗口
        if (isActiveWindow()) {
            qDebug() << "changeEvent: Window became active, preventing activation and switching focus back";
            
#ifdef Q_OS_MACOS
            // 使用macOS API将焦点切换回之前的应用程序
            WId winId = this->winId();
            if (winId) {
                void* nsView = reinterpret_cast<void*>(winId);
                SEL windowSel = sel_registerName("window");
                typedef void* (*GetWindowFunc)(void*, SEL);
                void* nsWindow = ((GetWindowFunc)objc_msgSend)(nsView, windowSel);
                if (nsWindow) {
                    // 尝试调用resignKeyWindow来放弃键盘焦点
                    SEL resignKeyWindowSel = sel_registerName("resignKeyWindow");
                    if (resignKeyWindowSel) {
                        typedef void (*ResignKeyWindowFunc)(void*, SEL);
                        ((ResignKeyWindowFunc)objc_msgSend)(nsWindow, resignKeyWindowSel);
                        qDebug() << "changeEvent: Called resignKeyWindow";
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
    // 完全忽略所有键盘事件，让它们穿透到下方应用
    // 关键：对于所有键盘事件，我们都不处理，让它们穿透
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        // 完全不处理键盘事件，让它们穿透到下层窗口
        // 注意：退出功能通过左下角的退出按钮实现
        // 返回false表示不处理事件，让事件继续传播（穿透）
        return false;
    }
    
    // 对于ShortcutOverride事件，也忽略，让快捷键穿透
    if (event->type() == QEvent::ShortcutOverride) {
        return false;  // 不拦截快捷键，让它们穿透
    }
    
    // 处理鼠标事件：实现选择性穿透（仅对centralWidget）
    // 如果鼠标在退出按钮上，不穿透；否则穿透
    // 关键：确保鼠标点击不会让窗口获得焦点
    QWidget *cw = centralWidget();
    if (obj == cw && cw && 
        (event->type() == QEvent::MouseButtonPress || 
         event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonDblClick)) {
        
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        
        // 检查鼠标是否在退出按钮上（确保按钮已初始化）
        if (m_quitButton && m_quitButton->isVisible() && m_quitButton->parentWidget()) {
            QPoint widgetPos = mouseEvent->pos();  // 相对于centralWidget的位置
            QPoint buttonPos = m_quitButton->pos();  // 按钮在centralWidget中的位置
            QRect buttonRect(buttonPos, m_quitButton->size());
            
            if (buttonRect.contains(widgetPos)) {
                // 鼠标在按钮上，不穿透，让按钮处理
                // 将事件转发给按钮（转换为按钮坐标系）
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
                
                // 关键：确保窗口不会因为按钮点击而获得焦点
                // 不调用activateWindow()，让焦点保持在之前的窗口
                return true;  // 已处理，阻止穿透
            }
        }
        
        // 鼠标不在按钮上，穿透到下层窗口
        // 通过忽略事件来实现穿透效果
        // 关键：确保不会因为鼠标点击而获得焦点
        return true;  // 忽略事件，实现穿透
    }
    
    // 处理窗口激活事件：防止窗口获得焦点
    if (event->type() == QEvent::WindowActivate) {
        qDebug() << "WindowActivate event: Window activated, but should not accept focus";
        // 不处理激活事件，让焦点保持在之前的窗口
        return false;  // 让事件继续传播，但我们不处理它
    }
    
    // 其他事件正常处理
    return QMainWindow::eventFilter(obj, event);
}

// 重写event()方法，在事件分发前拦截键盘事件
bool MainWindow::event(QEvent *event) {
    // 关键：在事件分发到子控件之前，完全忽略所有键盘相关事件
    // 这样即使子控件设置了焦点，也不会收到键盘事件
    if (event->type() == QEvent::KeyPress || 
        event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::ShortcutOverride ||
        event->type() == QEvent::Shortcut) {
        // 完全忽略键盘事件，不处理，不传播
        // 这样事件就不会到达任何子控件，也不会被Qt处理
        qDebug() << "MainWindow::event: Keyboard event intercepted at window level (type:" << event->type() << "), ignoring";
        return false;  // 返回false表示事件未被处理，让系统继续分发（穿透）
    }
    
    // 其他事件正常处理
    return QMainWindow::event(event);
}



