/**
 *
 **/

#include "NewServerConnector.h"
#include "CartaLib/LinearMap.h"
#include "core/MyQApp.h"
#include "core/SimpleRemoteVGView.h"
#include "core/State/ObjectManager.h"
#include "core/Data/DataLoader.h"
#include "core/Data/ViewManager.h"
#include "core/Data/Image/Controller.h"
#include "core/Data/Image/DataSource.h"
#include <iostream>
#include <QImage>
#include <QPainter>
#include <QXmlInputSource>
#include <cmath>
#include <QTime>
#include <QTimer>
#include <QCoreApplication>
#include <functional>

#include <QStringList>
#include <QBuffer>

#include <QThread>

#include "CartaLib/Proto/file_list.pb.h"
#include "CartaLib/Proto/file_info.pb.h"
#include "CartaLib/Proto/open_file.pb.h"
#include "CartaLib/Proto/set_image_view.pb.h"
//#include "CartaLib/Proto/raster_image.pb.h"
#include "CartaLib/Proto/spectral_profile.pb.h"
#include "CartaLib/Proto/spatial_profile.pb.h"
#include "CartaLib/Proto/set_image_channels.pb.h"
#include "CartaLib/Proto/set_cursor.pb.h"
#include "CartaLib/Proto/region_stats.pb.h"
#include "CartaLib/Proto/region_requirements.pb.h"
//#include "CartaLib/Proto/region_histogram.pb.h"
#include "CartaLib/Proto/region.pb.h"
#include "CartaLib/Proto/error.pb.h"
#include "CartaLib/Proto/contour_image.pb.h"
#include "CartaLib/Proto/contour.pb.h"
#include "CartaLib/Proto/close_file.pb.h"
#include "CartaLib/Proto/animation.pb.h"

#include "CartaLib/IImage.h"

/// \brief internal class of NewServerConnector, containing extra information we like
///  to remember with each view
///
struct NewServerConnector::ViewInfo
{

    /// pointer to user supplied IView
    /// this is a NON-OWNING pointer
    IView * view;

    /// last received client size
    QSize clientSize;

    /// linear maps convert x,y from client to image coordinates
    Carta::Lib::LinearMap1D tx, ty;

    /// refresh timer for this object
    QTimer refreshTimer;

    /// refresh ID
    qint64 refreshId = -1;

    ViewInfo( IView * pview )
    {
        view = pview;
        clientSize = QSize(1,1);
        refreshTimer.setSingleShot( true);
        // just long enough that two successive calls will result in only one redraw :)
        refreshTimer.setInterval( 1000 / 120);
    }

};

NewServerConnector::NewServerConnector()
{
    // // queued connection to prevent callbacks from firing inside setState
    // connect( this, & NewServerConnector::stateChangedSignal,
    //          this, & NewServerConnector::stateChangedSlot,
    //          Qt::QueuedConnection );

    m_callbackNextId = 0;
}

NewServerConnector::~NewServerConnector()
{
}

void NewServerConnector::initialize(const InitializeCallback & cb)
{
    m_initializeCallback = cb;
}

// The function was initially implemented for flushstate()
// Deprecated since newArch, remove the func after removing Hack directory
void NewServerConnector::setState(const QString& path, const QString & newValue)
{
    // // find the path
    // auto it = m_state.find( path);

    // // if we cannot find it, insert it, together with the new value, and emit a change
    // if( it == m_state.end()) {
    //     m_state[path] = newValue;
    //     emit stateChangedSignal( path, newValue);
    //     return;
    // }

    // // if we did find it, but the value is different, set it to new value and emit signal
    // if( it-> second != newValue) {
    //     it-> second = newValue;
    //     emit stateChangedSignal( path, newValue);
    // }

    // // otherwise there was no change to state, so do dothing
}


QString NewServerConnector::getState(const QString & path  )
{
    return m_state[ path ];
}


/// Return the location where the state is saved.
QString NewServerConnector::getStateLocation( const QString& saveName ) const {
	// \todo Generalize this.
	return "/tmp/"+saveName+".json";
}

