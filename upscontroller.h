#ifndef UPSCONTROLLER_H
#define UPSCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QSharedPointer>
#include <QFileSystemWatcher>

#include "nutclient.h"
#include "serialmanager/serialportwatcher.h"

#define UPS_ENABLE

class UpsController : public QObject
{
    Q_OBJECT
public:
    UpsController(QObject *parent = nullptr, const QString &upsDeviceName = "salicru");

signals:
    void newUpsState(const QString& upsState);

public slots:
    void sendUpsCommand();

protected slots:

protected:

private:
    nut::Client* m_nutClient;
    QTimer m_pollSaiStatusTimer;
    QString upsDeviceName;
};

#endif // UPSCONTROLLER_H
