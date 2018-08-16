/**
 *
 **/

#include "DesktopPlatform.h"
#include "SessionDispatcher.h"

#include <QtWidgets>

std::string warningColor, criticalColor, fatalColor, resetColor;
static void initializeColors() {
    static bool initialized = false;
    if( initialized) return;
    initialized = true;
    if( isatty(3)) return;
    warningColor = "\033[1m\033[36m";
    criticalColor = "\033[31m";
    fatalColor = "\033[41m";
    resetColor = "\033[0m";
}

static const int m_isatty = isatty(3);

/// custom Qt message handler
static
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &pmsg)
{
    initializeColors();

    QString msg = pmsg;
    if( ! msg.endsWith( '\n')) {
        msg += '\n';
    }
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug: %s", localMsg.constData());
        break;
    case QtWarningMsg:
        fprintf(stderr, "%sWarning: %s (%s:%u, %s)%s\n",
                warningColor.c_str(),
                localMsg.constData(), context.file, context.line, context.function,
                resetColor.c_str());
        break;
    case QtCriticalMsg:
        fprintf(stderr, "%sCritical: %s (%s:%u, %s)%s\n",
                criticalColor.c_str(),
                localMsg.constData(), context.file, context.line, context.function,
                resetColor.c_str());
        break;
    case QtFatalMsg:
        fprintf(stderr, "%sFatal: %s (%s:%u, %s)%s\n",
                fatalColor.c_str(),
                localMsg.constData(), context.file, context.line, context.function,
                resetColor.c_str());
        abort();
    }

} // qtMessageHandler

DesktopPlatform::DesktopPlatform()
    : QObject( nullptr)
{
    // install a custom message handler
    qInstallMessageHandler( qtMessageHandler);

    // create the connector
    m_connector = new SessionDispatcher();

}

IConnector * DesktopPlatform::connector()
{
    return m_connector;
}

void DesktopPlatform::goFullScreen()
{
    // m_mainWindow->showFullScreen();
}

const QStringList & DesktopPlatform::initialFileList()
{
    return m_initialFileList;
}

bool DesktopPlatform::isSecurityRestricted() const {
    return false;
}

QString DesktopPlatform::getCARTADirectory()
{
	return QDir::homePath().append("/CARTA/");
}
