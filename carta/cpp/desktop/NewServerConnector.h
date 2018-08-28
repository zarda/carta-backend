/**
 *
 **/


#ifndef NEW_SERVER_CONNECTOR_H
#define NEW_SERVER_CONNECTOR_H

#include <QObject>
#include "core/IConnector.h"
#include "core/CallbackList.h"
#include "core/Viewer.h"
#include "CartaLib/IRemoteVGView.h"
#include "CartaLib/IPercentileCalculator.h"
#include <QList>
#include <QByteArray>

typedef Carta::Lib::RegionHistogramData RegionHistogramData;

class IView;

/// private info we keep with each view
/// unfortunately it needs to live as it's own class because we need to give it slots...
//class ViewInfo;

class NewServerConnector : public QObject, public IConnector
{
    Q_OBJECT
public:

    /// constructor
    explicit NewServerConnector();

    // implementation of IConnector interface
    virtual void initialize( const InitializeCallback & cb) override;
    virtual void setState(const QString& state, const QString & newValue) override;
    virtual QString getState(const QString&) override;
    virtual CallbackID addCommandCallback( const QString & cmd, const CommandCallback & cb) override;
    virtual CallbackID addMessageCallback( const QString & cmd, const MessageCallback & cb) override;
    virtual CallbackID addStateCallback(CSR path, const StateChangedCallback &cb) override;
    virtual void registerView(IView * view) override;
    void unregisterView( const QString& viewName ) override;
    virtual qint64 refreshView( IView * view) override;
    virtual void removeStateCallback( const CallbackID & id) override;
    virtual Carta::Lib::IRemoteVGView *
    makeRemoteVGView( QString viewName) override;

    /// Return the location where the state is saved.
    virtual QString getStateLocation( const QString& saveName ) const override;

     ~NewServerConnector();

    Viewer viewer;
    QThread *selfThread; //not really use now, may take effect later

public slots:

    void startViewerSlot(const QString & sessionID);
    void onTextMessage(QString message);
    void onBinaryMessage(char* message, size_t length);
    void sendSerializedMessage(char* message, QString respName, PBMSharedPtr msg);

signals:

    //grimmer: newArch will not use stateChange mechanism anymore

    //new arch
    void startViewerSignal(const QString & sessionID);
    void onTextMessageSignal(QString message);
    void onBinaryMessageSignal(char* message, size_t length);

    void jsTextMessageResultSignal(QString result);
    void jsBinaryMessageResultSignal(char* message, size_t length);

    // /// we emit this signal when state is changed (either by c++ or by javascript)
    // /// we listen to this signal, and so does javascript
    // /// our listener then calls callbacks registered for this value
    // /// javascript listener caches the new value and also calls registered callbacks
    // void stateChangedSignal( const QString & key, const QString & value);

    // /// we emit this signal when command results are ready
    // /// javascript listens to it
    // void jsCommandResultsSignal(const QString & sessionID, const QString & senderSession, const QString & cmd, const QString & results, const QString & subIdentifier);
    // /// emitted by c++ when we want javascript to repaint the view
    // void jsViewUpdatedSignal(const QString & sessionID, const QString & viewName, const QString & img, qint64 id);

public:

    typedef std::vector<CommandCallback> CommandCallbackList;
    std::map<QString,  CommandCallbackList> m_commandCallbackMap;

    typedef std::vector<MessageCallback> MessageCallbackList;
    std::map<QString,  MessageCallbackList> m_messageCallbackMap;

    // list of callbacks
    typedef CallbackList<CSR, CSR> StateCBList;

    /// for each state we maintain a list of callbacks
    std::map<QString, StateCBList *> m_stateCallbackList;

    /// IDs for command callbacks
    CallbackID m_callbackNextId;

    /// private info we keep with each view
    struct ViewInfo;

    /// map of view names to view infos
    std::map< QString, ViewInfo *> m_views;

    ViewInfo * findViewInfo(const QString &viewName);

    // virtual void refreshViewNow(IView *view);

    IConnector* getConnectorInMap(const QString & sessionID) override;
    void setConnectorInMap(const QString & sessionID, IConnector *connector) override;

    void startWebSocket() override;

    /// @todo move as may of these as possible to protected section

protected:

    InitializeCallback m_initializeCallback;
    std::map< QString, QString > m_state;

private:
    bool m_changeImage = false;
    int m_xMin = 0;
    int m_xMax = 0;
    int m_yMin = 0;
    int m_yMax = 0;
    int m_mip = 0;
};


#endif // NEW_SERVER_CONNECTOR_H
