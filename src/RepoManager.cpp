#include "RepoManager.h"
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDir>
#include <QSettings>
#include <QDebug>
#include <QRegularExpression>
#include <KLocalizedString>   // 新增

const QString RepoManager::REPO_DIR = "/etc/yum.repos.d/";
const QString RepoManager::BACKUP_DIR = "/etc/yum.repos.d/backup/";

QString RepoManager::repoDir() {
    return REPO_DIR;
}

QString RepoManager::backupDir() {
    return BACKUP_DIR;
}

RepoManager::RepoManager(QObject *parent) : QObject(parent) {
    detectSystemInfo();
    ensureBackupDir();
}

void RepoManager::detectSystemInfo() {
    QFile osRelease("/usr/lib/os-release");
    if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&osRelease);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("VERSION_ID=")) {
                QString value = line.section('=', 1).trimmed();
                if (value.startsWith('"') && value.endsWith('"'))
                    value = value.mid(1, value.length() - 2);
                if (!value.isEmpty()) {
                    m_releasever = value;
                    break;
                }
            }
        }
        osRelease.close();
    }

    if (m_releasever.isEmpty()) {
        QProcess proc;
        proc.start("rpm", {"-E", "%fedora"});
        proc.waitForFinished();
        m_releasever = QString(proc.readAllStandardOutput()).trimmed();
    }
    if (m_releasever.isEmpty()) m_releasever = "40";

    QProcess proc;
    proc.start("uname", {"-m"});
    proc.waitForFinished();
    QString arch = QString(proc.readAllStandardOutput()).trimmed();
    if (arch == "x86_64" || arch == "aarch64")
        m_basearch = arch;
    else
        m_basearch = "x86_64";
}

void RepoManager::ensureBackupDir() const {
    QDir dir;
    if (!dir.exists(BACKUP_DIR))
        dir.mkpath(BACKUP_DIR);
}

QString RepoManager::releasever() const { return m_releasever; }
QString RepoManager::basearch() const { return m_basearch; }

QString RepoManager::expandVars(const QString &text, const QString &repoId) const {
    QString result = text;
    result.replace("$releasever", m_releasever);
    result.replace("$basearch", m_basearch);
    result.replace("$repo_id", repoId);
    return result;
}

QString RepoManager::generateRepoContent(const Repository &repo, const Mirror &mirror) const {
    QStringList lines;
    lines << QString("[%1]").arg(repo.id);
    lines << "name=" + repo.name;
    lines << QString("enabled=%1").arg(repo.enabled ? "1" : "0");
    lines << "gpgcheck=1";
    lines << "gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-$releasever-$basearch";
    if (!mirror.baseurl.isEmpty())
        lines << "baseurl=" + mirror.baseurl;
    if (!mirror.metalink.isEmpty())
        lines << "metalink=" + mirror.metalink;
    lines << "";
    return lines.join("\n");
}