IConnector::CallbackID NewServerConnector::addCommandCallback(
        const QString & cmd,
        const IConnector::CommandCallback & cb)
{
    m_commandCallbackMap[cmd].push_back( cb);
    return m_callbackNextId++;
}

IConnector::CallbackID NewServerConnector::addMessageCallback(
        const QString & cmd,
        const IConnector::MessageCallback & cb)
{
    m_messageCallbackMap[cmd].push_back( cb);
    return m_callbackNextId++;
}

IConnector::CallbackID NewServerConnector::addStateCallback(
        IConnector::CSR path,
        const IConnector::StateChangedCallback & cb)
{
    // find the list of callbacks for this path
    auto iter = m_stateCallbackList.find( path);

    // if it does not exist, create it
    if( iter == m_stateCallbackList.end()) {
//        qDebug() << "Creating callback list for variable " << path;
        auto res = m_stateCallbackList.insert( std::make_pair(path, new StateCBList));
        iter = res.first;
    }

//    iter = m_stateCallbackList.find( path);
//    if( iter == m_stateCallbackList.end()) {
////        qDebug() << "What the hell";
//    }

    // add the calllback
    return iter-> second-> add( cb);

//    return m_stateCallbackList[ path].add( cb);
}

void NewServerConnector::registerView(IView * view)
{
    // let the view know it's registered, and give it access to the connector
    view->registration( this);

    // insert this view int our list of views
    ViewInfo * viewInfo = new ViewInfo( view);
//    viewInfo-> view = view;
//    viewInfo-> clientSize = QSize(1,1);
    m_views[ view-> name()] = viewInfo;

    // connect the view's refresh timer to a lambda, which will in turn call
    // refreshViewNow()
    // this is instead of using std::bind...
    // connect( & viewInfo->refreshTimer, & QTimer::timeout,
    //         [=] () {
    //                  refreshViewNow( view);
    // });
}

// unregister the view
void NewServerConnector::unregisterView( const QString& viewName ){
    ViewInfo* viewInfo = this->findViewInfo( viewName );
    if ( viewInfo != nullptr ){

        (& viewInfo->refreshTimer)->disconnect();
        m_views.erase( viewName );
    }
}

//    static QTime st;

// schedule a view refresh
qint64 NewServerConnector::refreshView(IView * view)
{
    // find the corresponding view info
    ViewInfo * viewInfo = findViewInfo( view-> name());
    if( ! viewInfo) {
        // this is an internal error...
        qCritical() << "refreshView cannot find this view: " << view-> name();
        return -1;
    }

    // start the timer for this view if it's not already started
//    if( ! viewInfo-> refreshTimer.isActive()) {
//        viewInfo-> refreshTimer.start();
//    }
//    else {
//        qDebug() << "########### saved refresh for " << view->name();
//    }

    // refreshViewNow(view);

    viewInfo-> refreshId ++;
    return viewInfo-> refreshId;
}

void NewServerConnector::removeStateCallback(const IConnector::CallbackID & /*id*/)
{
    qFatal( "not implemented");
}


Carta::Lib::IRemoteVGView * NewServerConnector::makeRemoteVGView(QString viewName)
{
    return new Carta::Core::SimpleRemoteVGView( this, viewName, this);
}

NewServerConnector::ViewInfo * NewServerConnector::findViewInfo( const QString & viewName)
{
    auto viewIter = m_views.find( viewName);
    if( viewIter == m_views.end()) {
        qWarning() << "NewServerConnector::findViewInfo: Unknown view " << viewName;
        return nullptr;
    }

    return viewIter-> second;
}

IConnector* NewServerConnector::getConnectorInMap(const QString & sessionID){
    return nullptr;
}

void NewServerConnector::setConnectorInMap(const QString & sessionID, IConnector *connector){
}

void NewServerConnector::startWebSocket(){
    // qFatal('NewServerConnector should not start a websocket!');
    CARTA_ASSERT_X( false, "NewServerConnector should not start a websocket!");
}

void NewServerConnector::startViewerSlot(const QString & sessionID) {

    QString name = QThread::currentThread()->objectName();
    qDebug() << "[NewServerConnector] Current thread name:" << name;
    if (name != sessionID) {
        qDebug()<< "ignore startViewerSlot";
        return;
    }

    viewer.start();
}

