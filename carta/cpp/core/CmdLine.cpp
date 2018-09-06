/**
 *
 **/


#include "CmdLine.h"
#include <QDebug>
#include <QCommandLineParser>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <qglobal.h>

static QString cartaGetEnv( const QString & name)
{
    std::string fullName = ("CARTAVIS_" + name).toStdString();
    return qgetenv( fullName.c_str());
}

namespace CmdLine {

ParsedInfo parse(const QStringList & argv)
{
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument( "input-files", "list of files to open", "[input files]");
    QCommandLineOption configFileOption(
        { "config", "cfg"}, "config file path", "configFilePath");
    parser.addOption( configFileOption);
    QCommandLineOption htmlPathOption(
        "html", "development option for desktop version, path to html to load", "htmlPath"
                );
    parser.addOption( htmlPathOption);
    QCommandLineOption scriptPortOption(
                "scriptPort", "port on which to listen for scripted commands", "scriptPort");
    parser.addOption( scriptPortOption);
    QCommandLineOption sessiondispatcherPortOption(
                "port", "listening port for the Session Dispatcher", "port");
    parser.addOption( sessiondispatcherPortOption);

    // Process the actual command line arguments given by the user, exit if
    // command line arguments have a syntax error, or the user asks for -h or -v
    parser.process( argv);

    // try to get config file path from command line
    ParsedInfo info;
    info.m_configFilePath = parser.value( configFileOption);
    // if command line was not used to set the config file path, try environment var
    if( info.m_configFilePath.isEmpty()) {
        info.m_configFilePath = cartaGetEnv( "CONFIG");
    }
    // if the config file was not specified neither through command line or environment
    // assign a default value (search the config file under the home directory first)
    if( info.m_configFilePath.isEmpty()) {
        info.m_configFilePath = QDir::homePath() + "/.cartavis/config.json";
    }

    // if the config file was not specified neither through command line or environment
    // assign a default value (search the config file under the carta build directory second)
    if (!QFile::exists(info.m_configFilePath)) {
#ifdef Q_OS_LINUX
        info.m_configFilePath = QCoreApplication::applicationDirPath() + "/../../config/config.json";
        if (!QFile::exists(info.m_configFilePath)) {
            info.m_configFilePath = QCoreApplication::applicationDirPath() + "/../etc/config/config.json";
        }
#else
        info.m_configFilePath = QCoreApplication::applicationDirPath() + "/../../../../../config/config.json";
        if (!QFile::exists(info.m_configFilePath)) {
            info.m_configFilePath = QCoreApplication::applicationDirPath() + "/../Resources/config/config.json";
        }
#endif
    }

    // get html path
    if( parser.isSet( htmlPathOption)) {
        info.m_htmlPath = parser.value( htmlPathOption);
    }


    // get script port
    if( parser.isSet( scriptPortOption)) {
        QString portString = parser.value( scriptPortOption);
        bool ok;
        info.m_scriptPort = portString.toInt( & ok);
        if( ! ok || info.m_scriptPort < 0 || info.m_scriptPort > 65535) {
            parser.showHelp( -1);
        }

    }
    qDebug() << "script port=" << info.scriptPort();


    // get session dispatcher port
    if( parser.isSet( sessiondispatcherPortOption)) {
        QString dispatchString = parser.value( sessiondispatcherPortOption);
        bool ok;
        info.m_port = dispatchString.toInt( & ok);
        if( ! ok || info.m_port < 0 || info.m_port > 65535) {
            parser.showHelp( -1);
        }

    }
    qDebug() << "sessionDispatcher port=" << info.port();

    // get a list of files to open
    info.m_fileList = parser.positionalArguments();
    qDebug() << "list of files to open:" << info.m_fileList;

    return info;
}

QString ParsedInfo::configFilePath() const
{
    return m_configFilePath;
}

QString ParsedInfo::htmlPath() const
{
    return m_htmlPath;
}

const QStringList &ParsedInfo::fileList() const
{
    return m_fileList;
}

int ParsedInfo::scriptPort() const
{
    return m_scriptPort;
}

int ParsedInfo::port() const
{
    return m_port;
}

} // namespace CmdLine
