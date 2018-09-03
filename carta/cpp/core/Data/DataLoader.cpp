#include <unistd.h>

#include <QDebug>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>

#include "DataLoader.h"
#include "Util.h"
#include "Globals.h"
#include "IPlatform.h"
#include "State/UtilState.h"

#include <set>

namespace Carta {

namespace Data {

class DataLoader::Factory : public Carta::State::CartaObjectFactory {

public:

    Factory():
        CartaObjectFactory( "DataLoader" ){};

    Carta::State::CartaObject * create (const QString & path, const QString & id)
    {
        return new DataLoader (path, id);
    }
};

QString DataLoader::fakeRootDirName = "RootDirectory";
const QString DataLoader::CLASS_NAME = "DataLoader";
const QString DataLoader::DIR = "dir";
const QString DataLoader::CRTF = ".crtf";
const QString DataLoader::REG = ".reg";

bool DataLoader::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass ( CLASS_NAME,
                                                   new DataLoader::Factory());

DataLoader::DataLoader( const QString& path, const QString& id ):
    CartaObject( CLASS_NAME, path, id ){
}


QString DataLoader::getData(const QString& dirName, const QString& sessionId) {
    QString rootDirName = dirName;
    bool securityRestricted = isSecurityRestricted();
    //Just get the default if the user is trying for a directory elsewhere and
    //security is restricted.
    if ( securityRestricted && !dirName.startsWith( DataLoader::fakeRootDirName) ){
        rootDirName = "";
    }

    if ( rootDirName.length() == 0 || dirName == DataLoader::fakeRootDirName){
        if ( lastAccessedDir.length() == 0 ){
            lastAccessedDir = getRootDir(sessionId);
        }
        rootDirName = lastAccessedDir;
    }
    else {
        rootDirName = getFile( dirName, sessionId );
    }
    lastAccessedDir = rootDirName;
    QDir rootDir(rootDirName);
    QJsonObject rootObj;

    _processDirectory(rootDir, rootObj);

    if ( securityRestricted ){
        QString baseName = getRootDir( sessionId );
        QString displayName = rootDirName.replace( baseName, DataLoader::fakeRootDirName);
        rootObj.insert(Util::NAME, displayName);
    }

    QJsonDocument document(rootObj);
    QByteArray textArray = document.toJson();
    QString jsonText(textArray);
    return jsonText;
}

QString DataLoader::getFile( const QString& bogusPath, const QString& sessionId ) const {
    QString path( bogusPath );
    QString fakePath( DataLoader::fakeRootDirName );
    if( path.startsWith( fakePath )){
        QString rootDir = getRootDir( sessionId );
        QString baseRemoved = path.remove( 0, fakePath.length() );
        path = QString( "%1%2").arg( rootDir).arg( baseRemoved);
    }
    return path;
}

QString DataLoader::getRootDir(const QString& /*sessionId*/) const {
    return Globals::instance()-> platform()-> getCARTADirectory().append("Images");
}

QString DataLoader::getShortName( const QString& longName ) const {
    QString rootDir = getRootDir( "" );
    int rootLength = rootDir.length();
    QString shortName;
    if ( longName.contains( rootDir)){
        shortName = longName.right( longName.size() - rootLength - 1);
    }
    else {
        int lastSlashIndex = longName.lastIndexOf( QDir::separator() );
        if ( lastSlashIndex >= 0 ){
            shortName = longName.right( longName.size() - lastSlashIndex - 1);
        }
    }
    return shortName;
}

QStringList DataLoader::getShortNames( const QStringList& longNames ) const {
    QStringList shortNames;
    for ( int i = 0; i < longNames.size(); i++ ){
        QString shortName = getShortName( longNames[i] );
        shortNames.append( shortName );
    }
    return shortNames;
}


QString DataLoader::getLongName( const QString& shortName, const QString& sessionId ) const {
    QString longName = shortName;
    QString potentialLongName = getRootDir( sessionId) + QDir::separator() + shortName;
    QFile file( potentialLongName );
    if ( file.exists() ){
        longName = potentialLongName;
    }
    return longName;
}

// QString DataLoader::getFileList(const QString & params){

//     std::set<QString> keys = { "path" };
//     std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
//     QString dir = dataValues[*keys.begin()];
//     QString xml = getData( dir, "1" );
//     return xml;
// }

DataLoader::PBMSharedPtr DataLoader::getFileList( CARTA::FileListRequest fileListRequest ){
    std::string dir = fileListRequest.directory();
    std::shared_ptr<CARTA::FileListResponse> fileListResponse(new CARTA::FileListResponse());

    QString dirName = QString::fromStdString(dir);
    QString rootDirName = dirName;
    bool securityRestricted = isSecurityRestricted();
    //Just get the default if the user is trying for a directory elsewhere and
    //security is restricted.
    if ( securityRestricted && !dirName.startsWith( DataLoader::fakeRootDirName) ){
        rootDirName = "";
    }

    if ( rootDirName.length() == 0 || dirName == DataLoader::fakeRootDirName){
        if ( lastAccessedDir.length() == 0 ){
            lastAccessedDir = getRootDir("1");
        }
        rootDirName = lastAccessedDir;
    }
    else {
        rootDirName = getFile( dirName, "1" );
    }
    lastAccessedDir = rootDirName;
    fileListResponse->set_success(true);
    fileListResponse->set_directory(rootDirName.toStdString());

    QDir rootDir(rootDirName);

    if (!rootDir.exists()) {
        QString errorMsg = "Please check that "+rootDir.absolutePath()+" is a valid directory.";
        Util::commandPostProcess( errorMsg );
        return nullptr;
    }

    QString lastPart = rootDir.absolutePath();

    QDirIterator dit(rootDir.absolutePath(), QDir::NoFilter);
    while (dit.hasNext()) {
        dit.next();
        // skip "." and ".." entries
        if (dit.fileName() == "." || dit.fileName() == "..") {
            continue;
        }

        QString fileName = dit.fileInfo().fileName();
        if (dit.fileInfo().isDir()) {
            QString rootDirPath = rootDir.absolutePath();
            QString subDirPath = rootDirPath.append("/").append(fileName);

            if (_checkSubDir(subDirPath) == "image") {
                uint64_t fileSize = _subDirSize(subDirPath);
                CARTA::FileInfo *fileInfo = fileListResponse->add_files();
                fileInfo->set_type(CARTA::FileType::CASA);
                fileInfo->set_name(fileName.toStdString());
                fileInfo->set_size(fileSize);
                fileInfo->add_hdu_list();
            }
            else if (_checkSubDir(subDirPath) == "miriad") {
                uint64_t fileSize = _subDirSize(subDirPath);
                CARTA::FileInfo *fileInfo = fileListResponse->add_files();
                fileInfo->set_type(CARTA::FileType::MIRIAD);
                fileInfo->set_name(fileName.toStdString());
                fileInfo->set_size(fileSize);
                fileInfo->add_hdu_list();
            }
            else {
                fileListResponse->add_subdirectories(fileName.toStdString());
            }
        }
        else if (dit.fileInfo().isFile()) {
            QFile file(lastPart+QDir::separator()+fileName);
            if (file.open(QFile::ReadOnly)) {
                QString dataInfo = file.read(160);
                if (dataInfo.contains(QRegExp("^SIMPLE *= *T.* BITPIX*")) && !dataInfo.contains(QRegExp("\n"))) {
                    uint64_t fileSize = file.size();
                    CARTA::FileInfo *fileInfo = fileListResponse->add_files();
                    fileInfo->set_name(fileName.toStdString());
                    fileInfo->set_type(CARTA::FileType::FITS);
                    fileInfo->set_size(fileSize);
                    fileInfo->add_hdu_list();
                }
                file.close();
            }
        }
    }

    return fileListResponse;
}

DataLoader::PBMSharedPtr DataLoader::getFileInfo(CARTA::FileInfoRequest openFile) {

    QString fileDir = QString::fromStdString(openFile.directory());
    if (!QDir(fileDir).exists()) {
        qWarning() << "[File Info] File directory doesn't exist! (" << fileDir << ")";
        return nullptr;
    }

    QString fileName = QString::fromStdString(openFile.file());
    QString fileFullName = fileDir + "/" + fileName;

    QString file = fileFullName.trimmed();
    auto res = Globals::instance()->pluginManager()->prepare <Carta::Lib::Hooks::LoadAstroImage>(file).first();
    std::shared_ptr<Carta::Lib::Image::ImageInterface> image;
    if (!res.isNull()) {
        image = res.val();
    } else {
        qWarning() << "[File Info] Can not open the image file! (" << file << ")";
        return nullptr;
    }

    // Since the statistical plugin requires a vector of ImageInterface
    std::vector<std::shared_ptr<Carta::Lib::Image::ImageInterface> > images;
    images.push_back(image);

    CARTA::FileInfo* fileInfo = new CARTA::FileInfo();

    // FileInfo: name
    fileInfo->set_name(openFile.file());

    // FileInfo: type
    if (image->getType() == "FITSImage") {
        fileInfo->set_type(CARTA::FileType::FITS);
    } else {
        fileInfo->set_type(CARTA::FileType::CASA);
    }
    // fileInfo->add_hdu_list(openFile.hdu());

    // FileInfoExtended part 1: add extended information
    const std::vector<int> dims = image->dims();
    CARTA::FileInfoExtended* fileInfoExt = new CARTA::FileInfoExtended();
    fileInfoExt->set_dimensions(dims.size());
    fileInfoExt->set_width(dims[0]);
    fileInfoExt->set_height(dims[1]);

    // it may be not really the spectral axis, but we can regardless of it so far.
    if (dims.size() >= 3) {
        fileInfoExt->set_depth(dims[2]);
    }

    // it may be not really the stoke axis, but we can regardless of it so far.
    if (dims.size() >= 4) {
        fileInfoExt->set_stokes(dims[3]);
    }

    // Prepare to use the ImageStats plugin.
    std::vector<std::shared_ptr<Carta::Lib::Regions::RegionBase> > regions; // regions is an empty setting so far
    int dimSize = image->dims().size(); // get the dimension of the image
    std::vector<int> frameIndices(dimSize, -1); // get the statistical data of the whole image

    int sourceCount = images.size();
    if (sourceCount > 0) {
        auto result = Globals::instance()->pluginManager()
                -> prepare <Carta::Lib::Hooks::ImageStatisticsHook>(images, regions, frameIndices);

        auto lam = [=] (const Carta::Lib::Hooks::ImageStatisticsHook::ResultType &data) {
            //An array for each image
            int dataCount = data.size();

            for ( int i = 0; i < dataCount; i++ ) {
                // Each element of the image array contains an array of statistics.
                int statCount = data[i].size();

                // Go through each set of statistics for the image.
                for (int k = 0; k < statCount; k++) {
                    int keyCount = data[i][k].size();

                    for (int j = 0; j < keyCount; j++) {
                        QString label = data[i][k][j].getLabel();
                        QString value = data[i][k][j].getValue();
                        CARTA::HeaderEntry* headerEntry = fileInfoExt->add_header_entries();
                        headerEntry->set_name(label.toLocal8Bit().constData());
                        headerEntry->set_value(value.toLocal8Bit().constData());
                    }
                }
            }
        };

        try {
            result.forEach( lam );
        } catch (char*& error) {
            QString errorStr(error);
            qDebug() << "[File Info] There is an error message: " << errorStr;
        }
    }

    QString respName = "FILE_INFO_RESPONSE";

    // FileInfoExtended part 2: extract certain entries, such as NAXIS NAXIS1 NAXIS2 NAXIS3...etc, for showing in file browser
    if (false == extractFitsInfo(fileInfoExt, image, respName)) {
        qDebug() << "[File Info] Extract FileInfoExtended part 2 error.";
    }

    // FileInfoResponse
    std::shared_ptr<CARTA::FileInfoResponse> fileInfoResponse(new CARTA::FileInfoResponse());
    fileInfoResponse->set_success(true);
    fileInfoResponse->set_allocated_file_info(fileInfo);
    fileInfoResponse->set_allocated_file_info_extended(fileInfoExt);

    return fileInfoResponse;
}

// FileInfoExtended: extract Fits information and add to entries, including NAXIS NAXIS1 NAXIS2 NAXIS3...etc
// The fits header structure is like:
//  NAXIS1 = 1024
//  CTYPE1 = 'RA---TAN'
//  CDELT1 = -9.722222222222E-07
//   ...etc
bool DataLoader::extractFitsInfo(CARTA::FileInfoExtended* fileInfoExt,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image,
                                 const QString respond) {
    // validate parameters
    if (nullptr == fileInfoExt || nullptr == image) {
        return false;
    }

    // get fits header map using FitsHeaderExtractor
    FitsHeaderExtractor fhExtractor;
    fhExtractor.setInput(image);
    std::map<QString, QString> headerMap = fhExtractor.getHeaderMap();

    // extract necessary info from header, depending on the respond event
    if ("FILE_INFO_RESPONSE" == respond) {
        // extract only certain entries from fits header for showing in File browser
        std::vector<QString> keys {"NAXIS", "NAXIS1", "NAXIS2", "NAXIS3",
                                   "BMAJ", "BMIN", "BPA", "BUNIT",
                                   "EQUINOX", "RADESYS", "SPECSYS", "VELREF",
                                   "CTYPE1", "CRVAL1", "CDELT1", "CUNIT1",
                                   "CTYPE2", "CRVAL2", "CDELT2", "CUNIT2",
                                   "CTYPE3", "CRVAL3", "CDELT3", "CUNIT3",
                                   "CTYPE4", "CRVAL4", "CDELT4", "CUNIT4"};
        for (auto key = keys.begin(); key != keys.end(); key++) {
            // find value corresponding to key
            QString value = "";
            auto found = headerMap.find(*key);
            if (found != headerMap.end()) {
                value = found->second;
            }

            // insert (key, value) to header entry
            CARTA::HeaderEntry* headerEntry = fileInfoExt->add_header_entries();
            if (nullptr == headerEntry) {
                qDebug() << "Add header entry error in FILE_INFO_RESPONSE.";
                return false;
            }
            headerEntry->set_name((*key).toLocal8Bit().constData());
            headerEntry->set_value(value.toLocal8Bit().constData());
        }
    } else if ("OPEN_FILE_ACK" == respond) {
        // traverse whole map to return all entries for frontend to render (AST)
        for (auto iter = headerMap.begin(); iter != headerMap.end(); iter++) {
            CARTA::HeaderEntry* headerEntry = fileInfoExt->add_header_entries();
            if (nullptr == headerEntry) {
                qDebug() << "Add header entry error in OPEN_FILE_ACK.";
                return false;
            }
            headerEntry->set_name((iter->first).toLocal8Bit().constData());
            headerEntry->set_value((iter->second).toLocal8Bit().constData());
        }
    } else {
        return false;
    }

    return true;
}

void DataLoader::_initCallbacks(){

    //Callback for returning a list of data files that can be loaded.
//    addCommandCallback( "getData", [=] (const QString & /*cmd*/,
//            const QString & params, const QString & sessionId) -> QString {
//        std::set<QString> keys = { "path" };
//        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
//        QString dir = dataValues[*keys.begin()];
//        QString xml = getData( dir, sessionId );
//        return xml;
//    });

//    addCommandCallback( "isSecurityRestricted", [=] (const QString & /*cmd*/,
//                const QString & /*params*/, const QString & /*sessionId*/) -> QString {
//            bool securityRestricted = isSecurityRestricted();
//            QString result = "false";
//            if ( securityRestricted ){
//                result = true;
//            }
//            return result;
//        });
}

bool DataLoader::isSecurityRestricted() const {
    bool securityRestricted = Globals::instance()-> platform()-> isSecurityRestricted();
    return securityRestricted;
}

void DataLoader::_processDirectory(const QDir& rootDir, QJsonObject& rootObj) const {

    if (!rootDir.exists()) {
        QString errorMsg = "Please check that "+rootDir.absolutePath()+" is a valid directory.";
        Util::commandPostProcess( errorMsg );
        return;
    }

    QString lastPart = rootDir.absolutePath();
    rootObj.insert( Util::NAME, lastPart );

    QJsonArray dirArray;
    QDirIterator dit(rootDir.absolutePath(), QDir::NoFilter);
    while (dit.hasNext()) {
        dit.next();
        // skip "." and ".." entries
        if (dit.fileName() == "." || dit.fileName() == "..") {
            continue;
        }

        QString fileName = dit.fileInfo().fileName();
        if (dit.fileInfo().isDir()) {
            QString rootDirPath = rootDir.absolutePath();
            QString subDirPath = rootDirPath.append("/").append(fileName);

            if ( !_checkSubDir(subDirPath).isNull() ) {
                _makeFileNode( dirArray, fileName, _checkSubDir(subDirPath));
            }
            else {
                _makeFolderNode( dirArray, fileName );
            }
        }
        else if (dit.fileInfo().isFile()) {
            QFile file(lastPart+QDir::separator()+fileName);
            if (file.open(QFile::ReadOnly)) {
                QString dataInfo = file.read(160);
                if (dataInfo.contains("Region", Qt::CaseInsensitive)) {
                    if (dataInfo.contains("DS9", Qt::CaseInsensitive)) {
                        _makeFileNode(dirArray, fileName, "reg");
                    }
                    else if (dataInfo.contains("CRTF", Qt::CaseInsensitive)) {
                        _makeFileNode(dirArray, fileName, "crtf");
                    }
                }
                else if (dataInfo.contains(QRegExp("^SIMPLE *= *T.* BITPIX*")) && !dataInfo.contains(QRegExp("\n"))) {
                    _makeFileNode(dirArray, fileName, "fits");
                }
                file.close();
            }
        }
    }

    rootObj.insert( DIR, dirArray);
}

QString DataLoader::_checkSubDir( QString& subDirPath) const {

    QDir subDir(subDirPath);
    if (!subDir.exists()) {
        QString errorMsg = "Please check that "+subDir.absolutePath()+" is a valid directory.";
        Util::commandPostProcess( errorMsg );
        exit(0);
    }

    QMap< QString, QStringList> filterMap;

    QStringList imageFilters, miriadFilters;
    imageFilters << "table.f0_TSM0" << "table.info";
    miriadFilters << "header" << "image";

    filterMap.insert( "image", imageFilters);
    filterMap.insert( "miriad", miriadFilters);

    //look for the subfiles satisfying a special format
    foreach ( const QString &filter, filterMap.keys()){
        subDir.setNameFilters(filterMap.value(filter));
        if ( subDir.entryList().length() == filterMap.value(filter).length() ) {
            return filter;
        }
    }
    return NULL;
}

uint64_t DataLoader::_subDirSize(const QString &subDirPath) const {
    uint64_t totalSize = 0;
    QFileInfo str_info(subDirPath);
     if (str_info.isDir()) {
        QDir dir(subDirPath);
        dir.setFilter(QDir::Files | QDir::Dirs |  QDir::Hidden | QDir::NoSymLinks);
        QFileInfoList list = dir.entryInfoList();
         for (int i = 0; i < list.size(); i++) {
            QFileInfo fileInfo = list.at(i);
            if ((fileInfo.fileName() != ".") && (fileInfo.fileName() != "..")) {
                totalSize += (fileInfo.isDir()) ? this->_subDirSize(fileInfo.filePath()) : fileInfo.size();
            }
        }
    }
    return totalSize;
}

void DataLoader::_makeFileNode(QJsonArray& parentArray, const QString& fileName, const QString& fileType) const {
    QJsonObject obj;
    QJsonValue fileValue(fileName);
    obj.insert( Util::NAME, fileValue);
    //use type to represent the format of files
    //the meaning of "type" may differ with other codes
    //can change the string when feeling confused
    obj.insert( Util::TYPE, QJsonValue(fileType));
    parentArray.append(obj);
}

void DataLoader::_makeFolderNode( QJsonArray& parentArray, const QString& fileName ) const {
    QJsonObject obj;
    QJsonValue fileValue(fileName);
    obj.insert( Util::NAME, fileValue);
    QJsonArray arry;
    obj.insert(DIR, arry);
    parentArray.append(obj);
}



DataLoader::~DataLoader(){
}
}
}
