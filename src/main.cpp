#include "gui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

namespace {

void applyStyleSheet(QApplication& application)
{
    QFile styleFile(QStringLiteral(":/styles/application.qss"));
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&styleFile);
    application.setStyleSheet(stream.readAll());
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("HHKBS"));
    application.setApplicationDisplayName(QStringLiteral("HHKBS"));
    application.setOrganizationName(QStringLiteral("HHKBS"));

    applyStyleSheet(application);

    MainWindow window;
    window.show();

    return application.exec();
}
