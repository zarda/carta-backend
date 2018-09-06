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

using Carta::Lib::AxisInfo;

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
        QString errorMsg = "Please check that " + rootDir.absolutePath() + " is a valid directory.";
        Util::commandPostProcess( errorMsg );
        return nullptr;
    }

    // get the carta root path
    QString cartaRootPath = QDir::homePath() + "/CARTA";

    QDir rootDirCdUp(rootDirName);
    bool exist = rootDirCdUp.cdUp();

    // users can not access the directory upper to the carta root path
    if (exist && rootDirCdUp.path() != cartaRootPath) {
        fileListResponse->set_parent(rootDirCdUp.path().toStdString());
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

DataLoader::PBMSharedPtr DataLoader::getFileInfo(CARTA::FileInfoRequest fileInfoRequest) {

    QString fileDir = QString::fromStdString(fileInfoRequest.directory());
    if (!QDir(fileDir).exists()) {
        qWarning() << "[File Info] File directory doesn't exist! (" << fileDir << ")";
        return nullptr;
    }

    QString fileName = QString::fromStdString(fileInfoRequest.file());
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

    // FileInfo: set name & type
    CARTA::FileInfo* fileInfo = new CARTA::FileInfo();
    fileInfo->set_name(fileInfoRequest.file());
    if (image->getType() == "FITSImage") {
        fileInfo->set_type(CARTA::FileType::FITS);
    } else {
        fileInfo->set_type(CARTA::FileType::CASA);
    }

    // FileInfoExtended init: set dimensions, width, height
    const std::vector<int> dims = image->dims();
    CARTA::FileInfoExtended* fileInfoExt = new CARTA::FileInfoExtended();
    fileInfoExt->set_dimensions(dims.size());
    fileInfoExt->set_width(dims[0]);
    fileInfoExt->set_height(dims[1]);

    // set the stoke axis if it exists
    int stokeIndicator = Util::getAxisIndex(image, AxisInfo::KnownType::STOKES);
    if (stokeIndicator > 0) { // if stoke axis exists
        if (dims[stokeIndicator] > 0) { // if stoke dimension > 0
            fileInfoExt->set_stokes(dims[stokeIndicator]);
        }
    }

    // for the dims[k] that is not the stoke frame nor the x- or y-axis,
    // we assume it is a depth (it is the Spectral axis or the other unmarked axis)
    if (dims.size() > 2) {
        for (int i = 2; i < dims.size(); i++) {
            if (i != stokeIndicator && dims[i] > 0) {
                fileInfoExt->set_depth(dims[i]);
                break;
            }
        }
    }

    // FileInfoExtended part 1: get statistic informtion using ImageStats plugin
    if (false == _getStatisticInfo(fileInfoExt, image)) {
        qDebug() << "[File Info] Get statistic informtion error.";
    }

    // FileInfoExtended part 2: generate some customized information
    if (false == _genCustomizedInfo(fileInfoExt, image)) {
        qDebug() << "[File Info] Generate file information error.";
    }

    // FileInfoResponse
    std::shared_ptr<CARTA::FileInfoResponse> fileInfoResponse(new CARTA::FileInfoResponse());
    fileInfoResponse->set_success(true);
    fileInfoResponse->set_allocated_file_info(fileInfo);
    fileInfoResponse->set_allocated_file_info_extended(fileInfoExt);

    return fileInfoResponse;
}

// Get all fits headers and insert to header entry
bool DataLoader::getFitsHeaders(CARTA::FileInfoExtended* fileInfoExt,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image) {
    // validate parameters
    if (nullptr == fileInfoExt || nullptr == image) {
        return false;
    }

    // get fits header map using FitsHeaderExtractor
    FitsHeaderExtractor fhExtractor;
    fhExtractor.setInput(image);
    std::map<QString, QString> headerMap = fhExtractor.getHeaderMap();
    
    // traverse whole map to return all entries for frontend to render (AST)
    for (auto iter = headerMap.begin(); iter != headerMap.end(); iter++) {
        if (false == _insertHeaderEntry(fileInfoExt, iter->first, iter->second)) {
            qDebug() << "Insert (" << iter->first << ", " << iter->second << ") to header entry error.";
            return false;
        }
    }

    return true;
}

// Get statistic informtion using ImageStats plugin
bool DataLoader::_getStatisticInfo(CARTA::FileInfoExtended* fileInfoExt,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image) {
    // validate parameters
    if (nullptr == fileInfoExt || nullptr == image) {
        return false;
    }

    // the statistical plugin requires a vector of ImageInterface
    std::vector<std::shared_ptr<Carta::Lib::Image::ImageInterface>> images;
    images.push_back(image);
    
    // regions is an empty setting so far
    std::vector<std::shared_ptr<Carta::Lib::Regions::RegionBase>> regions;
    
    // get the statistical data of the whole image
    std::vector<int> frameIndices(image->dims().size(), -1);

    if (images.size() > 0) { // [TODO]: do we really need this if statement?
        // Prepare to use the ImageStats plugin.
        auto result = Globals::instance()->pluginManager()
                -> prepare <Carta::Lib::Hooks::ImageStatisticsHook>(images, regions, frameIndices);

        // lamda function for traverse
        auto lam = [=] (const Carta::Lib::Hooks::ImageStatisticsHook::ResultType &data) {
            //An array for each image
            for ( int i = 0; i < data.size(); i++ ) {
                // Each element of the image array contains an array of statistics.
                int statCount = data[i].size();

                // Go through each set of statistics for the image.
                for (int k = 0; k < statCount; k++) {
                    int keyCount = data[i][k].size();

                    for (int j = 0; j < keyCount; j++) {
                        QString label = "- " + data[i][k][j].getLabel();
                        QString value = data[i][k][j].getValue();
                        if (false == _insertHeaderEntry(fileInfoExt, label, value))
                            qDebug() << "Insert (" << label << ", " << value << ") to header entry error.";
                    }
                }
            }
        };

        try {
            result.forEach(lam);
        } catch (char*& error) {
            QString errorStr(error);
            qDebug() << "[File Info] There is an error message: " << errorStr;
            return false;
        }
    }

    return true;
}

// Generate customized file information for human readiblity using some fits headers
bool DataLoader::_genCustomizedInfo(CARTA::FileInfoExtended* fileInfoExt,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image) {
    // validate parameters
    if (nullptr == fileInfoExt || nullptr == image) {
        return false;
    }

    // get fits header map using FitsHeaderExtractor
    FitsHeaderExtractor fhExtractor;
    fhExtractor.setInput(image);
    std::map<QString, QString> headerMap = fhExtractor.getHeaderMap();

    // extract necessary info from header, depending on the respond event
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
        if (false == _insertHeaderEntry(fileInfoExt, *key, value)) {
            qDebug() << "Insert (" << *key << ", " << value << ") to header entry error.";
            return false;
        }
    }

    return true;
}

//  Insert fits header to header entry, the fits structure is like:
//  NAXIS1 = 1024
//  CTYPE1 = 'RA---TAN'
//  CDELT1 = -9.722222222222E-07
//   ...etc
bool DataLoader::_insertHeaderEntry(CARTA::FileInfoExtended* fileInfoExt, const QString key, const QString value) {
    // validate parameters
    if (nullptr == fileInfoExt) {
        return false;
    }

    // insert (key, value) to header entry
    CARTA::HeaderEntry* headerEntry = fileInfoExt->add_header_entries();
    if (nullptr == headerEntry) {
        return false;
    }
    headerEntry->set_name(key.toLocal8Bit().constData());
    headerEntry->set_value(value.toLocal8Bit().constData());

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
