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
    explicit UpsController(QObject *parent = nullptr);

signals:
    void newUpsState(const QString& upsState);

public slots:
    void sendUpsCommand();

protected slots:

protected:
    void tryConnection();

private:
    nut::Client* m_nutClient;
    QTimer m_pollSaiStatusTimer;
};

#endif // UPSCONTROLLER_H
