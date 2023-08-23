#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <unistd.h>
#include "qserialport.h"
#include "upscontroller.h"


UpsController::UpsController(QObject *parent, const QString &upsDeviceName) :
    QObject(parent),
    upsDeviceName(upsDeviceName)
{
#ifdef UPS_ENABLE

    qInfo() << "starting UpsController...";

    available_ports = QSerialPortInfo::availablePorts();
    //available_clients[current_client] // check that current_client points to the first item
    changeState(UPS_STATE::TEST_CONNECTION);
    doWork();
#endif
}


bool UpsController::getNextClient(){

    bool ok = false;
    size_t l = sizeof(available_clients) / sizeof(UPS_CLIENT);
    if (++current_client < l) {
        ok = true;
    }
    return ok;
}


void UpsController::doWork() {

    //qDebug() << QString("dowork(%1)").arg(stateName(ups_state));
    switch (ups_state) {
    case UPS_STATE::TEST_CONNECTION:
        qDebug() << QString("client: %1 try #%2 of %3").arg(available_clients[current_client]).arg(connection_tries + 1).arg(MAX_CONNECTIONS_TRIED);
        connect_client();
        break;

    case UPS_STATE::TEST_FAIL:
        sleep(NEXT_CONNECTION_WAIT_SECS);
        if (++connection_tries == MAX_CONNECTIONS_TRIED) {
            connection_tries = 0;
            if (getNextClient()) {
                qInfo() << "max tries reached, trying next client (" << available_clients[current_client] << ")";
                changeState(UPS_STATE::TEST_CONNECTION);
                doWork();
            } else {
                qInfo("No more clients to test, there is not UPS");
                changeState(UPS_STATE::ERROR);

                // QUIT ???
            }
        } else {
            changeState(UPS_STATE::TEST_CONNECTION);
            doWork();
        }
        break;

    case UPS_STATE::TEST_OK:
        changeState(UPS_STATE::CHECK);
        connect(&m_pollSaiStatusTimer, &QTimer::timeout, this, &UpsController::sendUpsCommand);
        m_pollSaiStatusTimer.start(1000);
        break;

    case UPS_STATE::CHECK:
        break;

    case UPS_STATE::WAITING:
        // do nothing
        usleep(10000);
        break;

    case UPS_STATE::ERROR:
        break;

    default:
        qDebug() << "S: ??? " << stateName(ups_state);
        // do nothing
        usleep(10000);
        doWork();
        break;
    }
}


void UpsController::connect_client() {

    qDebug() << "testing client " << available_clients[current_client];
    switch (available_clients[current_client]) {
    case NUT:
        try {
            m_nutClient = new nut::TcpClient("localhost", 3493);
            QString test = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName.toStdString(), "ups.status")[0]);
            qDebug() << "NUT driver test: " << test;
            if (test.contains("DRIVER-NOT-CONNECTED")) {
                qWarning() << "NUT driver not connected";
                changeState(UPS_STATE::TEST_FAIL);
            } else {
                qInfo("NUT client connected");
                m_nutClient->authenticate("admin", "admin");
                changeState(UPS_STATE::TEST_OK);
            }
        } catch (nut::NutException e) {
            qWarning() << "NUT driver error: " << QString::fromStdString(e.str());
            changeState(UPS_STATE::TEST_FAIL);
        }
        doWork();
        break;

    case MODBUS:
        if (!checkMODBUSPort()) {
            doWork();
        }
        break;

    default:
        qWarning("UNKNOWN UPS CLIENT!");
        break;
    }
}


