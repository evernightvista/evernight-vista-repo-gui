#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QMap>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QProgressBar>
#include <QListWidget>
#include <QTextEdit>
#include <QProcess>
#include <QPushButton>
#include "RepoManager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onApply();
    void onRefreshCache();
    void onRestoreDefaults();
    void startSpeedTest(const QString &repoId, const QList<Mirror> &mirrors);

    void onTestProgress(int current, int total);
    void onTestFinished(const QList<QPair<QString, int>> &results);

    // 维护功能槽
    void onRepairRpmDB();
    void onAutoRemove();
    void onDistroSync();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessReadyRead();

private:
    void setupUI();
    QWidget* createSectionPage(const QString &sectionName, const QList<Repository> &repos);
    QWidget* createMaintenancePage();

    // 通过 pkexec 调用特权助手，由 polkit 弹出认证窗口。sub 为助手子命令，
    // extra 为附加参数（如 apply 的暂存目录），tag 用于区分完成回调的处理。
    void runHelper(const QString &sub, const QString &extra,
                   const QString &statusMessage, const QString &tag, bool switchTab);
    void setActionButtonsEnabled(bool enabled);

    RepoManager *m_manager;
    QTabWidget *m_tabWidget;
    QMap<QString, QPair<QCheckBox*, QComboBox*>> m_repoControls;
    QMap<QString, Repository> m_repoMap;

    QDialog *m_speedDialog = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QListWidget *m_resultList = nullptr;

    // 顶部操作按钮
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_restoreBtn = nullptr;

    // 维护页面控件
    QWidget *m_maintenancePage = nullptr;
    QPushButton *m_repairBtn = nullptr;
    QPushButton *m_autoremoveBtn = nullptr;
    QPushButton *m_distroSyncBtn = nullptr;
    QTextEdit *m_outputEdit = nullptr;
    QProcess *m_process = nullptr;

    // 当前正在运行的操作标记，用于 onProcessFinished 区分处理
    QString m_currentAction;

    int getDefaultMirrorIndex(const QString &repoId, const QList<Mirror> &mirrors) const;
};