bool RepoManager::modifyRepoFile(const QString &filePath, const Mirror &mirror, bool enabled, const QString &repoId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd())
        lines << in.readLine();
    file.close();

    QString newEnabled = "enabled=" + QString(enabled ? "1" : "0");
    QString newBaseurl, newMetalink;
    if (!mirror.baseurl.isEmpty())
        newBaseurl = "baseurl=" + mirror.baseurl;
    if (!mirror.metalink.isEmpty())
        newMetalink = "metalink=" + mirror.metalink;

    int sectionStart = -1, sectionEnd = -1;
    bool foundTarget = false;
    int i = 0;
    while (i < lines.size()) {
        QString line = lines[i].trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            QString sec = line.mid(1, line.length() - 2);
            if (sec == repoId) {
                foundTarget = true;
                sectionStart = i;
                int j = i + 1;
                while (j < lines.size()) {
                    QString nextLine = lines[j].trimmed();
                    if (nextLine.startsWith('[') && nextLine.endsWith(']'))
                        break;
                    j++;
                }
                sectionEnd = j - 1;
                break;
            }
        }
        i++;
    }

    if (!foundTarget) {
        lines << QString("[%1]").arg(repoId);
        lines << newEnabled;
        if (!newBaseurl.isEmpty()) lines << newBaseurl;
        if (!newMetalink.isEmpty()) lines << newMetalink;
        lines << "";
    } else {
        bool enabledFound = false, baseurlFound = false, metalinkFound = false;
        for (int j = sectionStart + 1; j <= sectionEnd; ++j) {
            QString line = lines[j];
            if (line.trimmed().startsWith('#'))
                continue;
            QString trimmed = line.trimmed();
            if (trimmed.startsWith("enabled=")) {
                lines[j] = newEnabled;
                enabledFound = true;
            } else if (trimmed.startsWith("baseurl=") && !newBaseurl.isEmpty()) {
                lines[j] = newBaseurl;
                baseurlFound = true;
            } else if (trimmed.startsWith("metalink=") && !newMetalink.isEmpty()) {
                lines[j] = newMetalink;
                metalinkFound = true;
            }
        }
        int insertPos = sectionEnd + 1;
        if (!enabledFound) {
            lines.insert(insertPos, newEnabled);
            insertPos++;
        }
        if (!baseurlFound && !newBaseurl.isEmpty()) {
            lines.insert(insertPos, newBaseurl);
            insertPos++;
        }
        if (!metalinkFound && !newMetalink.isEmpty()) {
            lines.insert(insertPos, newMetalink);
            insertPos++;
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    for (const QString &line : lines)
        out << line << "\n";
    file.close();
    return true;
}

bool RepoManager::applyRepo(const Repository &repo, const Mirror &mirror, bool enabled, bool backup) {
    QString repoPath = REPO_DIR + repo.repoFile;
    if (backup && QFile::exists(repoPath)) {
        QString backupPath = BACKUP_DIR + repo.repoFile;
        QFile::copy(repoPath, backupPath);
    }

    if (QFile::exists(repoPath)) {
        return modifyRepoFile(repoPath, mirror, enabled, repo.id);
    } else {
        Repository r = repo;
        r.enabled = enabled;
        QString content = generateRepoContent(r, mirror);
        QFile file(repoPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&file);
        out << content;
        file.close();
        return true;
    }
}

bool RepoManager::applyAll(const QMap<QString, QPair<Mirror, bool>> &states) {
    bool allOk = true;
    auto sections = getRepoSections();
    QMap<QString, Repository> repoMap;
    for (const auto &list : sections) {
        for (const Repository &r : list) {
            repoMap[r.id] = r;
        }
    }
    for (auto it = states.begin(); it != states.end(); ++it) {
        QString id = it.key();
        if (repoMap.contains(id)) {
            const Repository &r = repoMap[id];
            if (!applyRepo(r, it.value().first, it.value().second))
                allOk = false;
        }
    }
    return allOk;
}

bool RepoManager::stageConfig(const QMap<QString, QPair<Mirror, bool>> &states, const QString &stageDir) {
    QDir().mkpath(stageDir);

    auto sections = getRepoSections();
    QMap<QString, Repository> repoMap;
    for (const auto &list : sections)
        for (const Repository &r : list)
            repoMap[r.id] = r;

    // Group the requested repos by their target .repo file, because several
    // repos (e.g. all Terra repos) may share one file.
    QMap<QString, QStringList> fileToIds;
    for (auto it = states.begin(); it != states.end(); ++it) {
        if (!repoMap.contains(it.key()))
            continue;
        fileToIds[repoMap[it.key()].repoFile].append(it.key());
    }

    bool allOk = true;
    for (auto fit = fileToIds.begin(); fit != fileToIds.end(); ++fit) {
        const QString &repoFile = fit.key();
        QString stagePath = stageDir + "/" + repoFile;
        QString realPath = REPO_DIR + repoFile;

        if (QFile::exists(realPath)) {
            // Seed the staging copy from the current (world-readable) repo file,
            // then edit each requested section in place.
            if (QFile::exists(stagePath))
                QFile::remove(stagePath);
            QFile::copy(realPath, stagePath);

            for (const QString &id : fit.value()) {
                const Repository &r = repoMap[id];
                if (!modifyRepoFile(stagePath, states[id].first, states[id].second, id))
                    allOk = false;
            }
        } else {
            // The file does not exist yet: generate full content for every
            // requested section in this file.
            QStringList parts;
            for (const QString &id : fit.value()) {
                Repository r = repoMap[id];
                r.enabled = states[id].second;
                parts << generateRepoContent(r, states[id].first);
            }
            QFile file(stagePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                allOk = false;
                continue;
            }
            QTextStream out(&file);
            out << parts.join("\n");
            file.close();
        }
    }
    return allOk;
}

bool RepoManager::refreshCache() {
    QProcess proc;
    proc.start("dnf", {"makecache"});
    proc.waitForFinished(-1);
    return proc.exitCode() == 0;
}

QMap<QString, bool> RepoManager::loadEnabledStates() {
    QMap<QString, bool> states;
    QDir dir(REPO_DIR);
    QStringList filters{"*.repo"};
    dir.setNameFilters(filters);
    for (const QString &fileName : dir.entryList(QDir::Files)) {
        QString filePath = dir.absolutePath() + "/" + fileName;
        QSettings settings(filePath, QSettings::IniFormat);
        for (const QString &groupId : settings.childGroups()) {
            settings.beginGroup(groupId);
            bool enabled = settings.value("enabled", true).toBool();
            states[groupId] = enabled;
            settings.endGroup();
        }
    }
    return states;
}

QMap<QString, QList<Repository>> RepoManager::getRepoSections() {
    QMap<QString, QList<Repository>> sections;

    auto fedoraMirrors = [](const QString &repoId) -> QList<Mirror> {
        QString subpath;
        if (repoId == "evernight-vista") {
            subpath = "releases/$releasever/Everything/$basearch/os/";
        } else if (repoId == "updates") {
            subpath = "updates/$releasever/Everything/$basearch/";
        } else {
            subpath = "releases/$releasever/Everything/$basearch/os/";
        }
        QList<Mirror> mirrors;
        mirrors << Mirror{i18n("Official (metalink)"), "", "https://mirrors.fedoraproject.org/metalink?repo=" + repoId + "&arch=$basearch"};
        mirrors << Mirror{i18n("Tsinghua (TUNA)"), "https://mirrors.tuna.tsinghua.edu.cn/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("University of Science and Technology of China (USTC)"), "https://mirrors.ustc.edu.cn/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Alibaba Cloud"), "https://mirrors.aliyun.com/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Nanjing University"), "https://mirrors.nju.edu.cn/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Jilin University"), "https://mirrors.jlu.edu.cn/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Beijing Foreign Studies University (BFSU)"), "https://mirrors.bfsu.edu.cn/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("JAIST (Japan)"), "http://ftp.jaist.ac.jp/pub/Linux/Fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Yamagata University (Japan)"), "http://ftp.yz.yamagata-u.ac.jp/pub/linux/fedora/linux/" + subpath, ""};
        mirrors << Mirror{i18n("Kernel.org (USA)"), "https://mirrors.kernel.org/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("Kakao (Korea)"), "https://mirror.kakao.com/fedora/" + subpath, ""};
        mirrors << Mirror{i18n("KAIST (Korea)"), "http://ftp.kaist.ac.kr/pub/fedora/linux/" + subpath, ""};
        return mirrors;
    };

    // Evernight Vista
    QList<Repository> evernightRepos;
    evernightRepos << Repository{"evernight-vista", i18n("Evernight Vista $releasever - $basearch"), true,
                                   "evernight-vista.repo", fedoraMirrors("evernight-vista")};
    evernightRepos << Repository{"updates", i18n("Evernight Vista $releasever - $basearch - Updates"), true,
                                   "evernight-vista-updates.repo", fedoraMirrors("updates")};
    sections["Evernight Vista"] = evernightRepos;

    // RPM Fusion
    auto rpmfusionMirrors = [](const QString &type, bool updates) -> QList<Mirror> {
        QString path;
        if (updates) {
            path = "rpmfusion/" + type + "/fedora/updates/$releasever/$basearch/";
        } else {
            path = "rpmfusion/" + type + "/fedora/releases/$releasever/Everything/$basearch/os/";
        }
        QList<Mirror> mirrors;
        QString repoName = (type == "free" ? "free" : "nonfree") + QString(updates ? "-updates" : "");
        mirrors << Mirror{i18n("Official metalink"), "", "https://mirrors.rpmfusion.org/metalink?repo=" + repoName + "-fedora-$releasever&arch=$basearch"};
        mirrors << Mirror{i18n("Tsinghua (TUNA)"), "https://mirrors.tuna.tsinghua.edu.cn/" + path, ""};
        mirrors << Mirror{i18n("University of Science and Technology of China (USTC)"), "https://mirrors.ustc.edu.cn/" + path, ""};
        mirrors << Mirror{i18n("Nanjing University"), "https://mirrors.nju.edu.cn/" + path, ""};
        mirrors << Mirror{i18n("Alibaba Cloud"), "https://mirrors.aliyun.com/" + path, ""};
        mirrors << Mirror{i18n("Beijing Foreign Studies University (BFSU)"), "https://mirrors.bfsu.edu.cn/" + path, ""};
        mirrors << Mirror{i18n("Rackspace (USA)"), "https://mirror.rackspace.com/" + path, ""};
        mirrors << Mirror{i18n("RIT (USA)"), "https://mirror.rit.edu/" + path, ""};
        return mirrors;
    };

    QList<Repository> rpmfusionRepos;
    rpmfusionRepos << Repository{"rpmfusion-free", i18n("RPM Fusion Free"), true, "rpmfusion-free.repo",
                                  rpmfusionMirrors("free", false)};
    rpmfusionRepos << Repository{"rpmfusion-free-updates", i18n("RPM Fusion Free Updates"), true, "rpmfusion-free-updates.repo",
                                  rpmfusionMirrors("free", true)};
    rpmfusionRepos << Repository{"rpmfusion-nonfree", i18n("RPM Fusion Nonfree"), true, "rpmfusion-nonfree.repo",
                                  rpmfusionMirrors("nonfree", false)};
    rpmfusionRepos << Repository{"rpmfusion-nonfree-updates", i18n("RPM Fusion Nonfree Updates"), true, "rpmfusion-nonfree-updates.repo",
                                  rpmfusionMirrors("nonfree", true)};
    sections["RPM Fusion"] = rpmfusionRepos;

    // Terra
    QList<Repository> terraRepos;
    auto terraMirrorsForRepo = [](const QString &repoId) -> QList<Mirror> {
        QString suffix;
        if (repoId == "terra") {
            suffix = "";
        } else {
            int dashPos = repoId.indexOf('-');
            if (dashPos != -1) {
                suffix = "-" + repoId.mid(dashPos + 1);
            } else {
                suffix = "";
            }
        }
        QList<Mirror> mirrors;
        mirrors << Mirror{i18n("Fyra Labs Official"), "https://repos.fyralabs.com/terra$releasever" + suffix + "/", ""};
        mirrors << Mirror{i18n("Freedif (Germany)"), "https://mirror.freedif.org/ultramarine/terra$releasever" + suffix + "/", ""};
        mirrors << Mirror{i18n("Alebcay (USA)"), "https://mirror.alebcay.com/files/fyralabs/terra$releasever" + suffix, ""};
        mirrors << Mirror{i18n("June Fish (USA)"), "https://mirror.june.fish/fyra/terra$releasever" + suffix + "/", ""};
        return mirrors;
    };

    terraRepos << Repository{"terra", i18n("Terra Base"), true, "terra.repo", terraMirrorsForRepo("terra")};
    terraRepos << Repository{"terra-extras", i18n("Terra Extras"), true, "terra.repo", terraMirrorsForRepo("terra-extras")};
    terraRepos << Repository{"terra-mesa", i18n("Terra Mesa"), true, "terra.repo", terraMirrorsForRepo("terra-mesa")};
    terraRepos << Repository{"terra-nvidia", i18n("Terra NVIDIA"), false, "terra.repo", terraMirrorsForRepo("terra-nvidia")};
    sections["Terra"] = terraRepos;

    return sections;
}