bool UpsController::checkMODBUSPort() {

    bool ok = true;

    qDebug() << "checkMODBUSPort()";
    /*** port scan (disabled due to issues with peripherals board
    if (current_port < available_ports.length()) {

        QSerialPortInfo &portInfo = available_ports[current_port];
        mb_portname = portInfo.portName();
    */
        mb_portname = "/dev/ttySAI";
        qDebug() << "trying MODBUS on port " << mb_portname;
        modbusDevice = new QModbusRtuSerialMaster(this);
        modbusDevice->setConnectionParameter(QModbusDevice::SerialPortNameParameter, mb_portname);
        modbusDevice->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::MarkParity);
        modbusDevice->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, QSerialPort::Baud19200);
        modbusDevice->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
        modbusDevice->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);
        modbusDevice->setTimeout(2000); // milliseconds
        modbusDevice->setNumberOfRetries(1);
        if (modbusDevice->connectDevice()) {

            qDebug() << "device connected";
            MBtestRequest();
        } else {
            qWarning() << "No serial port available";
            changeState(UPS_STATE::TEST_FAIL);
            ok = false;
        }
    /* port scan (disabled due to issues with peripherals board
    } else {
        qWarning() << "No serial ports available";
        changeState(UPS_STATE::TEST_FAIL);
        ok = false;
    }
    */
    return ok;
}


void UpsController::MBtestRequest() {

    //qDebug() << "MBtestRequest (" << mb_portname << ")";
    changeState(UPS_STATE::WAITING);

    // the test request also puts the UPS online
    QModbusDataUnit pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_ONLINE, 2);
    if (auto *reply = modbusDevice->sendWriteRequest(pdu, MODBUS_SLAVE_ID)) {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &UpsController::MBtestResponse);
        else
            delete reply; // broadcast replies return immediately
    } else {
        qWarning() << "MODBUS Read error (online): " << modbusDevice->errorString();
        changeState(UPS_STATE::TEST_FAIL);
    }

}


void UpsController::MBtestResponse() {

    //qDebug() << "MBtestResponse (" << mb_portname << ")";
    UPS_STATE newUpsState = UPS_STATE::TEST_CONNECTION;
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError) {

        qDebug() << QString("MODBUS test reply OK (%1, state: %2").arg(mb_portname).arg(stateName(ups_state));
        newUpsState = UPS_STATE::TEST_OK;

    } else if (reply->error() == QModbusDevice::ProtocolError) {
        qWarning() << QString("Test read response error: %1 (MODBUS exception: 0x%2 from %3)")
                          .arg(reply->errorString())
                          .arg(reply->rawResult().exceptionCode(), -1, 16)
                          .arg(mb_portname);
    } else {
        qWarning() << QString("Test read response error: %1 (code: 0x%2) from %3")
                          .arg(reply->errorString())
                          .arg(reply->error(), -1, 16)
                          .arg(mb_portname);
    }

    if (newUpsState != UPS_STATE::TEST_OK) {
        current_port++;
    }
    changeState(newUpsState);
    reply->deleteLater();
    doWork();
}


void UpsController::sendUpsCommand()
{
    static bool waitingForUpsOnline = false;
    QString upsState = "";
    QModbusDataUnit pdu;

#ifdef UPS_ENABLE

    if (ups_state == UPS_STATE::WAITING) {
        //qDebug() << "sendUpsCommand UPS_STATE::WAITING";
        return;
    }

    switch (available_clients[current_client]) {
    case NUT:
        try {
            upsState = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName.toStdString(), "ups.status")[0]);
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while requesting ups data. Details: " << QString::fromStdString(e.str());
            return;
        }
        //qDebug() << upsState;
        if (upsState.contains("BYPASS") && !waitingForUpsOnline) {
            try {
                m_nutClient->executeDeviceCommand(upsDeviceName.toStdString(), "load.on"); // Be sure that the UPS is online mode!
                waitingForUpsOnline = true;
            } catch (nut::NutException e) {
                qWarning() << "Nut driver error while setting UPS online mode. Details: " << QString::fromStdString(e.str());
            }
        } else if (upsState == "OL" && waitingForUpsOnline) {
            waitingForUpsOnline = false;
        }
        qDebug() << "NUT newUpsState " << upsState;
        emit newUpsState(upsState);
        break;

    case MODBUS:

        //qDebug() << "sendUpsCommand MODBUS (" << mb_portname << ")";

        // Read alarm registers
        pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_ALARM, 1);
        if (auto *reply = modbusDevice->sendReadRequest(pdu, MODBUS_SLAVE_ID)) {
            if (!reply->isFinished())
                connect(reply, &QModbusReply::finished, this, &UpsController::MODBUSresponse);
            else
                delete reply;
        } else {
            qWarning() << "MODBUS Read error (alarm): " << modbusDevice->errorString();
        }

        // Read battery registers
        pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_BATTERY, 4);
        if (auto *reply = modbusDevice->sendReadRequest(pdu, MODBUS_SLAVE_ID)) {
            if (!reply->isFinished())
                connect(reply, &QModbusReply::finished, this, &UpsController::MODBUSresponse);
            else
                delete reply;
        } else {
            qWarning() << "MODBUS Read error (battery): " << modbusDevice->errorString();
        }

        switch (ups_state) {
        case UPS_STATE::CHECK:
            changeState(UPS_STATE::WAITING);
            break;
        default:
            break;
        }
        break;
    default:
        // do nothing
        break;
    }
