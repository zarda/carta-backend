#include "PCacheSqlite3.h"
#include "CartaLib/Hooks/GetPersistentCache.h"
#include <QDebug>
#include <QtSql>
#include <QDir>

//class QMutex;
#include <qmutex.h>

static QMutex sql_mutex;

typedef Carta::Lib::Hooks::GetPersistentCache GetPersistentCacheHook;

///
/// Implementation of IPCache using sqlite
///
class SqLitePCache : public Carta::Lib::IPCache
{
public:



    virtual uint64_t
    maxStorage() override
    {
        return 1;
    }

    virtual uint64_t
    usedStorage() override
    {
        return 1;
    }

    virtual uint64_t
    nEntries() override
    {
        return 1;
    }

    virtual void
    deleteAll() override
    {
        if ( ! m_db.isOpen() ) {
            return;
        }
        QSqlQuery query( m_db );
        if ( ! query.exec("PRAGMA WRITABLE_SCHEMA = 1;") ) {
            qWarning() << "Unlock DB permission failed.";
        }
        if ( ! query.exec("DELETE FROM sqlite_master WHERE TYPE IN ('table');") ) {
            qWarning() << "Delete all tables failed.";
        }
        if ( ! query.exec("PRAGMA WRITABLE_SCHEMA = 0;") ) {
            qWarning() << "Lock DB permission failed.";
        }
        qWarning() << "All tables were deleted.";
        /// reduce disk usage
        if ( ! query.exec("VACUUM;") ) {
            qWarning() << "Vacuum failed.";
        }

    } // deleteAll

    virtual bool
    readEntry( const QByteArray & key, QByteArray & val, QByteArray & error ) override
    {
        sql_mutex.lock();
        if ( ! m_db.isOpen() ) {
             sql_mutex.unlock();
            return false;
        }
        QSqlQuery query( m_db );
        //query.prepare( "SELECT val FROM db WHERE key = :key" );

        // choose "val" and "error" with the minimum "error" for the specific "key"
        query.prepare( "SELECT val,error FROM db WHERE key = :key ORDER BY error ASC LIMIT 1" );
        query.bindValue( ":key", key );

        if ( ! query.exec() ) {
            qWarning() << "Select query failed.";
            sql_mutex.unlock();
            return false;
        }
        if ( query.next() ) {
            val = query.value( 0 ).toByteArray();
            error = query.value( 1 ).toByteArray();
            sql_mutex.unlock();
            return true;
        }
        sql_mutex.unlock();

        return false;
    } // readEntry

    virtual void
    setEntry( const QByteArray & key, const QByteArray & val, const QByteArray & error ) override
    {
        sql_mutex.lock();

        if ( ! m_db.isOpen() ) {
            sql_mutex.unlock();
            return;
        }

        //Q_UNUSED( priority );
        QSqlQuery query( m_db );
        //query.prepare( "INSERT OR REPLACE INTO db (key, val) VALUES ( :key, :val )" );

        // add a new value "error" in the row
        query.prepare( "INSERT INTO db (key, val, error) VALUES (:key, :val, :error)" );

        query.bindValue( ":key", key );
        query.bindValue( ":val", val );
        query.bindValue( ":error", error );

        if ( ! query.exec() ) {
            qWarning() << "Insert query failed:" << query.lastError().text();
        }
        sql_mutex.unlock();

    } // setEntry

    virtual void
    setEntry( const std::string & table, const std::vector<double> & value) override {
        sql_mutex.lock();

        if ( ! m_db.isOpen() ) {
            sql_mutex.unlock();
            return;
        }

        QSqlQuery query( m_db );

        SqLitePCache::deleteTable(table);

        SqLitePCache::createTable(table);

        query.prepare(
                    QString::fromStdString("INSERT INTO " + table + "(Value) VALUES (?);"));

        /// www.sqlite.org/faq.html#q19
        /// (19) INSERT is really slow - I can only do few dozen INSERTs per second
        qDebug()<<"can start a transaction:"<<QSqlDatabase::database().transaction();
        for(auto item : value){
            query.bindValue(0, item);
            query.exec();
        }
        qDebug()<<"end transaction:"<<QSqlDatabase::database().commit();

        sql_mutex.unlock();

    } // setEntry for array

    virtual bool
    readEntry( const std::string & table, std::vector<double> & Value) override {
        sql_mutex.lock();
        if ( ! m_db.isOpen() ) {
             sql_mutex.unlock();
            return false;
        }

        QSqlQuery query( m_db );
        Value.clear();

        int index = 0;
        if(query.exec( QString::fromStdString("SELECT * FROM " + table +";") )){
            while (query.next()) {
                Value.push_back( query.value( query.record().indexOf("Value") ).toDouble() );
                index++;
            }
        }
        query.exec("PRAGMA synchronous = OFF;");
        if ( index > 0) {
            sql_mutex.unlock();
            return true;
        }
        query.exec("PRAGMA synchronous = NORMAL;");
        sql_mutex.unlock();
        return false;
    } // readEntry for array

