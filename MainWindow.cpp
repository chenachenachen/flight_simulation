#include "MainWindow.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QScreen>
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_aircraftManager = new AircraftManager(this);
    m_xplaneReceiver  = new XPlaneReceiver(m_aircraftManager, this);
    m_blueSkyComm     = new BlueSkyCommunicator(m_aircraftManager, this);
    m_networkReceiver = new NetworkReceiver(m_aircraftManager, this);
    m_displayWidget   = new TrafficDisplayWidget(m_aircraftManager, this);
    m_quitButton      = nullptr;

    setupOverlayWindow();
    setupUI();
    
    connect(m_xplaneReceiver, &XPlaneReceiver::ownshipDataReceived, this, &MainWindow::onXPlaneDataReceived);

    m_xplaneReceiver->startListening(49009);
    m_blueSkyComm->startListening(49004);

    m_keepOnTopTimer = new QTimer(this);
    connect(m_keepOnTopTimer, &QTimer::timeout, this, &MainWindow::ensureOnTop);
    m_keepOnTopTimer->start(500);

    ensureOnTop();
}

MainWindow::~MainWindow() {}

void MainWindow::setupOverlayWindow() {
    // 绝对置顶 + 鼠标键盘彻底穿透
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::Tool;
    setWindowFlags(flags);
    
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true); // 核心：鼠标穿透
    
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("QMainWindow { background: transparent; }");

    // 自动抓取并合并投影仪屏幕
    const QList<QScreen *> screens = QGuiApplication::screens();
    QRect unitedRect;
    if (screens.size() > 1) {
        for (int i = 1; i < screens.size(); ++i) { // i=1 跳过主控屏
            unitedRect = unitedRect.united(screens[i]->geometry());
        }
        setGeometry(unitedRect);
    } else {
        setGeometry(screens.first()->geometry());
    }
}

void MainWindow::ensureOnTop() {
    if (!isVisible()) show();
    Qt::WindowFlags flags = windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        flags |= Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
    }
}

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setAttribute(Qt::WA_TranslucentBackground, true);
    centralWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true); // 允许穿透
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 状态栏
    QHBoxLayout *statusLayout = new QHBoxLayout();
    QLabel *xplaneStatus   = new QLabel("XP: --", this);
    QString style = "QLabel { background-color: rgba(0,0,0,150); color: white; padding: 4px; border-radius: 4px; font-weight: bold;}";
    xplaneStatus->setStyleSheet(style);
    statusLayout->addWidget(xplaneStatus);
    statusLayout->addStretch();
    statusLayout->setContentsMargins(20, 20, 20, 20);

    mainLayout->addLayout(statusLayout);
    m_displayWidget->show(); 
    mainLayout->addWidget(m_displayWidget, 1);
    
    // 退出按钮 (极其特殊：单独设置不穿透，否则点不到)
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(20, 0, 20, 20);
    m_quitButton = new QPushButton("Quit", this);
    m_quitButton->setAttribute(Qt::WA_TransparentForMouseEvents, false); 
    m_quitButton->setMinimumSize(120, 40);
    m_quitButton->setStyleSheet("QPushButton { background-color: rgba(0, 0, 0, 180); color: #ff4444; border: 2px solid #ff4444; border-radius: 6px; font-weight: bold; }");
    connect(m_quitButton, &QPushButton::clicked, qApp, &QApplication::quit);
    bottomLayout->addWidget(m_quitButton);
    bottomLayout->addStretch();
    mainLayout->addLayout(bottomLayout);

    connect(m_xplaneReceiver, &XPlaneReceiver::connectionStatusChanged, [xplaneStatus](bool ok) { xplaneStatus->setText(ok ? "XP: OK" : "XP: --"); });
}

void MainWindow::onXPlaneDataReceived() {
    AircraftData *ownship = m_aircraftManager->getOwnship();
    m_blueSkyComm->sendOwnshipPosition(ownship->latitude, ownship->longitude, ownship->altitude, ownship->heading, ownship->speed);
}
// 忽略其他系统事件，防止弹窗夺取焦点
void MainWindow::showEvent(QShowEvent *e) { QMainWindow::showEvent(e); ensureOnTop(); }
void MainWindow::closeEvent(QCloseEvent *e) { QMainWindow::closeEvent(e); }
void MainWindow::hideEvent(QHideEvent *e) { QMainWindow::hideEvent(e); }



