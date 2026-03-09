#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {

    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    #endif

    QApplication app(argc, argv);
    
    MainWindow window;

    // 1. Set starting coordinates: Skip the leftmost main screen (width 1920)
    // Start from the beginning of the three rightmost screens.
    int startX = 1920;
    int startY = 0;

    // 2. Set the giant window size: Three 1920x1080 screens combined
    int totalWidth = 1920 * 3;
    int totalHeight = 1080;
    
    // 3. Pack these data into a rectangular geometry
    QRect multiScreenGeometry(startX, startY, totalWidth, totalHeight);
    
    // 4. Force the window size and position to be specified
    // window.setGeometry(multiScreenGeometry);
    
    window.show();
    
    return app.exec();
}

