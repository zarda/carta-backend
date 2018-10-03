! include(../common.pri) {
  error( "Could not find the common.pri file!" )
}

QT  +=  core xml websockets webchannel network
QT  -=  gui widgets

HEADERS += \
    DesktopPlatform.h \
    NewServerConnector.h \
    SessionDispatcher.h \
    NewServerConnector.h

SOURCES += \
    DesktopPlatform.cpp \
    desktopMain.cpp \
    NewServerConnector.cpp \
    SessionDispatcher.cpp

HEADERS += \
    websockettransport.h \
    websocketclientwrapper.h

SOURCES += \
    websockettransport.cpp \
    websocketclientwrapper.cpp

INCLUDEPATH += ../../../ThirdParty/rapidjson/include
INCLUDEPATH += ../core

INCLUDEPATH += ../../../ThirdParty/protobuf/include
LIBS += -L../../../ThirdParty/protobuf/lib -lprotobuf

#INCLUDEPATH += /usr/local/opt/openssl/include
#LIBS += -L/usr/local/opt/openssl/lib -lssl

#INCLUDEPATH += /usr/local/opt/libuv/include
#LIBS += -L/usr/local/opt/libuv/lib -luv

#INCLUDEPATH += ../../../ThirdParty/uWebSockets/include
#LIBS += -L../../../ThirdParty/uWebSockets/lib -luWS -lz -lssl

unix: LIBS += -L$$OUT_PWD/../core/ -lcore
unix: LIBS += -L$$OUT_PWD/../CartaLib/ -lCartaLib
DEPENDPATH += $$PROJECT_ROOT/core
DEPENDPATH += $$PROJECT_ROOT/CartaLib

QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../CartaLib:\$$ORIGIN/../core\''

unix:macx {
    PRE_TARGETDEPS += $$OUT_PWD/../core/libcore.dylib
}
else{
    PRE_TARGETDEPS += $$OUT_PWD/../core/libcore.so
}

# set the name of Desktop Application
TARGET = CARTA

# for release builds
carta_qrc {

PREPROCESS_FILES = .
preprocess.name = autogenerate qrc file for release mode
preprocess.input = PREPROCESS_FILES
preprocess.output = files.qrc
preprocess.commands = touch files.qrc
preprocess.variable_out = RESOURCES
QMAKE_EXTRA_COMPILERS += preprocess

}
