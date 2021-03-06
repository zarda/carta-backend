/// Plugin for generating image statistics.

#pragma once

#include "CartaLib/IPlugin.h"
#include <QObject>

class StatisticsCASA : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA( IID "org.cartaviewer.IPlugin" )
    Q_INTERFACES( IPlugin )
    ;

public:

    /**
     * Constructor.
     */
    StatisticsCASA( QObject * parent = 0 );
    virtual bool
    handleHook( BaseHook & hookData ) override;

    virtual std::vector < HookId >
    getInitialHookList() override;

    virtual ~StatisticsCASA();


};