void NewServerConnector::onTextMessage(QString message){
    QString controllerID = this->viewer.m_viewManager->registerView("pluginId:ImageViewer,index:0");
    QString cmd = controllerID + ":" + message;

    qDebug() << "Message received:" << message;
    auto & allCallbacks = m_messageCallbackMap[ message];

    // QString result;
    std::string data;
    PBMSharedPtr msg;

    for( auto & cb : allCallbacks ) {
        msg = cb( message, "", "1");
    }
    msg->SerializeToString(&data);
    const QString result = QString::fromStdString(data);

    if( allCallbacks.size() == 0) {
        qWarning() << "JS command has no server listener:" << message;
    }

    emit jsTextMessageResultSignal(result);
}

void NewServerConnector::onBinaryMessage(char* message, size_t length){
    if (length < EVENT_NAME_LENGTH + EVENT_ID_LENGTH){
        qFatal("Illegal message.");
        return;
    }

    size_t nullIndex = 0;
    for (size_t i = 0; i < EVENT_NAME_LENGTH; i++) {
        if (!message[i]) {
            nullIndex = i;
            break;
        }
    }

    QString eventName = QString::fromStdString(std::string(message, nullIndex));
    qDebug() << "[NewServerConnector] Event received: " << eventName << QTime::currentTime().toString();

    QString respName;
    PBMSharedPtr msg;

    if (eventName == "REGISTER_VIEWER") {
        // The message should be handled in sessionDispatcher
        qFatal("Illegal request in NewServerConnector. Please handle it in SessionDispatcher.");
        return;

    } else if (eventName == "FILE_LIST_REQUEST") {
        respName = "FILE_LIST_RESPONSE";

        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        Carta::Data::DataLoader *dataLoader = objMan->createObject<Carta::Data::DataLoader>();

        CARTA::FileListRequest fileListRequest;
        fileListRequest.ParseFromArray(message + EVENT_NAME_LENGTH + EVENT_ID_LENGTH, length - EVENT_NAME_LENGTH - EVENT_ID_LENGTH);
        msg = dataLoader->getFileList(fileListRequest);

        // send the serialized message to the frontend
        sendSerializedMessage(message, respName, msg);
        return;

    } else if (eventName == "FILE_INFO_REQUEST") {
        respName = "FILE_INFO_RESPONSE";

        // we cannot handle the request so far, return a fake response.
        std::shared_ptr<CARTA::FileInfoResponse> fileInfoResponse(new CARTA::FileInfoResponse());
        fileInfoResponse->set_success(false);
        msg = fileInfoResponse;

        // send the serialized message to the frontend
        sendSerializedMessage(message, respName, msg);
        return;

    } else if (eventName == "CLOSE_FILE") {

        CARTA::CloseFile closeFile;
        closeFile.ParseFromArray(message + EVENT_NAME_LENGTH + EVENT_ID_LENGTH, length - EVENT_NAME_LENGTH - EVENT_ID_LENGTH);
        int closeFileId = closeFile.file_id();
        qDebug() << "[NewServerConnector] Close file id=" << closeFileId;

    } else if (eventName == "OPEN_FILE") {
        respName = "OPEN_FILE_ACK";

        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        QString controllerID = this->viewer.m_viewManager->registerView("pluginId:ImageViewer,index:0").split("/").last();
        qDebug() << "[NewServerConnector] controllerID=" << controllerID;
        Carta::Data::Controller* controller = dynamic_cast<Carta::Data::Controller*>( objMan->getObject(controllerID) );

        CARTA::OpenFile openFile;
        openFile.ParseFromArray(message + EVENT_NAME_LENGTH + EVENT_ID_LENGTH, length - EVENT_NAME_LENGTH - EVENT_ID_LENGTH);

        QString fileDir = QString::fromStdString(openFile.directory());
        if (!QDir(fileDir).exists()) {
            qWarning() << "File directory doesn't exist! (" << fileDir << ")";
            return;
        }

        bool success;
        QString fileName = QString::fromStdString(openFile.file());

        int fileId = openFile.file_id();
        qDebug() << "[NewServerConnector] Open the file ID:" << fileId;

        controller->addData(fileDir + "/" + fileName, &success, fileId);

        std::shared_ptr<Carta::Lib::Image::ImageInterface> image = controller->getImage();

        CARTA::FileInfo* fileInfo = new CARTA::FileInfo();
        fileInfo->set_name(openFile.file());

        if (image->getType() == "FITSImage") {
            fileInfo->set_type(CARTA::FileType::FITS);
        } else {
            fileInfo->set_type(CARTA::FileType::CASA);
        }
        fileInfo->add_hdu_list(openFile.hdu());

        const std::vector<int> dims = image->dims();
        CARTA::FileInfoExtended* fileInfoExt = new CARTA::FileInfoExtended();
        fileInfoExt->set_dimensions(dims.size());
        fileInfoExt->set_width(dims[0]);
        fileInfoExt->set_height(dims[1]);

        int stokeIndicator = controller->getStokeIndicator();
        // set the stoke axis if it exists
        if (stokeIndicator > 0) { // if stoke axis exists
            if (dims[stokeIndicator] > 0) { // if stoke dimension > 0
                fileInfoExt->set_stokes(dims[stokeIndicator]);
            }
        }

        // for the dims[k] that is not the stoke frame nor the x- or y-axis,
        // we assume it is a depth (it is the Spectral axis or the other unmarked axis)
        int lastFrame = 0;
        if (dims.size() > 2) {
            for (int i = 2; i < dims.size(); i++) {
                if (i != stokeIndicator && dims[i] > 0) {
                    fileInfoExt->set_depth(dims[i]);
                    lastFrame = dims[i] - 1;
                    break;
                }
            }
        }
        m_lastFrame[fileId] = lastFrame;

        // CARTA::HeaderEntry* headEntry = fileInfoExt->add_header_entries();

        // we cannot handle the request so far, return a fake response.
        std::shared_ptr<CARTA::OpenFileAck> ack(new CARTA::OpenFileAck());
        ack->set_success(true);
        ack->set_file_id(fileId);
        ack->set_allocated_file_info(fileInfo);
        ack->set_allocated_file_info_extended(fileInfoExt);
        msg = ack;

        // send the serialized message to the frontend
        sendSerializedMessage(message, respName, msg);

        // set the initial channel for spectral and stoke frames
        m_currentChannel[fileId] = {0, 0}; // {frameLow, stokeFrame}

        // set spectral and stoke frame ranges to calculate the pixel to histogram data
        //m_calHistRange[fileId] = {0, m_lastFrame[fileId], 0}; // {frameLow, frameHigh, stokeFrame}
        m_calHistRange[fileId] = {0, 0, 0}; // {frameLow, frameHigh, stokeFrame}

        m_changeFrame[fileId] = false;

        /////////////////////////////////////////////////////////////////////
        respName = "REGION_HISTOGRAM_DATA";

        // If the histograms correspond to the entire current 2D image, the region ID has a value of -1.
        int regionId = -1;

        // calculate pixels to histogram data
        Carta::Lib::IntensityUnitConverter::SharedPtr converter = nullptr; // do not include unit converter for pixel values
        PBMSharedPtr region_histogram_data = controller->getPixels2Histogram(fileId, regionId, m_calHistRange[fileId][0], m_calHistRange[fileId][1], numberOfBins, m_calHistRange[fileId][2], converter);

        msg = region_histogram_data;

        // send the serialized message to the frontend
        sendSerializedMessage(message, respName, msg);
        /////////////////////////////////////////////////////////////////////

        return;

    } else if (eventName == "SET_IMAGE_VIEW") {
        CARTA::SetImageView viewSetting;
        viewSetting.ParseFromArray(message + EVENT_NAME_LENGTH + EVENT_ID_LENGTH, length - EVENT_NAME_LENGTH - EVENT_ID_LENGTH);

        // get the controller
        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        QString controllerID = this->viewer.m_viewManager->registerView("pluginId:ImageViewer,index:0").split("/").last();
        qDebug() << "[NewServerConnector] controllerID=" << controllerID;
        Carta::Data::Controller* controller = dynamic_cast<Carta::Data::Controller*>( objMan->getObject(controllerID) );

        int fileId = viewSetting.file_id();
        qDebug() << "[NewServerConnector] File ID requested by frontend:" << fileId;

        // set the file id as the private parameter in the Stack object
        controller->setFileId(fileId);

        int frameLow = m_currentChannel[fileId][0];
        int frameHigh = frameLow;
        int stokeFrame = m_currentChannel[fileId][1];

        // if (frameLow != 0 || stokeFrame !=0) re-calculate the histogram!!
        if (m_changeFrame[fileId]) {
            qDebug() << "Re-calculate the pixel histogram!!";
            /////////////////////////////////////////////////////////////////////
            respName = "REGION_HISTOGRAM_DATA";

            // If the histograms correspond to the entire current 2D image, the region ID has a value of -1.
            int regionId = -1;

            // calculate pixels to histogram data
            Carta::Lib::IntensityUnitConverter::SharedPtr converter = nullptr; // do not include unit converter for pixel values
            PBMSharedPtr region_histogram_data = controller->getPixels2Histogram(fileId, regionId, m_calHistRange[fileId][0], m_calHistRange[fileId][1], numberOfBins, m_calHistRange[fileId][2], converter);

            msg = region_histogram_data;

            // send the serialized message to the frontend
            sendSerializedMessage(message, respName, msg);

            m_changeFrame[fileId] = false;
            /////////////////////////////////////////////////////////////////////
        }

        int mip = viewSetting.mip();
        int x_min = viewSetting.image_bounds().x_min();
        int x_max = viewSetting.image_bounds().x_max();
        int y_min = viewSetting.image_bounds().y_min();
        int y_max = viewSetting.image_bounds().y_max();

        // set image viewer bounds with respect to the fileId
        m_imageBounds[fileId] = {x_min, x_max, y_min, y_max, mip};

        /////////////////////////////////////////////////////////////////////
        respName = "RASTER_IMAGE_DATA";

        // get the down sampling raster image raw data
        PBMSharedPtr raster = controller->getRasterImageData(fileId, x_min, x_max, y_min, y_max, mip, frameLow, frameHigh, stokeFrame);
        msg = raster;

        // send the serialized message to the frontend
        sendSerializedMessage(message, respName, msg);
        return;
        /////////////////////////////////////////////////////////////////////

    } else if (eventName == "START_ANIMATION") {

        CARTA::StartAnimation startAnimation;
        startAnimation.ParseFromArray(message + EVENT_NAME_LENGTH + EVENT_ID_LENGTH, length - EVENT_NAME_LENGTH - EVENT_ID_LENGTH);
        int fileId = startAnimation.file_id();
        CARTA::AnimationFrame startFrame = startAnimation.start_frame();
        int startChannel = startFrame.channel();
        int startStoke = startFrame.stokes();
        CARTA::AnimationFrame endFrame = startAnimation.end_frame();
        int endChannel = endFrame.channel();
        int endStoke = endFrame.stokes();
        CARTA::AnimationFrame deltaFrame = startAnimation.delta_frame();
        int deltaChannel = deltaFrame.channel();
        int deltaStoke = deltaFrame.stokes();
        int frameInterval = startAnimation.frame_interval();
        bool looping = startAnimation.looping();
        bool reverse = startAnimation.reverse();
        qDebug() << "START_ANIMATION:" << "fileId=" << fileId << "startChannel=" << startChannel
                 << "startStoke=" << startStoke << "endChannel=" << endChannel << "endStoke=" << endStoke
                 << "deltaChannel=" << deltaChannel << "deltaStoke=" << deltaStoke << "frameInterval="
                 << frameInterval << "looping" << looping << "reverse" << reverse;
        return;

    } else {
        // Insert non-global object id
        // QString controllerID = this->viewer.m_viewManager->registerView("pluginId:ImageViewer,index:0");
        // QString cmd = controllerID + ":" + eventName;
        qCritical() << "There is no event handler:" << eventName;

//        auto & allCallbacks = m_messageCallbackMap[eventName];
//
//        if (allCallbacks.size() == 0) {
//            qCritical() << "There is no event handler:" << eventName;
//            return;
//        }
//
//        for (auto & cb : allCallbacks) {
//            msg = cb(eventName, "", "1");
//        }

        return;
    }

    // socket->send(binaryPayloadCache.data(), requiredSize, uWS::BINARY);
    // emit jsTextMessageResultSignal(result);
    return;
}

