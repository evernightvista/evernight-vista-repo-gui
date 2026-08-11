#include <QApplication>
#include <KLocalizedString>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("evernight-vista-repo-gui");
    app.setApplicationName("evernight-vista-repo-gui");
    app.setOrganizationDomain("evernight-vista");
    
    QIcon icon = QIcon::fromTheme("gpk-repo");
    if (icon.isNull()) {
        icon = QIcon::fromTheme("system-software-install");
    }
    app.setWindowIcon(icon);

    MainWindow w;
    w.show();
    return app.exec();
}