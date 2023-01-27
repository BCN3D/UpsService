#include <QCoreApplication>
#include <QtDBus/QDBusConnection>

#include <initializer_list>
#include <signal.h>
#include <unistd.h>

#include "upsservice.h"
#include "upsservicesettings.h"
#include "upsservice_adaptor.h"
#include "simpleloghandler.h"

void catchUnixSignals(std::initializer_list<int> quitSignals) {
    auto handler = [](int) -> void {
        QCoreApplication::quit();
    };

    sigset_t blocking_mask;
    sigemptyset(&blocking_mask);
    for (auto sig : quitSignals)
        sigaddset(&blocking_mask, sig);

    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_mask    = blocking_mask;
    sa.sa_flags   = 0;

    for (auto sig : quitSignals)
        sigaction(sig, &sa, nullptr);
}

int main(int argc, char *argv[])
{
    catchUnixSignals({SIGQUIT, SIGINT, SIGTERM, SIGHUP, SIGKILL});

    qInstallMessageHandler(simpleloghandler);

    qRegisterMetaType<UpsServiceSettings*>();

    QCoreApplication coreApplication(argc, argv);

    QString configPath, upsDeviceName;
    if(argc > 0) {
        configPath = argv[1];
    }
    if(argc > 1) {
        upsDeviceName = argv[2];
    }

    UpsService  upsService(configPath, upsDeviceName);

    QObject::connect(&coreApplication, SIGNAL(aboutToQuit()), &upsService, SLOT(handleQuitSignal()));

    QScopedPointer<UpsServiceAdaptor> psiAdaptor(new UpsServiceAdaptor(&upsService));
#ifdef __arm__
    QDBusConnection connection = QDBusConnection::systemBus();
#else
    QDBusConnection connection = QDBusConnection::sessionBus();
#endif
    connection.registerObject("/UpsService", &upsService);
    connection.registerService("com.bcn3dtechnologies.UpsService");


    return coreApplication.exec();
}
