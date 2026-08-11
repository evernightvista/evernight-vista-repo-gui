#include "MirrorTester.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <KLocalizedString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

MirrorTester::MirrorTester(QObject *parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager, &QNetworkAccessManager::finished, this, &MirrorTester::onReplyFinished);
}

void MirrorTester::testMirrors(const QList<QPair<QString, QString>> &mirrorUrls) {
    m_mirrors = mirrorUrls;
    m_results.clear();
    m_currentIndex = 0;
    if (m_mirrors.isEmpty()) {
        emit mirrorTestFinished(m_results);
        return;
    }
    emit mirrorTestProgress(0, m_mirrors.size());
    startNext();
}

void MirrorTester::startNext() {
    if (m_currentIndex >= m_mirrors.size()) {
        emit mirrorTestFinished(m_results);
        return;
    }
    QString url = m_mirrors[m_currentIndex].second;
    QNetworkRequest request(QUrl(url + "repodata/repomd.xml"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);
    m_timer.start();
    m_manager->head(request);
}

void MirrorTester::onReplyFinished(QNetworkReply *reply) {
    int elapsed = -1;
    QString errorMsg;
    if (reply->error() == QNetworkReply::NoError) {
        elapsed = m_timer.elapsed();
    } else {
        errorMsg = reply->errorString();
        QString url = reply->request().url().toString();
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        QString logLine = QString("[%1] FAILED: %2 - %3\n")
                              .arg(timestamp)
                              .arg(url)
                              .arg(errorMsg);

        QFile logFile("/tmp/mirror-verbose.log");
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << logLine;
            logFile.close();
        }

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
            elapsed = -403;
        }
    }

    QString name = m_mirrors[m_currentIndex].first;
    m_results.append(qMakePair(name, elapsed));
    m_currentIndex++;
    emit mirrorTestProgress(m_currentIndex, m_mirrors.size());
    reply->deleteLater();
    startNext();
}