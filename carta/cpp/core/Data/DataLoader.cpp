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
#include <math.h>

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
    std::shared_ptr<CARTA::FileInfoResponse> fileInfoResponse(new CARTA::FileInfoResponse());

    QString fileDir = QString::fromStdString(fileInfoRequest.directory());
    if (!QDir(fileDir).exists()) {
        QString message = "[File Info] File directory doesn't exist! (" + fileDir + ")";
        qWarning() << message;
        fileInfoResponse->set_success(false);
        fileInfoResponse->set_message(message.toStdString());
        return fileInfoResponse;
    }

    QString fileName = QString::fromStdString(fileInfoRequest.file());
    QString fileFullName = fileDir + "/" + fileName;

    QString file = fileFullName.trimmed();
    auto res = Globals::instance()->pluginManager()->prepare<Carta::Lib::Hooks::LoadAstroImage>(file).first();
    std::shared_ptr<Carta::Lib::Image::ImageInterface> image;
    if (!res.isNull()) {
        image = res.val();
    } else {
        QString message = "[File Info] Can not open the image file! (" + file + ")";
        qWarning() << message;
        fileInfoResponse->set_success(false);
        fileInfoResponse->set_message(message.toStdString());
        return fileInfoResponse;
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

    // generate information and insert to fileInfoExt
    // Part 1: get statistic information using ImageStats plugin
    // Part 2: generate some customized information
    std::map<QString, QString> infoMap = {};
    if (false == _getStatisticInfo(infoMap, image)) {
        qDebug() << "[File Info] Get statistic informtion error.";
    }  
    if (false == _genCustomizedInfo(infoMap, image)) {
        qDebug() << "[File Info] Generate file information error.";
    }

    // customized arrange file info into an array[] of {key, value}
    std::vector<std::vector<QString>> pairs = {};
    if (false == _arrangeFileInfo(infoMap, pairs)) {
        qDebug() << "Sort file info entry error.";
    } else {
        // insert Part 1, Part 2 to fileInfoExt
        for (auto iter = pairs.begin(); iter != pairs.end(); iter++) {
            auto *infoEntries = fileInfoExt->add_computed_entries();
            if (nullptr != infoEntries) {
                infoEntries->set_name((*iter)[0].toLocal8Bit().constData());
                infoEntries->set_value((*iter)[1].toLocal8Bit().constData());
            } else {
                qDebug() << "Insert info entry to fileInfoExt error.";
            }
        }
    }

    // Part 3: add all fits headers to fileInfoExt
    if (false == getFitsHeaders(fileInfoExt, image)) {
        qDebug() << "[File Info] Get fits headers error!";
    }

    // FileInfoResponse
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
    std::vector<std::vector<QString>> headerList = fhExtractor.getHeaderList();
    
    // traverse whole map to return all entries for frontend to render (AST)
    for (auto iter = headerList.begin(); iter != headerList.end(); iter++) {
        // insert (key, value) to header entry
        CARTA::HeaderEntry* headerEntry = fileInfoExt->add_header_entries();
        if ((*iter).size() != 2 || nullptr == headerEntry) {
            qDebug() << "Insert header to header entry error.";
            return false;
        }
        headerEntry->set_name((*iter)[0].toLocal8Bit().constData());
        headerEntry->set_value((*iter)[1].toLocal8Bit().constData());
    }

    return true;
}

