! include(../common.pri) {
  error( "Could not find the common.pri file!" )
}

QT      +=  network widgets xml websockets webchannel
# QT      +=  webkitwidgets

HEADERS += \
#    CustomWebPage.h \
    DesktopPlatform.h \
    NewServerConnector.h \
    SessionDispatcher.h \
    # NetworkReplyFileq.h \
    # NetworkAccessManager.h
    NewServerConnector.h

SOURCES += \
#    CustomWebPage.cpp \
    DesktopPlatform.cpp \
    desktopMain.cpp \
    NewServerConnector.cpp \
    SessionDispatcher.cpp \
    # NetworkAccessManager.cpp \
    # NetworkReplyFileq.cpp

#SOURCES += \
#    websockettransport.cpp \
#    websocketclientwrapper.cpp

#HEADERS += \
#    websockettransport.h \
#    websocketclientwrapper.h

RESOURCES = resources.qrc

INCLUDEPATH += ../../../ThirdParty/rapidjson/include
INCLUDEPATH += ../core

INCLUDEPATH += ../../../ThirdParty/protobuf/include
LIBS += -L../../../ThirdParty/protobuf/lib -lprotobuf

INCLUDEPATH += /usr/include/openssl
LIBS += -L/usr/lib/x86_64-linux-gnu -lssl

INCLUDEPATH += /usr/include
LIBS += -L/usr/lib/x86_64-linux-gnu -luv

INCLUDEPATH += ../../../ThirdParty/uWebSockets/include
LIBS += -L../../../ThirdParty/uWebSockets/lib -luWS -lz -lssl

unix: LIBS += -L$$OUT_PWD/../core/ -lcore
unix: LIBS += -L$$OUT_PWD/../CartaLib/ -lCartaLib
DEPENDPATH += $$PROJECT_ROOT/core
DEPENDPATH += $$PROJECT_ROOT/CartaLib

QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../CartaLib:\$$ORIGIN/../core\''

#QWT_ROOT = $$absolute_path("../../../ThirdParty/qwt")
unix:macx {
#    QMAKE_LFLAGS += '-F$$QWT_ROOT/lib'
#    LIBS +=-framework qwt
    PRE_TARGETDEPS += $$OUT_PWD/../core/libcore.dylib
}
else{
#    QMAKE_LFLAGS += '-Wl,-rpath,\'$$QWT_ROOT/lib\''
#    LIBS +=-L$$QWT_ROOT/lib -lqwt
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

# experimental websocket part, test on Mac
#LIBS +=  -lssl -lz -luv -luWS
#INCLUDEPATH += /usr/local/opt/openssl/include
#INCLUDEPATH += /usr/local/opt/libuv/include
#INCLUDEPATH += /usr/local/include
#LIBS += -L/usr/local/opt/openssl/lib
#LIBS += -L/usr/local/opt/libuv/lib
#LIBS += -L/usr/local/lib
