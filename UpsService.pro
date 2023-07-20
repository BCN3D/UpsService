QT -= gui
QT += serialport serialbus dbus jsonserializer network

CONFIG += c++17 console
CONFIG -= app_bundle

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include( ../3rdparty/3rdparty.pri )
include( ../Common/Common.pri )

LIBS += -lNutClient

SOURCES += *.cpp

HEADERS += *.h

# Default rules for deployment.
linux {
    CONFIG(debug, debug|release) {
        target.path = /tmp
    } else {
        target.path = /usr/bin/bcn3d/services
    }

    INSTALLS += target
}

STATECHARTS +=
