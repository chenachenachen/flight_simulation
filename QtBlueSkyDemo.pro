QT += core widgets network

CONFIG += c++11

# macOS特定配置
macx {
    LIBS += -framework Cocoa -framework Carbon
    
    # 使用自定义 Info.plist (关键：设置 LSUIElement)
    QMAKE_INFO_PLIST = Info.plist

    app_bundle.files = Info.plist
    app_bundle.path = Contents
}

TARGET = QtBlueSkyDemo
TEMPLATE = app

SOURCES += \
    main.cpp \
    AircraftManager.cpp \
    NetworkReceiver.cpp \
    XPlaneReceiver.cpp \
    BlueSkyCommunicator.cpp \
    TrafficDisplayWidget.cpp \
    MainWindow.cpp

HEADERS += \
    AircraftData.h \
    AircraftManager.h \
    NetworkReceiver.h \
    XPlaneReceiver.h \
    BlueSkyCommunicator.h \
    TrafficDisplayWidget.h \
    MainWindow.h

