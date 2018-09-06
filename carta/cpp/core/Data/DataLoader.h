/***
 * Returns Json representing a directory tree of eligible data that can be loaded
 * from a root directory.
 */

#pragma once

#include <State/ObjectManager.h>

#include <QDir>
#include <QJsonArray>
#include <memory>

#include "FitsHeaderExtractor.h"

#include "CartaLib/Proto/file_info.pb.h"
#include "CartaLib/Proto/file_list.pb.h"
#include "CartaLib/Proto/defs.pb.h"
#include "CartaLib/Proto/enums.pb.h"

// File_Info implementation needs
#include "CartaLib/Hooks/LoadAstroImage.h"
#include "CartaLib/Hooks/ImageStatisticsHook.h"
#include "Globals.h"

namespace Carta {

namespace Data {

class DataLoader : public Carta::State::CartaObject {

public:

    // QString getFileList(const QString & params);
    PBMSharedPtr getFileList( CARTA::FileListRequest fileListRequest);

    PBMSharedPtr getFileInfo(CARTA::FileInfoRequest fileInfoRequest);

    // FileInfoExtended: extract Fits information and add to entries, including NAXIS NAXIS1 NAXIS2 NAXIS3...etc
    bool extractFitsInfo(CARTA::FileInfoExtended* fileInfoExt,
                         const std::shared_ptr<Carta::Lib::Image::ImageInterface> image,
                         const QString respond);

    /**
     * Returns a QString containing a hierarchical listing of data files that can
     * be loaded.
     * @param selectionParams a filter for choosing specific types of data files.
     * @param sessionId the user's session identifier that may be eventually used to determine
     *        a search directory or URL for files.
     */
    QString getData(const QString& selectionParams,
                    const QString& sessionId);

    /**
     * Returns the name of the file corresponding to the doctored path and session identifier.
     * @param fakePath a QString identifying a file.
     * @param sessionId an identifier for the session.
     * @return the actual path for the file.
     */
    QString getFile( const QString& fakePath, const QString& sessionId ) const;

    /**
     * Return the absolute path of the file with the root directories stripped off.
     * @param sessionId - an identifier for the user's session.
     * @param shortName - a path with the top level directory stripped off.
     * @return - the full path to the file.
     */
    QString getLongName( const QString& shortName, const QString& sessionId ) const;

    /**
     * Return the top level directory for the data file search.
     * @param sessionId - an identifier for the user's session.
     * @return the absolute path to the directory containing the user's files.
     */
    QString getRootDir(const QString& sessionId) const;

    /**
     * Strips the top level directory from the file name and returns the remainder.
     * @param longName- absolute path to a file.
     * @return - the last part of the absolute path.
     */
    QString getShortName( const QString& longName ) const;

    /**
     * Strips the top level directory from the list of file names and
     * returns them.
     * @param longNames - a list of absolute file path locations.
     * @return the file names with the root directory stripped off.
     */
    QStringList getShortNames( const QStringList& longNames ) const;

    /**
     * Returns whether or not the user has full access to the file system.
     * @return true - if there are security restrictions with regard to acessing
     *      file information; false otherwise.
     */
    bool isSecurityRestricted() const;

    static QString fakeRootDirName;
    const static QString CLASS_NAME;
    const static QString CRTF;
    const static QString REG;

    virtual ~DataLoader();

private:

    static bool m_registered;

    QString lastAccessedDir = "";

    class Factory;

    const static QString DIR;

    void _initCallbacks();

    //Look for eligible data files in a specific directory (recursive).
    void _processDirectory(const QDir& rootDir, QJsonObject& rootArray) const;

    //Check whether the dir is casa or miriad format, others are null
    //return the format in json to set icons
    QString _checkSubDir( QString& subDirPath) const;

    // calculate the total size of sub-dir
    uint64_t _subDirSize(const QString& subDirPath) const;

    //Add a file to the list of those available in a given directory.
    void _makeFileNode(QJsonArray& parentArray, const QString& fileName, const QString& fileType) const;
    //Add a subdirectory to the list of available files.
    void _makeFolderNode( QJsonArray& parentArray, const QString& fileName ) const;
    DataLoader( const QString& path, const QString& id);
    DataLoader( const DataLoader& other);
    DataLoader& operator=( const DataLoader& other );
};
}
}
