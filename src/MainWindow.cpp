#include "MainWindow.h"
#include "MirrorTester.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QStatusBar>
#include <KLocalizedString>
#include <KMessageBox>
#include <KGuiItem>
#include <QApplication>
#include <QProcess>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QFont>

// 特权助手路径。GUI 以普通用户身份运行，所有需要 root 的操作都通过
// pkexec 调用此助手完成；polkit 会依据 .policy 文件弹出认证窗口。
static const char * const kHelperPath = "/usr/libexec/evernight-vista-repo-helper";

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_manager = new RepoManager(this);
    setupUI();

    QMap<QString, bool> states = RepoManager::loadEnabledStates();
    for (auto it = m_repoControls.begin(); it != m_repoControls.end(); ++it) {
        QString id = it.key();
        if (states.contains(id)) {
            it.value().first->setChecked(states[id]);
        }
    }
}

void MainWindow::setupUI() {
    setWindowTitle(i18n("Evernight Vista Repository Manager"));
    resize(1100, 750);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    QLabel *title = new QLabel(i18n("Evernight Vista Repository Manager"));
    title->setStyleSheet("font-size: 18pt; font-weight: bold;");
    mainLayout->addWidget(title);

    QLabel *info = new QLabel(i18n("Each repository can independently select a mirror source. Click 'Apply' to take effect. Click 'Test Speed' to check the response time of each mirror."));
    info->setStyleSheet("color: gray;");
    mainLayout->addWidget(info);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    auto sections = RepoManager::getRepoSections();
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        QString sectionName;
        if (it.key() == "Evernight Vista")
            sectionName = i18n("Evernight Vista");
        else if (it.key() == "RPM Fusion")
            sectionName = i18n("RPM Fusion");
        else if (it.key() == "Terra")
            sectionName = i18n("Terra");
        else
            sectionName = it.key();
        QWidget *page = createSectionPage(sectionName, it.value());
        m_tabWidget->addTab(page, sectionName);
    }

    // 添加维护标签页
    m_maintenancePage = createMaintenancePage();
    m_tabWidget->addTab(m_maintenancePage, i18n("Maintenance"));

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_applyBtn = new QPushButton(i18n("Apply Changes"));
    connect(m_applyBtn, &QPushButton::clicked, this, &MainWindow::onApply);
    btnLayout->addWidget(m_applyBtn);

    m_refreshBtn = new QPushButton(i18n("Refresh Cache"));
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshCache);
    btnLayout->addWidget(m_refreshBtn);

    m_restoreBtn = new QPushButton(i18n("Restore Defaults"));
    connect(m_restoreBtn, &QPushButton::clicked, this, &MainWindow::onRestoreDefaults);
    btnLayout->addWidget(m_restoreBtn);

    mainLayout->addLayout(btnLayout);

    statusBar()->showMessage(i18n("Ready"));
}

int MainWindow::getDefaultMirrorIndex(const QString &repoId, const QList<Mirror> &mirrors) const {
    int idx = 0;
    if (repoId == "evernight-vista" || repoId == "updates") {
        idx = 5; // 吉林大学
    } else if (repoId.startsWith("rpmfusion")) {
        idx = 3; // 南京大学
    } else if (repoId.startsWith("terra")) {
        idx = 1; // Freedif (德国)
    }
    if (idx >= mirrors.size()) idx = 0;
    return idx;
}

QWidget* MainWindow::createSectionPage(const QString &sectionName, const QList<Repository> &repos) {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QLabel *desc = new QLabel(i18n("Software repositories for %1", sectionName));
    desc->setStyleSheet("color: gray;");
    layout->addWidget(desc);

    for (const Repository &repo : repos) {
        QGroupBox *groupBox = new QGroupBox(repo.id);
        QVBoxLayout *groupLayout = new QVBoxLayout(groupBox);

        QHBoxLayout *rowLayout = new QHBoxLayout;
        QCheckBox *cb = new QCheckBox(i18n("Enable"));
        cb->setChecked(repo.enabled);
        rowLayout->addWidget(cb);

        QComboBox *combo = new QComboBox;
        for (const Mirror &mirror : repo.mirrors) {
            combo->addItem(mirror.name);
        }
        int defaultIdx = getDefaultMirrorIndex(repo.id, repo.mirrors);
        if (defaultIdx < combo->count())
            combo->setCurrentIndex(defaultIdx);
        rowLayout->addWidget(combo);

        QPushButton *speedBtn = new QPushButton(i18n("Test Speed"));
        connect(speedBtn, &QPushButton::clicked, this, [this, repo]() {
            startSpeedTest(repo.id, repo.mirrors);
        });
        rowLayout->addWidget(speedBtn);

        rowLayout->addStretch();
        groupLayout->addLayout(rowLayout);

        QLabel *nameLabel = new QLabel(m_manager->expandVars(repo.name, repo.id));
        nameLabel->setStyleSheet("color: gray; font-size: 9pt;");
        groupLayout->addWidget(nameLabel);

        layout->addWidget(groupBox);

        m_repoControls[repo.id] = qMakePair(cb, combo);
        m_repoMap[repo.id] = repo;
    }

    layout->addStretch();
    return page;
}

