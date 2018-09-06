/// IPCache is a set of APIs to access persistent cache.

#pragma once

#include "CartaLib/CartaLib.h"
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <memory>

namespace Carta
{
namespace Lib
{
class IPCache
{
    CLASS_BOILERPLATE( IPCache );

public:

    /// return maximum storage in bytes
    virtual uint64_t
    maxStorage() = 0;

    /// return used storage in bytes
    virtual uint64_t
    usedStorage() = 0;

    //// return number of entries
    virtual uint64_t
    nEntries() = 0;

    /// remove all entries
    virtual void
    deleteAll() = 0;

    /// read a value of an entry
    /// if entry does not exist, false is returned
    virtual bool
    readEntry( const QByteArray & key,
               QByteArray & val,
               QByteArray & error ) = 0;

    /// set a value of an entry
    virtual void
    setEntry( const QByteArray & key,
              const QByteArray & val,
              const QByteArray & error ) = 0;

    /// for array IO
        /// read array
        virtual void
        setEntry( const std::string & table, const std::vector<double> & value) = 0;

        /// write array
        virtual bool
        readEntry( const std::string & table, std::vector<double> & Value) = 0;

        /// create empty array
        virtual bool
        createTable( const std::string & table) = 0;

        /// delete array
        virtual bool
        deleteTable( const std::string & table) = 0;

        /// list name of arraies
        virtual void
        listTable(std::vector<std::string> & Tables) = 0;


    /// Release the shared_ptr before the program quits.
    /// There may be a better way to prevent the segementation fault
    /// comes from the ~SqLitePCache when CARTA shuts down.
    virtual void
    Release() = 0;

    virtual
    ~IPCache() {}

};
}
}