// Get statistic informtion using ImageStats plugin
bool DataLoader::_getStatisticInfo(std::map<QString, QString>& infoMap,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image) {
    // validate parameter
    if (nullptr == image) {
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
        auto lam = [&] (const Carta::Lib::Hooks::ImageStatisticsHook::ResultType &data) {
            //An array for each image
            for (int i = 0; i < data.size(); i++) {
                // Each element of the image array contains an array of statistics.
                // Go through each set of statistics for the image.
                for (int j = 0; j < data[i].size(); j++) {
                    for (int k = 0; k < data[i][j].size(); k++) {
                        infoMap[data[i][j][k].getLabel()] = data[i][j][k].getValue();
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

// Generate customized file information for human readiblity by using some fits headers
bool DataLoader::_genCustomizedInfo(std::map<QString, QString>& infoMap,
                                 const std::shared_ptr<Carta::Lib::Image::ImageInterface> image) {
    // validate parameter
    if (nullptr == image) {
        return false;
    }

    // get fits header map using FitsHeaderExtractor
    FitsHeaderExtractor fhExtractor;
    fhExtractor.setInput(image);
    std::map<QString, QString> headerMap = fhExtractor.getHeaderMap();

    // generate customized info 1~6
    // 1. Generate customized stokes + channels info & insert to entry
    if (false == _genStokesChannelsInfo(infoMap, headerMap)) {
        qDebug() << "Generate stokes + channels info & insert to entry failed.";
    }

    // 2. Generate customized pixel size info & insert to entry
    if (false == _genPixelSizeInfo(infoMap, headerMap)) {
        qDebug() << "Generate pixel size info & insert to entry failed.";
    }

    // 3. Generate customized coordinate type info & insert to entry
    if (false == _genCoordTypeInfo(infoMap, headerMap)) {
        qDebug() << "Generate coordinate type info & insert to entry failed.";
    }

    // 4. Generate customized image reference coordinate info & insert to entry
    if (false == _genImgRefCoordInfo(infoMap, headerMap)) {
        qDebug() << "Generate image reference coordinate info & insert to entry failed.";
    }

    // 5. Generate customized celestial frame info & insert to entry
    if (false == _genCelestialFrameInfo(infoMap, headerMap)) {
        qDebug() << "Generate celestial frame info & insert to entry failed.";
    }

    // 6. Generate customized velocity definition info & insert to entry
    if (false == _genRemainInfo(infoMap, headerMap)) {
        qDebug() << "Generate velocity definition info & insert to entry failed.";
    }    

    return true;
}

// Generate customized stokes & channels info according to CTYPE3, CTYPE4, NAXIS3, NAXIS4
bool DataLoader::_genStokesChannelsInfo(std::map<QString, QString>& infoMap,
                                   const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }

    auto naxis = headerMap.find("NAXIS");
    if (naxis == headerMap.end()) {
        qDebug() << "Cannot find NAXIS.";
        return false;
    }

    bool ok = false;
    double n = (naxis->second).toDouble(&ok);
    if (ok && n > 2) {
        // find CTYPE3, CTYPE4
        auto ctype3 = headerMap.find("CTYPE3");
        auto ctype4 = headerMap.find("CTYPE4");
        QString stokes = "NA", channels = "NA";

        // check CTYPE3, CTYPE4 to determine stokes
        if (ctype3 != headerMap.end() && (ctype3->second).contains("STOKES", Qt::CaseInsensitive)) {
            auto naxis3 = headerMap.find("NAXIS3");
            if (naxis3 == headerMap.end()) {
                qDebug() << "Cannot find NAXIS3.";
                return false;
            }
            stokes = naxis3->second;
        } else if (ctype4 != headerMap.end() && (ctype4->second).contains("STOKES", Qt::CaseInsensitive)) {
            auto naxis4 = headerMap.find("NAXIS4");
            if (naxis4 == headerMap.end()) {
                qDebug() << "Cannot find NAXIS4.";
                return false;
            }
            stokes = naxis4->second;
        }

        // check CTYPE3, CTYPE4 to determine channels
        // [TODO]: regular expression should be case insensitive
        if (ctype3 != headerMap.end() && (ctype3->second).contains(QRegExp("VOPT|FREQ"))) {
            auto naxis3 = headerMap.find("NAXIS3");
            if (naxis3 == headerMap.end()) {
                qDebug() << "Cannot find NAXIS3.";
                return false;
            }
            channels = naxis3->second;
        } else if (ctype4 != headerMap.end() && (ctype4->second).contains(QRegExp("VOPT|FREQ"))) {
            auto naxis4 = headerMap.find("NAXIS4");
            if (naxis4 == headerMap.end()) {
                qDebug() << "Cannot find NAXIS4.";
                return false;
            }
            channels = naxis4->second;
        }

        // insert stokes, channels to info entry if they are not "NA"
        if ("NA" != stokes) {
            infoMap["Number of Stokes"] = stokes;
        }
        if ("NA" != channels) {
            infoMap["Number of Channels"] = channels;
        }
    }

    return true;
}

// Generate customized pixel size info according to CDELT1, CDELT2, CUNIT1, CUNIT2
bool DataLoader::_genPixelSizeInfo(std::map<QString, QString>& infoMap,
                                   const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }

    // check whether corresponding fields can be found in map
    auto cdelt1 = headerMap.find("CDELT1");
    auto cdelt2 = headerMap.find("CDELT2");
    auto unit1 = headerMap.find("CUNIT1");
    auto unit2 = headerMap.find("CUNIT2");
    if (cdelt1 == headerMap.end() || cdelt2 == headerMap.end() ||
        unit1 == headerMap.end() || unit2 == headerMap.end()) {
        qDebug() << "Cannot find CDELT1 CDELT2 CUNIT1 CUNIT2.";
        return false;
    }

    // unit conversion
    QString str1 = _unitConversion(cdelt1->second, unit1->second);
    QString str2 = _unitConversion(cdelt2->second, unit2->second);

    // insert (label, value) to info entry
    infoMap["Pixel size"] = str1 + ", " + str2;

    return true;
}

// Generate customized coordinate type info according to CTYPE1, CTYPE2
bool DataLoader::_genCoordTypeInfo(std::map<QString, QString>& infoMap,
                                   const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }

    auto ctype1 = headerMap.find("CTYPE1");
    auto ctype2 = headerMap.find("CTYPE2");
    if (ctype1 == headerMap.end() || ctype2 == headerMap.end()) {
        qDebug() << "Cannot find CTYPE1 CTYPE2.";
        return false;
    }

    // coordinate should be [RA, DEC], [GLON, GLAT], [others]
    QString c1Str = ctype1->second;
    QString c2Str = ctype2->second;
    if (c1Str.contains("RA", Qt::CaseInsensitive) && c2Str.contains("DEC", Qt::CaseInsensitive)) {
        infoMap["Coordinate type"] = "RA, DEC";

        // find projection, "RA---SIN", projection = SIN
        QStringList list = c1Str.split(QRegExp("[\-]+"));
        if (list.size() > 1) {
            infoMap["Projection"] = list[1];
        }
    } else if (c1Str.contains("GLON", Qt::CaseInsensitive) && c2Str.contains("GLAT", Qt::CaseInsensitive)) {
        infoMap["Coordinate type"] = "GLON, GLAT";

        // find projection, "GLON---SIN", projection = SIN
        QStringList list = c1Str.split(QRegExp("[\-]+"));
        if (list.size() > 1) {
            infoMap["Projection"] = list[1];
        }
    } else {
        infoMap["Coordinate type"] = c1Str + ", " + c2Str;
    }

    return true;
}

// Generate customized image reference coordinate info according to CRPIX1, CRPIX2, CRVAL1, CRVAL2, CUNIT1, CUNIT2
bool DataLoader::_genImgRefCoordInfo(std::map<QString, QString>& infoMap,
                                    const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }

    auto crpix1 = headerMap.find("CRPIX1");
    auto crpix2 = headerMap.find("CRPIX2");
    auto crval1 = headerMap.find("CRVAL1");
    auto crval2 = headerMap.find("CRVAL2");
    auto cunit1 = headerMap.find("CUNIT1");
    auto cunit2 = headerMap.find("CUNIT2");

    // check whether corresponding fields can be found in map
    if (crpix1 == headerMap.end() || crpix2 == headerMap.end() ||
        crval1 == headerMap.end() || crval2 == headerMap.end() ||
        cunit1 == headerMap.end() || cunit2 == headerMap.end()) {
        qDebug() << "Cannot find CRPIX1 CRPIX2 CRVAL1 CRVAL2 CUNIT1 CUNIT2.";
        return false;
    }
    
    // convert CRPIX1, CRPIX2, CRVAL1, CRVAL2 to numeric value
    bool ok = false;
    double p1 = (crpix1->second).toDouble(&ok);
    if(!ok) {qDebug() << "Convert CRPIX1 to double error."; return false;}
    double p2 = (crpix2->second).toDouble(&ok);
    if(!ok) {qDebug() << "Convert CRPIX2 to double error."; return false;}
    double v1 = (crval1->second).toDouble(&ok);
    if(!ok) {qDebug() << "Convert CRVAL1 to double error."; return false;}
    double v2 = (crval2->second).toDouble(&ok);
    if(!ok) {qDebug() << "Convert CRVAL2 to double error."; return false;}

    // set CRPIX1, CRPIX2, CRVAL1, CRVAL2 with specific format
    char buf[512];
    snprintf(buf, sizeof(buf), "%d", (int)p1);
    QString pix1Str = QString(buf);
    snprintf(buf, sizeof(buf), "%d", (int)p2);
    QString pix2Str = QString(buf);

    QString val1Str = "";
    QString val2Str = "";
    // convert to MHz if unit is Hz
    if ((cunit1->second).contains("Hz", Qt::CaseInsensitive)) {
        val1Str = _convertHz(v1);
    } else {
        snprintf(buf, sizeof(buf), "%.4f", v1);
        val1Str = QString(buf) + " " + cunit1->second;
    }
    if ((cunit2->second).contains("Hz", Qt::CaseInsensitive)) {
        val2Str = _convertHz(v2);
    } else {
        snprintf(buf, sizeof(buf), "%.4f", v2);
        val2Str = QString(buf) + " " + cunit2->second;
    }

    // generate arcsec if unit is degree
    QString arcsecStr = "";
    if ((cunit1->second).contains("deg", Qt::CaseInsensitive)) {
        char buf[512];
        QString tmp1, tmp2;

        int ra_hh = (int)(v1 / 15.0);
        int ra_mm = (int)((v1 / 15.0 - ra_hh) * 60);
        double ra_ss = (((v1 / 15.0 - ra_hh) * 60) - ra_mm) * 60;
        snprintf(buf, sizeof(buf), "%d:%d:%.4f", ra_hh, ra_mm, ra_ss);
        tmp1 = QString(buf);

        int dec_dd = (int)v2;
        int dec_mm = (int)((v2 - dec_dd) * 60);
        double dec_ss = (((v2 - dec_dd) * 60) - dec_mm) * 60;
        snprintf(buf, sizeof(buf), "%d:%d:%.4f", dec_dd, abs(dec_mm), fabs(dec_ss));
        tmp2 = QString(buf);

        arcsecStr = " [" + tmp1 + ", " + tmp2 + "]";
    }

    // insert (label, value) to info entry
    QString value = "[" + pix1Str + ", " + pix2Str + "] [" +
                    val1Str + ", " + val2Str + "]" +
                    arcsecStr;
    infoMap["Image reference coordinate"] = value;

    return true;
}

// Generate customized celestial frame according to RADESYS, EQUINOX
bool DataLoader::_genCelestialFrameInfo(std::map<QString, QString>& infoMap,
                                        const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }

    // find RADESYS
    auto radesys = headerMap.find("RADESYS");
    if (radesys == headerMap.end()) {
        qDebug() << "Cannot find RADESYS.";
        return false;
    }
    QString rad = radesys->second;

    // find EQUINOX
    auto equinox = headerMap.find("EQUINOX");
    if (equinox != headerMap.end()) {
        QString equ = equinox->second;

        // FK4 => B1950, FK5 => J2000, others => not modified
        if (rad.contains("FK4", Qt::CaseInsensitive)) {
            bool ok = false;
            int e = equ.toDouble(&ok);
            if (!ok) {return false;}
            rad += (", B" + QString(std::to_string(e).c_str()));
        } else if (rad.contains("FK5", Qt::CaseInsensitive)) {
            bool ok = false;
            int e = equ.toDouble(&ok);
            if (!ok) {return false;}
            rad += (", J" + QString(std::to_string(e).c_str()));
        }
    }

    // insert (label, value) to info entry
    infoMap["Celestial frame"] = rad;

    return true;
}

// Generate customized remaining info according to BUNIT, SPECSYS, VELREF
bool DataLoader::_genRemainInfo(std::map<QString, QString>& infoMap,
                                   const std::map<QString, QString> headerMap) {
    // check whether headerMap is empty
    if (headerMap.empty()) {
        qDebug() << "Empty argument: headerMap.";
        return false;
    }
    
    // find BUNIT & insert to entry
    auto found = headerMap.find("BUNIT");
    if (found == headerMap.end()) {
        qDebug() << "BUNIT not found.";
        return false;
    }
    infoMap["Pixel unit"] = found->second;

    // find SPECSYS & insert to entry
    found = headerMap.find("SPECSYS");
    if (found == headerMap.end()) {
        qDebug() << "SPECSYS not found.";
        return false;
    }
    infoMap["Spectral frame"] = found->second;

    // find VELREF & insert to entry
    found = headerMap.find("VELREF");
    if (found == headerMap.end()) {
        qDebug() << "VELREF not found.";
        return false;
    }
    if ((found->second).contains("Radio", Qt::CaseInsensitive)) {
        infoMap["Velocity definition"] = "Radio";
    } else if ((found->second).contains("Optical", Qt::CaseInsensitive)) {
        infoMap["Velocity definition"] = "Optical";
    } else {
        infoMap["Velocity definition"] = found->second;
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

// customized arrange file info
bool DataLoader::_arrangeFileInfo(const std::map<QString, QString> infoMap, std::vector<std::vector<QString>>& pairs){
    // check whether headerMap is empty
    if (infoMap.empty()) {
        qDebug() << "Empty argument: infoMap.";
        return false;
    }

    // sort file info by the following order
    const std::vector<QString> keys = {
        "Name",
        "Shape",
        "Number of Stokes",
        "Number of Channels",
        "Image reference coordinate",
        "RA Range",
        "Dec Range",
        "Frequency Range",
        "Velocity Range",
        "Celestial frame",
        "Coordinate type",
        "Projection",
        "Spectral frame",
        "Velocity definition",
        "Restoring Beam",
        "Median Restoring Beam",
        "Beam Area",
        "Pixel unit",
        "Pixel size"
    };

    for (auto iter = keys.begin(); iter != keys.end(); iter++) {
        auto found = infoMap.find(*iter);

        // omit empty field
        if (found != infoMap.end() && "" != found->second) {
            pairs.push_back({found->first, found->second});
        }
    }

    return true;
}

// Unit conversion: returns "value + unit"
QString DataLoader::_unitConversion(const QString value, const QString unit){
    QString result = "";

    // convert value to double
    bool ok = false;
    double num = 0;
    num = value.toDouble(&ok);
    if (!ok) {
        qDebug() << "Convert string " << value << " to double error.";
        result = value + " " + unit;
        return result;
    }

    // check unit: degree, Hz, arcsec, etc...
    if (unit.contains("deg", Qt::CaseInsensitive)) {
        result = _deg2arcsec(num);
    } else if (unit.contains("Hz", Qt::CaseInsensitive)) {
        result = _convertHz(num);
    } else if (unit.contains("arcsec", Qt::CaseInsensitive)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%.3f\"", num);
        result = QString(buf);
    } else { // unknown
        char buf[512];
        snprintf(buf, sizeof(buf), "%.3f", num);
        result = QString(buf) + " " + unit;
    }

    return result;
}

// Unit conversion: convert degree to arcsec
QString DataLoader::_deg2arcsec(const double degree) {
    // 1 degree = 60 arcmin = 60*60 arcsec
    double arcs = fabs(degree * 3600);

    // customized format of arcsec
    char buf[512];
    if (arcs >= 60.0){ // arcs >= 60, convert to arcmin
        snprintf(buf, sizeof(buf), "%.2f\'", degree < 0 ? -1*arcs/60 : arcs/60);
    } else if (arcs < 60.0 && arcs > 0.1) { // 0.1 < arcs < 60
        snprintf(buf, sizeof(buf), "%.2f\"", degree < 0 ? -1*arcs : arcs);
    } else if (arcs <= 0.1 && arcs > 0.01) { // 0.01 < arcs <= 0.1
        snprintf(buf, sizeof(buf), "%.3f\"", degree < 0 ? -1*arcs : arcs);
    } else { // arcs <= 0.01
        snprintf(buf, sizeof(buf), "%.4f\"", degree < 0 ? -1*arcs : arcs);
    }

    return QString(buf);
}

// Unit conversion: convert Hz to MHz or GHz
QString DataLoader::_convertHz(const double hz) {
    char buf[512];

    if (hz >= 1.0e9) {
        snprintf(buf, sizeof(buf), "%.4f GHz", hz/1.0e9);
    } else if (hz < 1.0e9 && hz >= 1.0e6) {
        snprintf(buf, sizeof(buf), "%.4f MHz", hz/1.0e6);
    } else {
        snprintf(buf, sizeof(buf), "%.4f Hz", hz);
    }

    return QString(buf);
}

DataLoader::~DataLoader(){
}
}
}