void MainWindow::startSpeedTest(const QString &repoId, const QList<Mirror> &mirrors) {
    QList<QPair<QString, QString>> testUrls;
    for (const Mirror &mirror : mirrors) {
        if (mirror.baseurl.isEmpty())
            continue;
        QString url = m_manager->expandVars(mirror.baseurl, repoId);
        if (!url.endsWith('/')) url += '/';
        testUrls.append(qMakePair(mirror.name, url));
    }

    if (testUrls.isEmpty()) {
        KMessageBox::information(this, i18n("No testable mirrors (only baseurl supported)"), i18n("Info"));
        return;
    }

    m_speedDialog = new QDialog(this);
    m_speedDialog->setWindowTitle(i18n("Speed Testing - %1", repoId));
    m_speedDialog->resize(600, 400);

    QVBoxLayout *dlgLayout = new QVBoxLayout(m_speedDialog);
    m_progressBar = new QProgressBar(m_speedDialog);
    m_progressBar->setRange(0, testUrls.size());
    dlgLayout->addWidget(m_progressBar);

    m_resultList = new QListWidget(m_speedDialog);
    dlgLayout->addWidget(m_resultList);

    QPushButton *closeBtn = new QPushButton(i18n("Close"));
    connect(closeBtn, &QPushButton::clicked, m_speedDialog, &QDialog::accept);
    dlgLayout->addWidget(closeBtn);

    m_speedDialog->show();

    MirrorTester *tester = new MirrorTester(this);
    connect(tester, &MirrorTester::mirrorTestProgress, this, &MainWindow::onTestProgress);
    connect(tester, &MirrorTester::mirrorTestFinished, this, &MainWindow::onTestFinished);
    connect(tester, &MirrorTester::mirrorTestFinished, tester, &MirrorTester::deleteLater);
    tester->testMirrors(testUrls);
}

void MainWindow::onTestProgress(int current, int total) {
    if (m_progressBar) {
        m_progressBar->setValue(current);
        m_progressBar->setFormat(i18n("%v / %m"));
    }
}

void MainWindow::onTestFinished(const QList<QPair<QString, int>> &results) {
    if (!m_resultList) return;
    m_resultList->clear();
    for (const auto &result : results) {
        QString text;
        if (result.second < 0) {
            if (result.second == -403)
                text = i18n("%1: 403 (Access Denied)", result.first);
            else
                text = i18n("%1: Failed", result.first);
        } else {
            text = i18n("%1: %2 ms", result.first, result.second);
        }
        m_resultList->addItem(text);
    }
}

// ------------------- 仓库配置 -------------------

void MainWindow::onApply() {
    QMap<QString, QPair<Mirror, bool>> states;
    for (auto it = m_repoControls.begin(); it != m_repoControls.end(); ++it) {
        QString id = it.key();
        QCheckBox *cb = it.value().first;
        QComboBox *combo = it.value().second;
        const Repository &repo = m_repoMap[id];
        if (!repo.mirrors.isEmpty()) {
            int idx = combo->currentIndex();
            Mirror mirror = repo.mirrors[idx];
            states[id] = qMakePair(mirror, cb->isChecked());
        }
    }

    if (states.isEmpty()) {
        KMessageBox::information(this, i18n("No repositories configured"), i18n("Info"));
        return;
    }

    // 以普通用户身份把期望的 .repo 文件写入暂存目录，再交给 root 助手安装。
    QString stageDir = QString("%1/evr-stage-%2")
                           .arg(QDir::tempPath())
                           .arg(QDateTime::currentMSecsSinceEpoch());
    if (!m_manager->stageConfig(states, stageDir)) {
        QDir(stageDir).removeRecursively();
        KMessageBox::error(this, i18n("Failed to prepare repository configuration."), i18n("Error"));
        return;
    }

    runHelper("apply", stageDir, i18n("Applying configuration..."), QStringLiteral("apply"), false);
}

