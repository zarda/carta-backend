/**
 *
 **/

#include "Globals.h"
#include "IConnector.h"
#include "IPlatform.h"
#include "PluginManager.h"
#include <QThread>

Globals * Globals::m_instance = nullptr;

IPlatform *Globals::platform()
{
    Q_ASSERT( m_platform );
    return m_platform;
}

void Globals::setPlatform(IPlatform *platform)
{
    Q_ASSERT_X( ! m_platform, "Globals", "redefining platform");
    m_platform = platform;
}

PluginManager::SharedPtr Globals::pluginManager()
{
    Q_ASSERT( m_pluginManager);
    return m_pluginManager;
}

void Globals::setPluginManager(PluginManager::SharedPtr pluginManager)
{
    Q_ASSERT_X( ! m_pluginManager, "Globals", "redefining platform manager");
    m_pluginManager = pluginManager;
}

Globals *Globals::instance()
{
    if( ! m_instance) {
        m_instance = new Globals;
    }
    return m_instance;
}

const CmdLine::ParsedInfo * Globals::cmdLineInfo() const
{
    Q_ASSERT( m_cmdLineInfo);
    return m_cmdLineInfo;
}

void Globals::setCmdLineInfo(const CmdLine::ParsedInfo * cmdLineInfo)
{
    Q_ASSERT_X( ! m_cmdLineInfo, "Globals", "Redefinging command line info!?!?!");
    m_cmdLineInfo = cmdLineInfo;
}
const MainConfig::ParsedInfo * Globals::mainConfig() const
{
    Q_ASSERT( m_mainConfig);
    return m_mainConfig;
}

void Globals::setMainConfig(const MainConfig::ParsedInfo * mainConfig)
{
    Q_ASSERT_X( ! m_mainConfig, "Globals", "Redefinging main config info!?!?!");
    m_mainConfig = mainConfig;
}


Globals::Globals()
{
    m_connector = nullptr;
    m_platform = nullptr;
    m_pluginManager = nullptr;
    m_cmdLineInfo = nullptr;
    m_mainConfig = nullptr;
}



IConnector * Globals::connector()
{

    //20170829 grimmer:modify for new arch
    //origin plan: SessionDispatcher *c = static_cast<SessionDispatcher*>(m_connector);
    // now let SessionDispatcher implements IConnector

    //    Q_ASSERT( m_connector != nullptr);
    QString sessionID = QThread::currentThread()->objectName();

    IConnector* connector = m_connector->getConnectorInMap(sessionID);

    return connector;
}

QString Globals::sessionID() {
    return QThread::currentThread()->objectName();
}

void Globals::setConnector(IConnector *connector)
{
    Q_ASSERT_X( m_connector == nullptr, "Globals", "redefining connector?!?!");

    m_connector = connector;
}
