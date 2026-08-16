#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QPair>

struct Mirror {
    QString name;
    QString baseurl;
    QString metalink;
};

struct Repository {
    QString id;
    QString name;
    bool enabled = true;
    QString repoFile;
    QList<Mirror> mirrors;
};

class RepoManager : public QObject {
    Q_OBJECT
public:
    explicit RepoManager(QObject *parent = nullptr);

    QString releasever() const;
    QString basearch() const;
    QString expandVars(const QString &text, const QString &repoId) const;
    QString generateRepoContent(const Repository &repo, const Mirror &mirror) const;

    bool applyRepo(const Repository &repo, const Mirror &mirror, bool enabled, bool backup = true);
    bool applyAll(const QMap<QString, QPair<Mirror, bool>> &states);
    bool refreshCache();
    static QMap<QString, bool> loadEnabledStates();
    static QMap<QString, Mirror> loadMirrorStates();

    // Build the desired .repo files in a staging directory (as the normal user)
    // instead of writing the system repo directory directly. The root helper installs them.
    bool stageConfig(const QMap<QString, QPair<Mirror, bool>> &states, const QString &stageDir);

    static QMap<QString, QList<Repository>> getRepoSections();
    static QString repoDir();
    static QString backupDir();

private:
    QString m_releasever;
    QString m_basearch;
    void detectSystemInfo();
    void ensureBackupDir() const;

    bool modifyRepoFile(const QString &filePath, const Mirror &mirror, bool enabled, const QString &repoId);

    static const QString REPO_DIR;
    static const QString BACKUP_DIR;
};