void MainWindow::onRefreshCache() {
    int ret = KMessageBox::questionTwoActions(this,
                                                 i18n("This will execute 'dnf makecache'. Continue?"),
                                                 i18n("Confirm"),
                                                 KGuiItem(i18n("Continue")),
                                                 KGuiItem(i18n("Cancel")));
    if (ret != KMessageBox::PrimaryAction) return;

    runHelper("refresh-cache", QString(), i18n("Refreshing cache..."), QStringLiteral("refresh-cache"), true);
}

void MainWindow::onRestoreDefaults() {
    int ret = KMessageBox::questionTwoActions(this,
                 i18n("This will restore all repositories to default configuration. Continue?"),
                 i18n("Confirm"),
                 KGuiItem(i18n("Continue")),
                 KGuiItem(i18n("Cancel")));
    if (ret != KMessageBox::PrimaryAction) return;

    // 计算默认状态并暂存，复用 apply 动作由 root 助手安装。
    QMap<QString, QPair<Mirror, bool>> states;
    for (auto it = m_repoMap.begin(); it != m_repoMap.end(); ++it) {
        const QString &repoId = it.key();
        const Repository &repo = it.value();
        if (repo.mirrors.isEmpty()) continue;
        int defaultIndex = getDefaultMirrorIndex(repoId, repo.mirrors);
        states[repoId] = qMakePair(repo.mirrors[defaultIndex], repo.enabled);
    }

    if (states.isEmpty()) {
        KMessageBox::information(this, i18n("No repositories configured"), i18n("Info"));
        return;
    }

    QString stageDir = QString("%1/evr-stage-%2")
                           .arg(QDir::tempPath())
                           .arg(QDateTime::currentMSecsSinceEpoch());
    if (!m_manager->stageConfig(states, stageDir)) {
        QDir(stageDir).removeRecursively();
        KMessageBox::error(this, i18n("Failed to prepare repository configuration."), i18n("Error"));
        return;
    }

    runHelper("apply", stageDir, i18n("Restoring default configuration..."), QStringLiteral("restore-defaults"), false);
}

// ------------------- 维护功能实现 -------------------

QWidget* MainWindow::createMaintenancePage() {
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QLabel *info = new QLabel(i18n("Perform system maintenance tasks for DNF/RPM. Each action asks for administrator authentication through polkit."));
    info->setStyleSheet("color: gray;");
    info->setWordWrap(true);
    layout->addWidget(info);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_repairBtn = new QPushButton(i18n("Repair RPM Database"));
    connect(m_repairBtn, &QPushButton::clicked, this, &MainWindow::onRepairRpmDB);
    btnLayout->addWidget(m_repairBtn);

    m_autoremoveBtn = new QPushButton(i18n("Clean Unused Packages (autoremove)"));
    connect(m_autoremoveBtn, &QPushButton::clicked, this, &MainWindow::onAutoRemove);
    btnLayout->addWidget(m_autoremoveBtn);

    m_distroSyncBtn = new QPushButton(i18n("Synchronize Distribution (distro-sync)"));
    connect(m_distroSyncBtn, &QPushButton::clicked, this, &MainWindow::onDistroSync);
    btnLayout->addWidget(m_distroSyncBtn);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_outputEdit = new QTextEdit(page);
    m_outputEdit->setReadOnly(true);
    m_outputEdit->setFont(QFont("Monospace", 10));
    layout->addWidget(m_outputEdit);

    // 初始化 QProcess
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::onProcessReadyRead);

    return page;
}

void MainWindow::runHelper(const QString &sub, const QString &extra,
                           const QString &statusMessage, const QString &tag, bool switchTab) {
    m_currentAction = tag;
    setActionButtonsEnabled(false);

    m_outputEdit->clear();
    if (switchTab && m_maintenancePage)
        m_tabWidget->setCurrentWidget(m_maintenancePage);

    statusBar()->showMessage(statusMessage);

    QStringList args;
    args << QString::fromLatin1(kHelperPath) << sub;
    if (!extra.isEmpty())
        args << extra;

    // pkexec 会按 .policy 中 exec.path + exec.argv1 匹配对应动作并弹出认证窗口；
    // 认证通过后以 root 身份执行助手。
    m_process->start("pkexec", args);
}

