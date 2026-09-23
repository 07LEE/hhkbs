#include "gui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTimer>

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

    const auto demoMode = application.arguments().contains(QStringLiteral("--demo"));
    MainWindow window(demoMode);
    window.show();

    const auto screenshotPath = qEnvironmentVariable("HHKBS_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        bool delayIsValid = false;
        const auto configuredDelay =
            qEnvironmentVariableIntValue("HHKBS_SCREENSHOT_DELAY_MS", &delayIsValid);
        const auto screenshotDelay = delayIsValid ? configuredDelay : 250;
        QTimer::singleShot(screenshotDelay, &application, [&application, &window, screenshotPath] {
            window.grab().save(screenshotPath);
            application.quit();
        });
    }

    return application.exec();
}
