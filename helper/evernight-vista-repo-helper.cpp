// evernight-vista-repo-helper.cpp
// --------------------------------
// evernight-vista-repo-gui 的特权助手（C++ 版本）。
//
// GUI 以普通用户身份运行。需要执行特权操作时调用：
//   pkexec /usr/libexec/evernight-vista-repo-helper <subcommand> [args]
// polkit 通过 /usr/share/polkit-1/actions/org.evernight.vista.repo.policy 中的
// org.freedesktop.policykit.exec.path / .argv1 注解匹配对应动作，并显示认证窗口。
// 只有管理员认证通过后，本助手才会以 root 身份执行。
//
// pkexec 会设置 PKEXEC_UID 为调用用户的 uid；助手运行在最小环境中。
// 维护命令会用 dnf/rpm 替换当前进程，因此 stdout/stderr 可直接回流到 GUI。
//
// 构建：
//   g++ -O2 -Wall -o evernight-vista-repo-helper evernight-vista-repo-helper.cpp
// 安装到：/usr/libexec/evernight-vista-repo-helper  (mode 0755, owner root:root)
//
// 导出 POT：
//   xgettext --language=C++ --keyword=_ --from-code=UTF-8 -o evernight-vista-repo-helper.pot evernight-vista-repo-helper.cpp

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *REPO_DIR   = "/etc/yum.repos.d";
static const char *BACKUP_DIR = "/etc/yum.repos.d/backup";
static const char *TEXT_DOMAIN = "evernight-vista-repo-gui";
static const char *LOCALE_DIR  = "/usr/share/locale";

#define _(String) gettext(String)

static void initI18n() {
    setlocale(LC_ALL, "");
    bindtextdomain(TEXT_DOMAIN, LOCALE_DIR);
    bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
    textdomain(TEXT_DOMAIN);
}

static void die(const std::string &message, int code = 1) {
    fprintf(stderr, "%s\n", message.c_str());
    exit(code);
}

// 递归删除目录树（尽力而为）。root 安装文件后用于清理 /tmp 下的用户暂存目录。
static void removeDirTree(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (!dir) {
        rmdir(path.c_str());
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        std::string child = path + "/" + name;
        struct stat st;
        if (lstat(child.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode))
                removeDirTree(child);
            else
                unlink(child.c_str());
        }
    }
    closedir(dir);
    rmdir(path.c_str());
}

// 复制文件内容。成功返回 true。
static bool copyFile(const std::string &src, const std::string &dst) {
    int in = open(src.c_str(), O_RDONLY);
    if (in < 0)
        return false;
    int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return false;
    }
    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out, buf + written, static_cast<size_t>(n - written));
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            written += w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = false;
    close(in);
    close(out);
    return ok;
}

// 将 GUI 准备好的暂存 .repo 文件安装到 /etc/yum.repos.d，并先备份现有文件。
// 操作完成后删除暂存目录。
static int applyConfig(const std::string &stageDir) {
    if (stageDir.empty()) {
        die(std::string(_("Invalid staging directory: ")) + _("(empty)"));
    }
    struct stat st;
    if (stat(stageDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        die(std::string(_("Invalid staging directory: ")) + stageDir);
    }

    // 确保备份目录存在。
    mkdir(BACKUP_DIR, 0755); // 已存在时忽略错误

    DIR *dir = opendir(stageDir.c_str());
    if (!dir) {
        die(std::string(_("Cannot open staging directory: ")) + stageDir);
    }

    // 收集并排序 .repo 条目，确保应用顺序可预测。
    std::vector<std::string> repoFiles;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        std::string fullPath = stageDir + "/" + name;
        struct stat entryStat;
        if (stat(fullPath.c_str(), &entryStat) != 0)
            continue;
        if (!S_ISREG(entryStat.st_mode)) {
            fprintf(stderr, _("Skipping non-regular file: %s\n"), name.c_str());
            continue;
        }
        // 只处理以 ".repo" 结尾的文件。
        if (name.size() >= 5 && name.substr(name.size() - 5) == ".repo") {
            repoFiles.push_back(name);
        } else {
            fprintf(stderr, _("Skipping non-repo file: %s\n"), name.c_str());
        }
    }
    closedir(dir);

    if (repoFiles.empty()) {
        removeDirTree(stageDir);
        fprintf(stderr, "%s\n", _("No repository files to apply."));
        return 1;
    }

    // 排序以确保顺序可预测。
    std::sort(repoFiles.begin(), repoFiles.end());

    bool allOk = true;
    int applied = 0;
    for (const std::string &name : repoFiles) {
        std::string src = stageDir + "/" + name;
        std::string dst = std::string(REPO_DIR) + "/" + name;
        std::string bak = std::string(BACKUP_DIR) + "/" + name;

        // 如果目标文件已存在，先备份。
        struct stat dstStat;
        if (stat(dst.c_str(), &dstStat) == 0) {
            // 先移除旧备份，确保 copyFile 写入全新文件。
            unlink(bak.c_str());
            if (!copyFile(dst, bak)) {
                fprintf(stderr, _("Warning: failed to back up %s\n"), name.c_str());
            }
        }

        // 安装暂存文件。
        if (!copyFile(src, dst)) {
            fprintf(stderr, _("Error: failed to install %s: %s\n"), name.c_str(), strerror(errno));
            allOk = false;
            continue;
        }
        if (chmod(dst.c_str(), 0644) != 0) {
            fprintf(stderr, _("Warning: failed to set permissions on %s: %s\n"), name.c_str(), strerror(errno));
        }
        printf(_("Applied: %s\n"), name.c_str());
        applied++;
    }

    // 无论成功或失败，都清理暂存目录。
    removeDirTree(stageDir);

    if (applied == 0) {
        fprintf(stderr, "%s\n", _("No repository files were applied."));
        return 1;
    }

    printf(_("Repository configuration updated (%d file(s)).\n"), applied);
    return allOk ? 0 : 1;
}

// 用指定命令替换当前进程，使输出直接回流 GUI，并保留退出码。
static void runCommand(const std::vector<std::string> &argv) {
    // 构建 execvp 需要的 char* 数组。
    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (const std::string &arg : argv)
        cargv.push_back(const_cast<char *>(arg.c_str()));
    cargv.push_back(nullptr);

    execvp(cargv[0], cargv.data());
    // execvp 只有失败时才会返回。
    die(std::string(_("Failed to execute ")) + cargv[0] + ": " + strerror(errno), 127);
}

int main(int argc, char *argv[]) {
    initI18n();

    if (argc < 2) {
        die(std::string(_("Usage: evernight-vista-repo-helper ")) +
            _("<apply|rebuilddb|autoremove|distro-sync|refresh-cache> [args]"), 2);
    }

    std::string sub = argv[1];

    if (sub == "apply") {
        std::string stageDir = (argc > 2) ? argv[2] : "";
        return applyConfig(stageDir);
    } else if (sub == "rebuilddb") {
        runCommand({"rpm", "--rebuilddb"});
    } else if (sub == "autoremove") {
        runCommand({"dnf", "autoremove", "-y"});
    } else if (sub == "distro-sync") {
        runCommand({"dnf", "distro-sync", "-y"});
    } else if (sub == "refresh-cache") {
        runCommand({"dnf", "makecache"});
    } else {
        die(std::string(_("Unknown subcommand: ")) + sub, 2);
    }

    return 0; // 不可达
}