#endif
}

QString UpsController::stateName(UPS_STATE state) {

    QString r = "unknown";

    switch (state) {
    case UPS_STATE::OUT:             r = "OUT"; break;
    case UPS_STATE::TEST_CONNECTION: r = "TEST_CONNECTION"; break;
    case UPS_STATE::TEST_OK:         r = "TEST_OK"; break;
    case UPS_STATE::TEST_FAIL:       r = "TEST_FAIL"; break;
    case UPS_STATE::CHECK:           r = "CHECK"; break;
    case UPS_STATE::WAITING:         r = "WAITING"; break;
    case UPS_STATE::ERROR:           r = "ERROR"; break;
    }
    return r;
}

void UpsController::changeState(UPS_STATE newstate) {
    //qDebug() << QString("state change from %1 to %2").arg(stateName(ups_state)).arg(stateName(newstate));
    ups_state = newstate;
}

void UpsController::MODBUSresponse() {

    QString upsState = "OB: OFF";
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError) {

        //qDebug() << QString("MODBUS reply OK (%1, state: %2").arg(mb_portname).arg(stateName(ups_state));
        switch (ups_state) {
        case UPS_STATE::TEST_CONNECTION:
            changeState(UPS_STATE::TEST_OK);
            doWork();
            break;
        case UPS_STATE::WAITING:
            changeState(UPS_STATE::CHECK);
            break;
        default:
            break;
        }

        const QModbusDataUnit unit = reply->result();
        int batlvl = 0;
        int minleft = 0;

        switch (unit.startAddress()) {
        case MB_REG_ALARM:
            if (1 & unit.value(0) >> MB_ALARM_AC_BIT) {
                upsState = "OL: on";
            } else {
                upsState = "OB: OFF";
            }
            break;

        case MB_REG_BATTERY:
            /*** debug ***
            for (int i = 0, total = int(unit.valueCount()); i < total; ++i) {
                const QString entry = QString("Address: %1, Value: %2")
                                        .arg(unit.startAddress() + i)
                                        .arg(QString::number(unit.value(i), unit.registerType() <= QModbusDataUnit::Coils ? 10 : 16));
                qDebug() << "MODBUS: " << entry;
            }
            //*/
            batlvl = unit.value(0);
            minleft = unit.value(2)/60;
            upsState = QString(" battery level: %1%  (minutes left: %2)").arg(batlvl).arg(minleft);
            break;
/*
        case MB_REG_ONLINE:
            qDebug() << "MB_REG_ONLINE completed";
            modbus_state = ONLINE_OK;
            break;
*/
        default:
            // ignore response
            upsState = "";
            break;
        }

    } else if (reply->error() == QModbusDevice::ProtocolError) {
        qWarning() << QString("Read response error: %1 (MODBUS exception: 0x%2 from %3)")
                          .arg(reply->errorString())
                          .arg(reply->rawResult().exceptionCode(), -1, 16)
                          .arg(mb_portname);
    } else {
        qWarning() << QString("Read response error: %1 (code: 0x%2) from %3")
                          .arg(reply->errorString())
                          .arg(reply->error(), -1, 16)
                          .arg(mb_portname);
    }
    reply->deleteLater();

    if (upsState.length() > 1) {
        qDebug() << "MODBUS newUpsState: " << upsState;
        emit newUpsState(upsState);
    }
    doWork();
}
