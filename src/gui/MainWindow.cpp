#include "gui/MainWindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildInterface();
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("HHKBS"));
    setMinimumSize(900, 560);
    resize(1120, 700);

    auto* centralWidget = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(centralWidget);
    pageLayout->setContentsMargins(32, 28, 32, 28);
    pageLayout->setSpacing(22);

    auto* headerLayout = new QHBoxLayout;
    auto* titleLayout = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("HHKBS"));
    title->setObjectName(QStringLiteral("title"));
    auto* subtitle = new QLabel(QStringLiteral("HHKB Studio keymap editor"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    auto* connectionStatus = new QLabel(QStringLiteral("No device connected"));
    connectionStatus->setObjectName(QStringLiteral("connectionStatus"));
    connectionStatus->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(connectionStatus);
    pageLayout->addLayout(headerLayout);

    auto* layerTabs = new QTabBar;
    layerTabs->setObjectName(QStringLiteral("layerTabs"));
    layerTabs->addTab(QStringLiteral("Base"));
    layerTabs->addTab(QStringLiteral("Fn1"));
    layerTabs->addTab(QStringLiteral("Fn2"));
    layerTabs->addTab(QStringLiteral("Fn3"));
    layerTabs->setExpanding(false);
    layerTabs->setEnabled(false);
    pageLayout->addWidget(layerTabs);

    auto* workspace = new QFrame;
    workspace->setObjectName(QStringLiteral("workspace"));
    workspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setAlignment(Qt::AlignCenter);

    auto* placeholderTitle = new QLabel(QStringLiteral("Connect an HHKB Studio"));
    placeholderTitle->setObjectName(QStringLiteral("placeholderTitle"));
    placeholderTitle->setAlignment(Qt::AlignCenter);
    auto* placeholderBody = new QLabel(
        QStringLiteral("Device discovery and profile reading will be added in phase 2."));
    placeholderBody->setObjectName(QStringLiteral("placeholderBody"));
    placeholderBody->setAlignment(Qt::AlignCenter);
    workspaceLayout->addWidget(placeholderTitle);
    workspaceLayout->addWidget(placeholderBody);
    pageLayout->addWidget(workspace, 1);

    auto* actionLayout = new QHBoxLayout;
    auto* refreshButton = new QPushButton(QStringLiteral("Read from keyboard"));
    auto* importButton = new QPushButton(QStringLiteral("Import"));
    auto* exportButton = new QPushButton(QStringLiteral("Export"));
    auto* resetButton = new QPushButton(QStringLiteral("Discard changes"));
    auto* applyButton = new QPushButton(QStringLiteral("Apply to keyboard"));
    applyButton->setObjectName(QStringLiteral("primaryButton"));

    for (auto* button : {refreshButton, importButton, exportButton, resetButton, applyButton}) {
        button->setEnabled(false);
    }

    actionLayout->addWidget(refreshButton);
    actionLayout->addWidget(importButton);
    actionLayout->addWidget(exportButton);
    actionLayout->addStretch();
    actionLayout->addWidget(resetButton);
    actionLayout->addWidget(applyButton);
    pageLayout->addLayout(actionLayout);

    setCentralWidget(centralWidget);
}
