
#pragma once
#include <QObject>
#include <QString>
#include <QDebug>
#include <QtSql>
#include <QDir>
#include <qmutex.h>

static QMutex sql_mutex;
///
/// Implementation of IPCache using sqlite
///
class SqLitePCacheVector
{
private:

    QSqlDatabase m_db;


public:

    SqLitePCacheVector( QString dirPath)
    {
        m_db = QSqlDatabase::database();

        m_db.setDatabaseName( dirPath );
        bool ok = m_db.open();
        if ( ! ok ) {
            qCritical() << "Could not open sqlite database at location" << dirPath;
        }

        QSqlQuery query( m_db );

//        query.prepare( "CREATE TABLE IF NOT EXISTS db (key TEXT, val BLOB, error BLOB)" );

//        if ( ! query.exec() ) {
//            qCritical() << "Create table query failed:" << query.lastError().text();
//        }
    }

    void
    deleteAll() {
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

    void
    setEntry( const std::string & table, const std::vector<double> & value){
        sql_mutex.lock();

        if ( ! m_db.isOpen() ) {
            sql_mutex.unlock();
            return;
        }

        QSqlQuery query( m_db );

        SqLitePCacheVector::deleteTable(table);

        SqLitePCacheVector::createTable(table);

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

    bool
    readEntry( const std::string & table, std::vector<double> & Value){
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

    bool
    createTable( const std::string & table){
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

    bool
    deleteTable( const std::string & table){
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

    void
    listTable(std::vector<std::string> & Tables) {

        if ( ! m_db.isOpen() ) {
            return;
        }

        Tables.clear();
        for(auto item : m_db.tables()){
            Tables.push_back(item.toStdString());
        }

    } // list table name of array


    virtual void Release(){}

    ~SqLitePCacheVector()
    {
        m_db.close();
    }

};