void MainWindow::setActionButtonsEnabled(bool enabled) {
    if (m_applyBtn) m_applyBtn->setEnabled(enabled);
    if (m_refreshBtn) m_refreshBtn->setEnabled(enabled);
    if (m_restoreBtn) m_restoreBtn->setEnabled(enabled);
    if (m_repairBtn) m_repairBtn->setEnabled(enabled);
    if (m_autoremoveBtn) m_autoremoveBtn->setEnabled(enabled);
    if (m_distroSyncBtn) m_distroSyncBtn->setEnabled(enabled);
}

void MainWindow::onRepairRpmDB() {
    int ret = KMessageBox::questionTwoActions(this,
                 i18n("This will rebuild the RPM database using 'rpm --rebuilddb'. Continue?"),
                 i18n("Confirm"),
                 KGuiItem(i18n("Continue")),
                 KGuiItem(i18n("Cancel")));
    if (ret != KMessageBox::PrimaryAction) return;

    runHelper("rebuilddb", QString(), i18n("Repairing RPM database..."), QStringLiteral("rebuilddb"), true);
}

void MainWindow::onAutoRemove() {
    int ret = KMessageBox::questionTwoActions(this,
                 i18n("This will remove unnecessary packages via 'dnf autoremove -y'. Continue?"),
                 i18n("Confirm"),
                 KGuiItem(i18n("Continue")),
                 KGuiItem(i18n("Cancel")));
    if (ret != KMessageBox::PrimaryAction) return;

    runHelper("autoremove", QString(), i18n("Cleaning unused packages..."), QStringLiteral("autoremove"), true);
}

void MainWindow::onDistroSync() {
    int ret = KMessageBox::questionTwoActions(this,
                 i18n("This will synchronize your system with the repository via 'dnf distro-sync -y'. Continue?"),
                 i18n("Confirm"),
                 KGuiItem(i18n("Continue")),
                 KGuiItem(i18n("Cancel")));
    if (ret != KMessageBox::PrimaryAction) return;

    runHelper("distro-sync", QString(), i18n("Synchronizing distribution..."), QStringLiteral("distro-sync"), true);
}

void MainWindow::onProcessReadyRead() {
    QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    if (!output.isEmpty()) {
        m_outputEdit->append(output);
    }
    QString error = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (!error.isEmpty()) {
        m_outputEdit->append(QString("<span style=\"color:#cc0000;\">%1</span>").arg(error.toHtmlEscaped()));
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    setActionButtonsEnabled(true);

    QString action = m_currentAction;
    m_currentAction.clear();

    QString resultMsg;
    bool success = false;

    // pkexec 在认证被取消时返回 126，未授权/助手缺失时返回 127。
    if (exitCode == 126) {
        resultMsg = i18n("Authentication was dismissed.");
        statusBar()->showMessage(i18n("Canceled"));
    } else if (exitCode == 127) {
        resultMsg = i18n("Not authorized, or the privileged helper was not found.");
        statusBar()->showMessage(i18n("Authorization failed"));
    } else {
        success = (exitStatus == QProcess::NormalExit && exitCode == 0);

        if (action == QLatin1String("apply") || action == QLatin1String("restore-defaults")) {
            if (success) {
                resultMsg = i18n("Repository configuration applied.");
                statusBar()->showMessage(i18n("Configuration applied"));
                // 恢复默认成功后，把界面控件同步到默认值。
                if (action == QLatin1String("restore-defaults")) {
                    for (auto it = m_repoMap.begin(); it != m_repoMap.end(); ++it) {
                        const QString &repoId = it.key();
                        const Repository &repo = it.value();
                        if (repo.mirrors.isEmpty()) continue;
                        int defaultIndex = getDefaultMirrorIndex(repoId, repo.mirrors);
                        if (m_repoControls.contains(repoId)) {
                            m_repoControls[repoId].first->setChecked(repo.enabled);
                            m_repoControls[repoId].second->setCurrentIndex(defaultIndex);
                        }
                    }
                }
            } else {
                resultMsg = i18n("Failed to apply configuration (exit code %1).", exitCode);
                statusBar()->showMessage(i18n("Failed to apply configuration"));
            }
        } else {
            // 维护类命令（rebuilddb / autoremove / distro-sync / refresh-cache）
            if (success) {
                resultMsg = i18n("Command completed successfully.");
                statusBar()->showMessage(i18n("Task finished"));
            } else {
                resultMsg = i18n("Command failed with exit code %1.", exitCode);
                statusBar()->showMessage(i18n("Task failed"));
            }
        }
    }

    m_outputEdit->append(QString("\n--- %1 ---").arg(resultMsg));

    if (success)
        KMessageBox::information(this, resultMsg, i18n("Success"));
    else
        KMessageBox::error(this, resultMsg, i18n("Error"));
}
