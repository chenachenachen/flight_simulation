#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {

    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    #endif

    QApplication app(argc, argv);
    
    MainWindow window;
    // 外接三联屏几何由 MainWindow::setupOverlayWindow() 根据 QGuiApplication::screens()
    // 自动合并「除最左屏外」的所有显示器；勿在此处 setGeometry，否则仍会被 ensureOnTop 里
    // 可能的 setWindowFlags+show 在 Windows 上重置；逻辑已集中在 applyWallGeometry()。
    window.show();
    
    return app.exec();
}