void NewServerConnector::imageChannelUpdateSignalSlot(char* message, int fileId, int channel, int stoke) {
    qDebug() << "[NewServerConnector] Set image channel=" << channel << ", fileId=" << fileId << ", stoke=" << stoke;

    QString respName;
    PBMSharedPtr msg;

    if (m_currentChannel[fileId][0] != channel || m_currentChannel[fileId][1] != stoke) {
        // update the current channel and stoke
        m_currentChannel[fileId][0] = channel;
        m_currentChannel[fileId][1] = stoke;
    } else {
        qDebug() << "[NewServerConnector] Internal signal is repeated!! Don't know the reason yet, just ignore the signal!!";
        return;
    }

    // set spectral and stoke frame ranges to calculate the pixel to histogram data
    //m_calHistRange[fileId] = {0, m_lastFrame[fileId], 0};
    m_calHistRange[fileId] = {channel, channel, stoke};

    m_changeFrame[fileId] = true;

    // get the controller
    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    QString controllerID = this->viewer.m_viewManager->registerView("pluginId:ImageViewer,index:0").split("/").last();
    qDebug() << "[NewServerConnector] controllerID=" << controllerID;
    Carta::Data::Controller* controller = dynamic_cast<Carta::Data::Controller*>( objMan->getObject(controllerID) );

    // set the file id as the private parameter in the Stack object
    controller->setFileId(fileId);

    /////////////////////////////////////////////////////////////////////
    respName = "REGION_HISTOGRAM_DATA";

    // If the histograms correspond to the entire current 2D image, the region ID has a value of -1.
    int regionId = -1;

    // calculate pixels to histogram data
    Carta::Lib::IntensityUnitConverter::SharedPtr converter = nullptr; // do not include unit converter for pixel values
    PBMSharedPtr region_histogram_data = controller->getPixels2Histogram(fileId, regionId, m_calHistRange[fileId][0], m_calHistRange[fileId][1], numberOfBins, m_calHistRange[fileId][2], converter);

    msg = region_histogram_data;

    // mark the image file is changed
    //m_changeImage = true;

    // send the serialized message to the frontend
    sendSerializedMessage(message, respName, msg);
    /////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////
    respName = "RASTER_IMAGE_DATA";

    int frameLow = m_currentChannel[fileId][0];
    int frameHigh = frameLow;
    int stokeFrame = m_currentChannel[fileId][1];

    // get image viewer bounds with respect to the fileId
    int x_min = m_imageBounds[fileId][0];
    int x_max = m_imageBounds[fileId][1];
    int y_min = m_imageBounds[fileId][2];
    int y_max = m_imageBounds[fileId][3];
    int mip = m_imageBounds[fileId][4];

    // use image bounds with respect to the fileID and get the down sampling raster image raw data
    PBMSharedPtr raster = controller->getRasterImageData(fileId, x_min, x_max, y_min, y_max, mip, frameLow, frameHigh, stokeFrame);
    msg = raster;

    // send the serialized message to the frontend
    sendSerializedMessage(message, respName, msg);
    /////////////////////////////////////////////////////////////////////
}

void NewServerConnector::sendSerializedMessage(char* message, QString respName, PBMSharedPtr msg) {
    bool success = false;
    size_t requiredSize = 0;
    std::vector<char> result = serializeToArray(message, respName, msg, success, requiredSize);
    if (success) {
        emit jsBinaryMessageResultSignal(result.data(), requiredSize);
        qDebug() << "[NewServerConnector] Send event:" << respName << QTime::currentTime().toString();
    }

}

// void NewServerConnector::stateChangedSlot(const QString & key, const QString & value)
// {
//     // find the list of callbacks for this path
//     auto iter = m_stateCallbackList.find( key);

//     // if it does not exist, do nothing
//     if( iter == m_stateCallbackList.end()) {
//         return;
//     }

//     // call all registered callbacks for this key
//     iter-> second-> callEveryone( key, value);
// }