    virtual bool
    createTable( const std::string & table) override {
        if ( ! m_db.isOpen() ) {
            return false;
        }
        QSqlQuery query( m_db );

        query.prepare(
                    QString::fromStdString("CREATE TABLE IF NOT EXISTS " + table + " (Id integer PRIMARY KEY, Value);"));

        if ( ! query.exec() ) {
            qWarning() << "Create table failed:" << query.lastError().text();
            return false;
        }
        return true;
    } // create table for array

    virtual bool
    deleteTable( const std::string & table) override {
        if ( ! m_db.isOpen() ) {
            return false;
        }
        QSqlQuery query( m_db );

        /// delete the table
        if ( ! query.exec(QString::fromStdString("DROP TABLE IF EXISTS " + table +";")) ) {
            qWarning() << "Drop table failed.";
            return false;
        }
        /// reduce disk usage
        if ( ! query.exec("VACUUM;") ) {
            qWarning() << "Vacuum failed.";
            return false;
        }
        return true;
    } // delete table for array

    virtual void
    listTable(std::vector<std::string> & Tables) override {

        if ( ! m_db.isOpen() ) {
            return;
        }

        Tables.clear();
        for(auto item : m_db.tables()){
            Tables.push_back(item.toStdString());
        }

    } // list table name of array

    static
    Carta::Lib::IPCache::SharedPtr
    getCacheSingleton( QString dirPath)
    {
        sql_mutex.lock();
        if ( m_cachePtr ) {
            qCritical() << "PCacheSQlite3Plugin::Calling GetPersistentCacheHook multiple times!!!";
        }
        else {
            m_cachePtr.reset( new SqLitePCache( dirPath) );
        }
        sql_mutex.unlock();
        return m_cachePtr;
    }

    virtual void Release() override
    {
        m_cachePtr = nullptr;
    }

    ~SqLitePCache()
    {
        m_db.close();
    }

private:

    SqLitePCache( QString dirPath)
    {
        m_db = QSqlDatabase::addDatabase( "QSQLITE" );

        m_db.setDatabaseName( dirPath );
        bool ok = m_db.open();
        if ( ! ok ) {
            qCritical() << "Could not open sqlite database at location" << dirPath;
        }

        QSqlQuery query( m_db );

        //query.prepare( "CREATE TABLE IF NOT EXISTS db (key TEXT PRIMARY KEY, val BLOB)" );

        // allow to insert rows with the same "key TEXT" but may have different errors
        query.prepare( "CREATE TABLE IF NOT EXISTS db (key TEXT, val BLOB, error BLOB)" );

        if ( ! query.exec() ) {
            qCritical() << "Create table query failed:" << query.lastError().text();
        }
    }

private:

    QSqlDatabase m_db;
    static Carta::Lib::IPCache::SharedPtr m_cachePtr; //  = nullptr;
};

Carta::Lib::IPCache::SharedPtr SqLitePCache::m_cachePtr = nullptr;

PCacheSQlite3Plugin::PCacheSQlite3Plugin( QObject * parent ) :
    QObject( parent )
{ }

bool
PCacheSQlite3Plugin::handleHook( BaseHook & hookData )
{
    // we only handle one hook: get the cache object
    if ( hookData.is < GetPersistentCacheHook > () ) {
        // decode hook data
        GetPersistentCacheHook & hook = static_cast < GetPersistentCacheHook & > ( hookData );

        // if no dbdir was specified, refuse to work :)
        if( m_dbPath.isNull()) {
            hook.result.reset();
            return false;
        }

        // try to create the database
        hook.result = SqLitePCache::getCacheSingleton( m_dbPath);

        // return true if result is not null
        return hook.result != nullptr;
    }

    qWarning() << "PCacheSQlite3Plugin: Sorry, don't know how to handle this hook.";
    return false;
} // handleHook

void
PCacheSQlite3Plugin::initialize( const IPlugin::InitInfo & initInfo )
{
    qDebug() << "PCacheSQlite3Plugin initializing...";
    QJsonDocument doc( initInfo.json );
    qDebug() << doc.toJson();

    // extract the location of the database from carta.config
    m_dbPath = initInfo.json.value( "dbPath").toString();

    if( m_dbPath.isNull()) {
        qCritical() << "No dbPath specified for PCacheSqlite3 plugin!!!";
    }
    else {
        // convert this to absolute path just in case
        m_dbPath.replace("$(HOME)", QDir::homePath()); // get user's home directory
        m_dbPath.replace("$(APPDIR)", QCoreApplication::applicationDirPath()); // get the directory of CARTA Application
        m_dbPath = QDir(m_dbPath).absolutePath();
    }
}

std::vector < HookId >
PCacheSQlite3Plugin::getInitialHookList()
{
    return {
               GetPersistentCacheHook::staticId
    };
}
