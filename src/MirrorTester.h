#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QList>
#include <QPair>

class QNetworkReply;

class MirrorTester : public QObject {
    Q_OBJECT
public:
    explicit MirrorTester(QObject *parent = nullptr);

    void testMirrors(const QList<QPair<QString, QString>> &mirrorUrls);

signals:
    void mirrorTestProgress(int current, int total);
    void mirrorTestFinished(const QList<QPair<QString, int>> &results);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
    QList<QPair<QString, QString>> m_mirrors;
    QList<QPair<QString, int>> m_results;
    int m_currentIndex = 0;
    QElapsedTimer m_timer;

    void startNext();
};