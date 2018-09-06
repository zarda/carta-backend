#include "State/ObjectManager.h"
#include "State/UtilState.h"
#include "Data/Image/Controller.h"
#include "Data/Image/CoordinateSystems.h"
#include "Data/Image/DataFactory.h"
#include "Data/Image/Stack.h"
#include "Data/Image/Grid/LabelFormats.h"
#include "Data/Image/DataSource.h"
#include "Data/Image/Grid/AxisMapper.h"
#include "Data/Image/Grid/DataGrid.h"
#include "Data/Image/Grid/GridControls.h"
#include "Data/Image/Contour/ContourControls.h"
#include "Data/Image/Contour/DataContours.h"
#include "Data/Region/RegionControls.h"
#include "Data/Region/Region.h"
#include "Data/Settings.h"
#include "Data/DataLoader.h"
#include "Data/Error/ErrorManager.h"
#include "../../ImageRenderService.h"
#include "Data/Colormap/Colormaps.h"

#include "Data/Util.h"
#include "ImageView.h"
#include "CartaLib/IImage.h"
#include "Globals.h"

#include "CartaLib/Proto/lm.helloworld.pb.h"

#include <QtCore/QDebug>
#include <QtCore/QList>
#include <QtCore/QDir>
#include <memory>
#include <set>

using namespace std;

namespace Carta {

namespace Data {

class Controller::Factory : public Carta::State::CartaObjectFactory {

public:

    Carta::State::CartaObject * create (const QString & path, const QString & id)
    {
        return new Controller (path, id);
    }
};

const QString Controller::CLIP_VALUE_MIN = "clipValueMin";
const QString Controller::CLIP_VALUE_MAX = "clipValueMax";
const QString Controller::CLOSE_IMAGE = "closeImage";
const QString Controller::AUTO_CLIP = "autoClip";
const QString Controller::DATA = "data";
const QString Controller::DATA_PATH = "dataPath";
const QString Controller::CURSOR = "formattedCursorCoordinates";
const QString Controller::CENTER = "center";
const QString Controller::IMAGE = "image";
const QString Controller::PAN_ZOOM_ALL = "panZoomAll";
const QString Controller::PLUGIN_NAME = "ImageViewer";
const QString Controller::STACK_SELECT_AUTO = "stackAutoSelect";


const QString Controller::CLASS_NAME = "Controller";
bool Controller::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass (CLASS_NAME,
                                                   new Controller::Factory());

using Carta::State::UtilState;
using Carta::State::StateInterface;
using Carta::Lib::AxisInfo;

Controller::Controller( const QString& path, const QString& id ) :
        		CartaObject( CLASS_NAME, path, id),
				m_stateMouse(UtilState::getLookup(path, Util::VIEW)){

	_initializeState();

	Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();

	//Stack
	Stack* layerGroupRoot = objMan->createObject<Stack>();
	QString viewName = Carta::State::UtilState::getLookup( path, Util::VIEW);
	layerGroupRoot->_setViewName( viewName );
	m_stack.reset( layerGroupRoot );
	connect( m_stack.get(), SIGNAL( frameChanged(Carta::Lib::AxisInfo::KnownType)),
			this, SLOT(_notifyFrameChange( Carta::Lib::AxisInfo::KnownType)));
	connect( m_stack.get(), SIGNAL( viewLoad()), this, SLOT(_loadViewQueued()));
	connect( m_stack.get(), SIGNAL(contourSetAdded(Layer*,const QString&)),
			this, SLOT(_contourSetAdded(Layer*, const QString&)));
	connect( m_stack.get(), SIGNAL(contourSetRemoved(const QString&)),
			this, SLOT(_contourSetRemoved(const QString&)));
    connect( m_stack.get(), SIGNAL(colorStateChanged()), this, SLOT(_emitColorChanged()));
	connect( m_stack.get(), SIGNAL(saveImageResult( bool)), this, SIGNAL(saveImageResult(bool)));
	connect( m_stack.get(), SIGNAL(inputEvent(  InputEvent)), this,
			SLOT( _onInputEvent( InputEvent )));

	// GridControls* gridObj = objMan->createObject<GridControls>();
	// m_gridControls.reset( gridObj );
	// connect( m_gridControls.get(), SIGNAL(gridChanged( const Carta::State::StateInterface&,bool)),
	// 		this, SLOT(_gridChanged( const Carta::State::StateInterface&, bool )));
	// connect( m_gridControls.get(), SIGNAL(displayAxesChanged(std::vector<Carta::Lib::AxisInfo::KnownType>,bool )),
	// 		this, SLOT( _displayAxesChanged(std::vector<Carta::Lib::AxisInfo::KnownType>,bool )));

	ContourControls* contourObj = objMan->createObject<ContourControls>();
	m_contourControls.reset( contourObj );
	m_contourControls->setPercentIntensityMap( this );
	connect( m_contourControls.get(), SIGNAL(drawContoursChanged()),
			this, SLOT(_loadViewQueued()));

	Settings* settingsObj = objMan->createObject<Settings>();
	m_settings.reset( settingsObj );

	_initializeCallbacks();

	RegionControls* regionObj = objMan->createObject<RegionControls>();
	connect( regionObj, SIGNAL(regionsChanged()), this, SLOT(_regionsChanged()));

	m_regionControls.reset( regionObj );
}

void Controller::addContourSet( std::shared_ptr<DataContours> contourSet){
	m_stack->_addContourSet( contourSet );
}

QString Controller::addData(const QString& fileName, bool* success, int fileId) {
	*success = false;
    QString result = DataFactory::addData( this, fileName, success, fileId);
    return result;
}

void Controller::setFileId(int fileId) {
    // set the file id as the private parameter in the Stack object
    m_stack->_setFileId(fileId);
}

QString Controller::_addDataImage(const QString& fileName, bool* success, int fileId) {
    // assign the fileId as a private parameter in the m_stack
    QString result = m_stack->_addDataImage(fileName, success, fileId);
    if ( *success ){
//        if ( isStackSelectAuto() ){
//            QStringList selectedLayers;
//            QString stackId= m_stack->_getCurrentId();
//            selectedLayers.append( stackId );
//            _setLayersSelected( selectedLayers );
//        }
        // _setSkyCSName();
        // _updateDisplayAxes();
        //emit dataChanged( this );
    }
    return result;
}

QStringList Controller::getOpenedFileList(){
  return m_stack->_getOpenedFileList();
}

QString Controller::applyClips( double minIntensityPercentile, double maxIntensityPercentile ){
    QString result;
    bool clipsChangedValue = false;
    if ( minIntensityPercentile < maxIntensityPercentile ){
        const double ERROR_MARGIN = 0.0001;
        if ( 0 <= minIntensityPercentile && minIntensityPercentile <= 1 ){
            double oldMin = m_state.getValue<double>(CLIP_VALUE_MIN );
            if ( qAbs(minIntensityPercentile - oldMin) > ERROR_MARGIN ){
                m_state.setValue<double>(CLIP_VALUE_MIN, minIntensityPercentile );
                clipsChangedValue = true;
            }
        }
        else {
            result = "Minimum intensity percentile invalid [0,1]: "+ QString::number( minIntensityPercentile);
        }
        if ( 0 <= maxIntensityPercentile && maxIntensityPercentile <= 1 ){
            double oldMax = m_state.getValue<double>(CLIP_VALUE_MAX);
            if ( qAbs(maxIntensityPercentile - oldMax) > ERROR_MARGIN ){
                m_state.setValue<double>(CLIP_VALUE_MAX, maxIntensityPercentile );
                clipsChangedValue = true;
            }
        }
        else {
            result = "Maximum intensity percentile invalid [0,1]: "+ QString::number( maxIntensityPercentile);
        }
        if( clipsChangedValue ){
            m_state.flushState();
            _loadViewQueued();
            bool autoClip = m_state.getValue<bool>(AUTO_CLIP);
            double minPercent = m_state.getValue<double>(CLIP_VALUE_MIN);
            double maxPercent = m_state.getValue<double>(CLIP_VALUE_MAX);
            emit clipsChanged( minPercent, maxPercent, autoClip );
        }
    }
    else {
        result = "The minimum percentile: "+QString::number(minIntensityPercentile)+
                " must be less than "+QString::number(maxIntensityPercentile);
    }
    return result;
}


void Controller::clear(){
    unregisterView();
}


void Controller::_clearStatistics(){
    m_stateMouse.setValue<QString>( CURSOR, "" );
    m_stateMouse.flushState();
}

QString Controller::closeImage( const QString& id ){
    QString result;
    bool imageClosed = m_stack->_closeData( id );
    if ( !imageClosed ){
        result = "Could not find data to remove for id="+id;
    }
    else {
        int visibleImageCount = m_stack->_getStackSizeVisible();
        if ( visibleImageCount == 0 ){
            _clearStatistics();
        }
        emit dataChanged( this );
    }
    return result;
}

void Controller::centerOnPixel( double centerX, double centerY ){
    bool panZoomAll = m_state.getValue<bool>( PAN_ZOOM_ALL );
    m_stack->_setPan( centerX, centerY, panZoomAll );
}

void Controller::_contourSetAdded( Layer* cData, const QString& setName ){
    if ( cData != nullptr ){
        std::shared_ptr<DataContours> addedSet = cData->_getContour( setName );
        if ( addedSet ){
            m_contourControls->_setDrawContours( addedSet );
        }
    }
}

void Controller::_contourSetRemoved( const QString setName ){
    //Remove the contour set from the controls only if nothing in the stack
    if ( !m_stack->_getContour( setName) ){
        m_contourControls->deleteContourSet( setName );
    }
}


// void Controller::_displayAxesChanged(std::vector<AxisInfo::KnownType> displayAxisTypes,
//         bool applyAll ){
//     m_stack->_displayAxesChanged( displayAxisTypes, applyAll );
//     emit axesChanged(); //animator has this signal axesChanged, also frameChanged
//     _updateCursorText( true );
// }


std::vector<Carta::Lib::AxisInfo::KnownType> Controller::_getAxisZTypes() const {
    std::vector<Carta::Lib::AxisInfo::KnownType> zTypes = m_stack->_getAxisZTypes();
    return zTypes;
}

std::set<AxisInfo::KnownType> Controller::_getAxesHidden() const {
    return m_stack->_getAxesHidden();
}


QPointF Controller::getCenterPixel() const {
    QPointF center = m_stack->_getCenterPixel();
    return center;
}

bool Controller::getAutoClip() const {
    bool autoClip = m_state.getValue<bool>(AUTO_CLIP);
    return autoClip;
}

double Controller::getClipPercentileMax() const {
    double clipValueMax = m_state.getValue<double>(CLIP_VALUE_MAX);
    return clipValueMax;
}

double Controller::getClipPercentileMin() const {
    double clipValueMin = m_state.getValue<double>(CLIP_VALUE_MIN);
    return clipValueMin;
}

QPointF Controller::_getContextPt( const QPointF& mousePt, const QSize& outputSize, bool* valid ) const {
	return m_stack->_getContextPt( mousePt, outputSize, valid );
}

Carta::Lib::KnownSkyCS Controller::getCoordinateSystem() const {
    return m_stack->_getCoordinateSystem();
}

QStringList Controller::getCoordinates( double x, double y, Carta::Lib::KnownSkyCS system) const {
    return m_stack->_getCoords( x, y, system);
}

std::shared_ptr<DataSource> Controller::getDataSource() const {
    return m_stack->_getDataSource();
}

QPointF Controller::getImagePt( bool* valid ) const {
    int mouseX = m_stateMouse.getValue<int>(ImageView::MOUSE_X);
    int mouseY = m_stateMouse.getValue<int>(ImageView::MOUSE_Y);
    QPointF mousePt( mouseX, mouseY );
    QSize outputSize = getOutputSize();
    return m_stack->_getImagePt( mousePt,  outputSize, valid  );
}


std::vector< std::shared_ptr<Layer> > Controller::getLayers() {
    return m_stack-> _getLayers();
}


std::shared_ptr<Layer> Controller::getLayer( const QString& name ) {
    return m_stack->_getLayer( name );
}


std::shared_ptr<RegionControls> Controller::getRegionControls() {
	return m_regionControls;
}

std::vector< std::shared_ptr<Carta::Lib::Image::ImageInterface> > Controller::getImages() {
    return m_stack->_getImages();
}

std::shared_ptr<Carta::Lib::Image::ImageInterface> Controller::getImage() {
    return m_stack->_getImage();
}

std::shared_ptr<ContourControls> Controller::getContourControls() {
    return m_contourControls;
}


QSize Controller::_getDisplaySize() const {
    return m_stack->_getDisplaySize();
}


int Controller::getFrameUpperBound( AxisInfo::KnownType axisType ) const {
    return m_stack->_getFrameUpperBound( axisType );
}


int Controller::getFrame( AxisInfo::KnownType axisType ) const {
    int frame = m_stack->_getFrame( axisType );
    return frame;
}


// std::shared_ptr<GridControls> Controller::getGridControls() {
//     return m_gridControls;
// }


std::vector<int> Controller::getImageDimensions( ) const {
    std::vector<int> result = m_stack->_getImageDimensions();
    return result;
}


QStringList Controller::getLayerIds() const{
    QStringList names = m_stack->_getLayerIds();
    return names;
}


std::vector<int> Controller::getImageSlice() const {
    std::vector<int> result = m_stack->_getImageSlice();
    return result;
}


std::vector<double> Controller::getIntensity( const std::vector<double>& percentiles, Carta::Lib::IntensityUnitConverter::SharedPtr converter ) const{
    int currentFrame = getFrame( AxisInfo::KnownType::SPECTRAL);
    std::vector<double> result = getIntensity( currentFrame, currentFrame, percentiles, converter );
    return result;
}


std::vector<double> Controller::getIntensity( int frameLow, int frameHigh, const std::vector<double>& percentiles, Carta::Lib::IntensityUnitConverter::SharedPtr converter ) const{
    int stokeFrame = getFrame(AxisInfo::KnownType::STOKES);
    qDebug() << "++++++++ get the stoke frame=" << stokeFrame << "( -1: no stoke, 0: stoke I, 1: stoke Q, 2: stoke U, 3: stoke V)";
    std::vector<double> intensities = m_stack->_getIntensity( frameLow, frameHigh, percentiles, stokeFrame, converter );
    return intensities;
}

int Controller::getStokeIndicator() const {
    int result = m_stack->_getStokeIndicator();
    return result;
}

int Controller::getSpectralIndicator() const {
    int result = m_stack->_getSpectralIndicator();
    return result;
}

PBMSharedPtr Controller::getPixels2Histogram(int fileId, int regionId, int frameLow, int frameHigh, int numberOfBins, int stokeFrame, Lib::IntensityUnitConverter::SharedPtr converter) const {
    PBMSharedPtr result = m_stack->_getPixels2Histogram(fileId, regionId, frameLow, frameHigh, numberOfBins, stokeFrame, converter);
    return result;
}

PBMSharedPtr Controller::getRasterImageData(int fileId, int x_min, int x_max, int y_min, int y_max,
    int mip, int frameLow, int frameHigh, int stokeFrame) const {
    PBMSharedPtr result = m_stack->_getRasterImageData(fileId, x_min, x_max, y_min, y_max, mip, frameLow, frameHigh, stokeFrame);
    return result;
}

QRectF Controller::_getInputRectangle(  ) const {
    return m_stack->_getInputRectangle( );
}


QSize Controller::getOutputSize( ) const {
    QSize result = m_stack->_getOutputSize();
    return result;
}


std::vector<double> Controller::getPercentiles( int frameLow, int frameHigh, std::vector<double> intensities, Carta::Lib::IntensityUnitConverter::SharedPtr converter ) const {
    return m_stack->_getPercentiles( frameLow, frameHigh, intensities, converter );
}


QPointF Controller::getPixelCoordinates( double ra, double dec, bool* valid ) const {
    QPointF result = m_stack->_getPixelCoordinates( ra, dec, valid );
    return result;
}

QPointF Controller::getWorldCoordinates( double pixelX, double pixelY, bool* valid ) const {
    Carta::Lib::KnownSkyCS coordSys = getCoordinateSystem();
    QPointF result = m_stack->_getWorldCoordinates( pixelX, pixelY, coordSys, valid );
    return result;
}

QString Controller::getPixelValue( double x, double y ) const {
    QString result = m_stack->_getPixelVal( x, y );
    return result;
}

QString Controller::getPixelUnits() const {
    QString result = m_stack->_getPixelUnits();
    return result;
}

QString Controller::_getPreferencesId() const {
    QString id;
    if ( m_settings.get() != nullptr ){
        id = m_settings->getPath();
    }
    return id;
}

Carta::Lib::NdArray::RawViewInterface* Controller::getRawData() const {
    std::vector<int> frames = getImageSlice();
    std::shared_ptr<DataSource> dataSource = getDataSource();
    return dataSource->_getRawData(frames);
}

int Controller::getRegionCount() const {
	return m_regionControls->getRegionCount();
}

int Controller::getSelectImageIndex() const {
    return m_stack->_getSelectImageIndex();
}

int Controller::getRegionIndexCurrent() const {
	return m_regionControls->getIndexCurrent();
}


std::vector<std::shared_ptr<ColorState> > Controller::getSelectedColorStates( bool global ){
    std::vector<std::shared_ptr<ColorState> > colorStates = m_stack->_getSelectedColorStates( global );
    return colorStates;
}

QString Controller::_getRegionControlsId() const {
	return m_regionControls->getPath();
}

QString Controller::_getStackId() const {
    return m_stack->getPath();
}


int Controller::getStackedImageCount() const {
    return m_stack->_getStackSize();
}


int Controller::getStackedImageCountVisible() const {
    return m_stack->_getStackSizeVisible();
}

QString Controller::getStateString( const QString& sessionId, SnapshotType type ) const{
    QString result("");
    if ( type == SNAPSHOT_PREFERENCES ){
        StateInterface prefState( "");
        prefState.setValue<QString>(Carta::State::StateInterface::OBJECT_TYPE, CLASS_NAME );
        prefState.insertValue<QString>(Util::PREFERENCES, m_state.toString());
        prefState.insertValue<QString>(Settings::SETTINGS, m_settings->getStateString(sessionId, type) );
        prefState.insertValue<QString>(RegionControls::CLASS_NAME, m_regionControls->_getStateString( sessionId,type));
        result = prefState.toString();
    }
    else if ( type == SNAPSHOT_DATA ){
        Carta::State::StateInterface dataState("");
        dataState.setState( m_stack->_getStateString() );
        dataState.setValue<QString>( StateInterface::OBJECT_TYPE, CLASS_NAME + StateInterface::STATE_DATA);
        dataState.setValue<int>(StateInterface::INDEX, getIndex() );
        QString regionControlState = m_regionControls->_getStateString( sessionId, type );
        dataState.insertObject( RegionControls::CLASS_NAME, regionControlState );
        result = dataState.toString();
    }
    return result;
}



QString Controller::getSnapType(CartaObject::SnapshotType snapType) const {
    QString objType = CartaObject::getSnapType( snapType );
    if ( snapType == SNAPSHOT_DATA ){
        objType = objType + Carta::State::StateInterface::STATE_DATA;
    }
    return objType;
}



double Controller::getZoomLevel( ) const {
    return m_stack->_getZoom();
}

// void Controller::_gridChanged( const StateInterface& state, bool applyAll ){
//     m_stack->_gridChanged( state, applyAll );
//     _setSkyCSName();
// }

void Controller::_onInputEvent( InputEvent  ev ){

	Carta::Lib::InputEvents::HoverEvent hover( ev );
	if ( hover.isValid() ){
		QPointF target = hover.pos();
		int mouseX = target.x();
		int mouseY = target.y();
		_updateCursor( mouseX, mouseY );

	}

	Carta::Lib::InputEvents::PointerEvent pointer( ev );
	if ( pointer.isValid() ){
		bool valid = false;
		QSize outputSize = getOutputSize();
		QPointF pointPt = pointer.pos();
		QPointF imagePixelPt = m_stack->_getImagePt( pointPt,  outputSize, &valid  );
		if ( valid ){
			//Only handle region events that are inside the image itself.
			Carta::Lib::AxisInfo::KnownType xType = m_stack->_getAxisXType();
			Carta::Lib::AxisInfo::KnownType yType = m_stack->_getAxisYType();
			int frameCountX = m_stack->_getFrameCount( xType );
			int frameCountY = m_stack->_getFrameCount( yType );


			if ( 0 <= imagePixelPt.x() && imagePixelPt.x() <= frameCountX ){
				if ( 0 <= imagePixelPt.y() && imagePixelPt.y() <= frameCountY ){
					m_regionControls->_onInputEvent( ev, imagePixelPt );
				}
			}
		}
	}

	//Note:  we set the event consumed if we are editing a region to prevent
	//a subsequent pan operation.
	if ( ! ev.isConsumed() ){
		Carta::Lib::InputEvents::DoubleTapEvent doubleTap( ev );
		if ( doubleTap.isValid() ){
			QPointF target = doubleTap.pos();
			int mouseX = target.x();
			int mouseY = target.y();
			updatePan( mouseX, mouseY );
		}
	}
}

void Controller::_initializeCallbacks(){

    // addCommandCallback( "testProtoBuf", [=] (const QString & /*cmd*/,
    //         const QString & params, const QString & /*sessionId*/) -> QString {
    //     QString result;
    //     std::string data;
    //     lm::helloworld msg1;
    //     msg1.set_id(101);
    //     msg1.set_str("hello");
    //     msg1.SerializeToString(&data);
    //     result = QString::fromStdString(data);
    //     return result;
    // });
    addMessageCallback( "testProtoBuf", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> PBMSharedPtr {
        std::shared_ptr<lm::helloworld> msg1(new lm::helloworld());
        msg1->set_id(101);
        msg1->set_str("hello");
        return static_cast<PBMSharedPtr>(msg1);
    });

    // addMessageCallback( "OPEN_FILE", [=] (const QString & /*cmd*/,
    //         const QString & params, const QString & /*sessionId*/) -> PBMSharedPtr {

    //     CARTA::FileInfo* fileInfo = new CARTA::FileInfo();
    //     fileInfo->set_name("test");
    //     fileInfo->set_type()

    //     std::shared_ptr<CARTA::OpenFileAck> ack(new CARAT::OpenFileAck());
    //     ack->set_success(true);
    //     ack->set_allocated_file_info(fileInfo);
    // });

    addCommandCallback( "hideImage", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::ID};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString idStr = dataValues[*keys.begin()];
        QString result = setImageVisibility( /*imageIndex*/idStr, false );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "moveLayers", [=] (const QString & /*cmd*/,
                                    const QString & params, const QString & /*sessionId*/) -> QString {
                std::set<QString> keys = {"moveDown"};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                QString moveDownStr = dataValues[*keys.begin()];
                bool validBool = false;
                bool moveDown = Util::toBool( moveDownStr, &validBool );
                QString result;
                if ( validBool ){
                    result = moveSelectedLayers( moveDown );
                }
                else {
                    result = "Whether to move up or down selected layers must be true/false: "+params;
                }
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "setGroup", [=] (const QString & /*cmd*/,
                                const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {Layer::GROUP};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString groupSelects = dataValues[*keys.begin()];
            bool validBool = false;
            bool group = Util::toBool( groupSelects, &validBool );
            QString result;
            if ( validBool ){
                result = setSelectedLayersGrouped( group );
            }
            else {
                result = "Grouping layers must be true/false: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "showImage", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::ID};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString idStr = dataValues[*keys.begin()];
        QString result = setImageVisibility( idStr, true );
        Util::commandPostProcess( result );
        return result;
    });



    //Listen for updates to the clip and reload the frame.
    addCommandCallback( "setClipValue", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {"clipValue"};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        bool validClip = false;
        QString clipKey = *keys.begin();
        QString clipWithoutPercent = dataValues[clipKey].remove("%");
        double clipVal = dataValues[clipKey].toDouble(&validClip);
        if ( validClip ){
            result = setClipValue( clipVal );
        }
        else {
            result = "Invalid clip value: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setAutoClip", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {"autoClip"};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString clipKey = *keys.begin();
        bool validBool = false;
        bool autoClip = Util::toBool( dataValues[clipKey], &validBool );
        QString result;
        if ( validBool ){
            setAutoClip( autoClip );
        }
        else {
            result = "Auto clip must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setPanZoomAll", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {"panZoomAll"};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString panZoomKey = *keys.begin();
            bool validBool = false;
            bool panZoomAll = Util::toBool( dataValues[panZoomKey], &validBool );
            QString result;
            if ( validBool ){
                setPanZoomAll( panZoomAll );
            }
            else {
                result = "Pan/Zoom All must be true/false: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    /*QString pointerPath= UtilState::getLookup( getPath(), UtilState::getLookup( Util::VIEW, Util::POINTER_MOVE));
    addStateCallback( pointerPath, [=] ( const QString& path, const QString& value ) {
        QStringList mouseList = value.split( " ");
        if ( mouseList.size() == 2 ){
            bool validX = false;
            int mouseX = mouseList[0].toInt( &validX );
            bool validY = false;
            int mouseY = mouseList[1].toInt( &validY );
            if ( validX && validY ){
                _updateCursor( mouseX, mouseY );
                emit zoomChanged();
            }
        }
    });*/

    addCommandCallback( "inputEvent", [=] (const QString & /*cmd*/,
    		const QString & params, const QString & /*sessionId*/) ->QString {

    	QJsonDocument doc = QJsonDocument::fromJson( params.toLatin1() );
    	if ( doc.isObject() ) {
    		InputEvent ev( doc.object() );
    		_onInputEvent( ev );
    	}
    	else {
    		qDebug() << "Input event doc not an object";
    	}
        return m_stateMouse.toString();
    	// return "";
    });

    addCommandCallback( CLOSE_IMAGE, [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) ->QString {
        std::set<QString> keys = {IMAGE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString imageId = dataValues[*keys.begin()];
        QString result = closeImage( imageId );
                return result;
    });


    addCommandCallback( Util::ZOOM, [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {
        bool error = false;
        auto vals = Util::string2VectorDouble( params, &error );
        if ( vals.size() > 2 ) {
            double centerX = vals[0];
            double centerY = vals[1];
            double z = vals[2];
            updatePanZoom( centerX, centerY, z );
        }
        return "";
    });

    addCommandCallback( "regionZoom", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {
        std::vector<std::shared_ptr<Region>> regions = m_regionControls->getRegions();
        std::shared_ptr<DataSource> dataSource = this->getDataSource();
        std::shared_ptr<Carta::Core::ImageRenderService::Service> imageService = dataSource->_getRenderer();
        QJsonArray array;
        for(int i = 0; i < regions.size(); i++) {
            QJsonObject json = regions[i]->toJSON();
            qreal x = json.value("x").toDouble();
            qreal y = json.value("y").toDouble();
            qreal width = json.value("width").toDouble();
            qreal height = json.value("height").toDouble();
            QPointF pt;
            pt.setX(x - (width / 2));
            pt.setY(y + (height / 2));
            QPointF topLeft = imageService->img2screen(pt);
            pt.setX(x + (width / 2));
            QPointF topRight = imageService->img2screen(pt);
            int w = static_cast<int>(topRight.x() - topLeft.x() + 0.5);
            pt.setX(x - (width / 2));
            pt.setY(y - (height / 2));
            QPointF bottomLeft = imageService->img2screen(pt);
            int h = static_cast<int>(bottomLeft.y() - topLeft.y() + 0.5);
            QString str = "";
            QJsonObject screenJson;
            screenJson.insert("x", static_cast<int>(topLeft.x() + 0.5));
            screenJson.insert("y", static_cast<int>(topLeft.y() + 0.5));
            screenJson.insert("width", w);
            screenJson.insert("height", h);
            array.insert(i, QJsonValue(screenJson));
        }
        QString value = "";
        QJsonDocument doc(array);
        value = QString(doc.toJson());
        return value;
    });

    addCommandCallback( "getDataGridState", [=] (const QString & /*cmd*/,
            const QString & /*params*/, const QString & /*sessionId*/) ->QString {
        Carta::State::StateInterface dataGridState = m_stack->_getDataGridState();
        QString result = dataGridState.toString();
        return result;
    });

    addCommandCallback( "newzoom", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {
        bool error = false;
        auto vals = Util::string2VectorDouble( params, &error );
        if ( vals.size() > 0 ) {
//            double centerX = vals[0];
//            double centerY = vals[1];
            double z = vals[0];

            // updateZoom( centerX, centerY, z );
            updateZoom(z);

            // original it is used by Python Client, use for new CARTA zoom in/out temporarily
            // setZoomLevel(z);
        }
        return "";
    });

    addCommandCallback( "getStackData", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {

        if ( m_stack != nullptr ){
            QString ttt= m_stack->getStateString();
            return m_stack->getStateString();
        }

        return "";
    });

    // unused
    addCommandCallback( "setPanAndZoomLevel", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {
        bool error = false;
        auto vals = Util::string2VectorDouble( params, &error );
        if ( vals.size() > 2 ) {
            double centerX = vals[0];
            double centerY = vals[1];
            double level = vals[2];
            double layerId = vals[3];
            updatePanZoomLevelJS( centerX, centerY, level, layerId );
        }
        return "";
    });

    addCommandCallback( "setZoomLevel", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {
        bool error = false;
        auto vals = Util::string2VectorDouble( params, &error );
        if ( vals.size() > 0 ) {
            double z = vals[0];
            double layerId = vals[1];
            setZoomLevelJS(z, layerId);
        }
        return "";
    });

    addCommandCallback( "registerContourControls", [=] (const QString & /*cmd*/,
                            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result;
        if ( m_contourControls.get() != nullptr ){
            result = m_contourControls->getPath();
        }
        return result;
    });

    addCommandCallback( "registerStack", [=] (const QString & /*cmd*/,
                                const QString & /*params*/, const QString & /*sessionId*/) -> QString {
            QString result;
            if ( m_stack.get() != nullptr ){
                result = m_stack->getPath();
            }
            return result;
        });

    addCommandCallback( "registerGridControls", [=] (const QString & /*cmd*/,
                        const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result;
        // if ( m_gridControls.get() != nullptr ){
        //     result = m_gridControls->getPath();
        // }
        return result;
    });

    addCommandCallback( "registerPreferences", [=] (const QString & /*cmd*/,
                            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result = _getPreferencesId();
        return result;
   });

    addCommandCallback( "registerRegionControls", [=] (const QString & /*cmd*/,
                               const QString & /*params*/, const QString & /*sessionId*/) -> QString {
           QString result = _getRegionControlsId();
           return result;
      });


    addCommandCallback( "resetPan", [=] (const QString & /*cmd*/,
                            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result;
        resetPan();
        return result;
    });


    addCommandCallback( "resetZoom", [=] (const QString & /*cmd*/,
                        const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result;
        resetZoom();
        return result;
    });

    addCommandCallback( "setLayerName", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::ID,Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString result = setLayerName( dataValues[Util::ID], dataValues[Util::NAME] );
        Util::commandPostProcess( result );
        return result;
    });


    addCommandCallback( "setLayersSelected", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        QStringList names = params.split(";");
        if ( names.size() == 0 ){
            result = "Please specify the layers to select.";
        }
        else {
            result = setLayersSelected( names );
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "saveImage", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {DATA_PATH};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString result = saveImage( dataValues[DATA_PATH]);
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setMaskColor", [=] (const QString & /*cmd*/,
                                const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::ID, Util::RED, Util::GREEN, Util::BLUE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString result;
        QString id = dataValues[Util::ID];
        QString redStr = dataValues[Util::RED];
        bool validRed = false;
        int redAmount = redStr.toInt( &validRed );
        QString greenStr = dataValues[Util::GREEN];
        bool validGreen = false;
        int greenAmount = greenStr.toInt( &validGreen );
        QString blueStr = dataValues[Util::BLUE];
        bool validBlue = false;
        int blueAmount = blueStr.toInt( &validBlue );
        if ( validRed && validGreen && validBlue ){
            QStringList errorList = setMaskColor( id, redAmount, greenAmount, blueAmount );
            result = errorList.join(";");
        }
        else {
            result = "Invalid mask color(s): "+params;
        }

        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setMaskAlpha", [=] (const QString & /*cmd*/,
                                    const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = { Util::ID, Util::ALPHA };
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString result;
        QString idStr = dataValues[Util::ID];
        QString alphaStr = dataValues[Util::ALPHA];
        bool validAlpha = false;
        int alphaAmount = alphaStr.toInt( &validAlpha );
        if ( validAlpha ){
            result = setMaskAlpha( idStr, alphaAmount );
        }
        else {
            result = "Invalid mask opacity: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setStackSelectAuto", [=] (const QString & /*cmd*/,
                       const QString & params, const QString & /*sessionId*/) -> QString {
       std::set<QString> keys = {STACK_SELECT_AUTO};
       std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
       QString autoModeStr = dataValues[STACK_SELECT_AUTO];
       bool validBool = false;
       bool autoSelect = Util::toBool( autoModeStr, &validBool );
       QString result;
       if ( validBool ){
           setStackSelectAuto( autoSelect );
       }
       else {
           result = "Please specify true/false when setting whether stack selection should be automatic: "+autoModeStr;
       }
       Util::commandPostProcess( result );
       return result;
   });

    addCommandCallback( "setCompositionMode", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::ID, LayerGroup::COMPOSITION_MODE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString compMode = dataValues[LayerGroup::COMPOSITION_MODE];
        QString idStr = dataValues[Util::ID];
        QString result = setCompositionMode( idStr, compMode );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setRegionType", [=] (const QString & /*cmd*/,
                                       const QString & params, const QString & /*sessionId*/) -> QString {
               std::set<QString> keys = {Util::TYPE};
               std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
               QString shape = dataValues[Util::TYPE];
               QString result = m_regionControls->setRegionCreateType( shape );
               return result;
           });

    addCommandCallback( "setTabIndex", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {Util::TAB_INDEX};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString tabIndexStr = dataValues[Util::TAB_INDEX];
        bool validIndex = false;
        int tabIndex = tabIndexStr.toInt( &validIndex );
        if ( validIndex ){
            result = setTabIndex( tabIndex );
        }
        else {
            result = "Please check that the tab index is a number: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });



////////// Copy the callback functions in gridcontrol here. A note for unfinished commands.

    // addCommandCallback( "setApplyAll", [=] (const QString & /*cmd*/,
    //                const QString & params, const QString & /*sessionId*/) -> QString {
    //            QString result;
    //            std::set<QString> keys = {ALL};
    //            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    //            QString applyAllStr = dataValues[ALL];
    //            bool validBool = false;
    //            bool applyAll = Util::toBool( applyAllStr, &validBool );
    //            if ( validBool ){
    //                setApplyAll( applyAll  );
    //            }
    //            else {
    //                result = "Whether or not to apply grid changes to all images must be true/false:"+params;
    //            }
    //            Util::commandPostProcess( result );
    //            return result;
    //        });
    //
    //
    // addCommandCallback( "setAxesColor", [=] (const QString & /*cmd*/,
    //                                 const QString & params, const QString & /*sessionId*/) -> QString {
    //     int redAmount = 0;
    //     int greenAmount = 0;
    //     int blueAmount = 0;
    //     QStringList result = _parseColorParams( params, "Axes", &redAmount, &greenAmount, &blueAmount);
    //     if ( result.size() == 0 ){
    //         result = setAxesColor( redAmount, greenAmount, blueAmount );
    //     }
    //     QString errors;
    //     if ( result.size() > 0 ){
    //         errors = result.join( ",");
    //     }
    //     Util::commandPostProcess( errors );
    //     return errors;
    // });
    //
    addCommandCallback( "setAxesThickness", [=] (const QString & /*cmd*/,
                                    const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::AXES_WIDTH;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setAxesTransparency", [=] (const QString & /*cmd*/,
                                    const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::AXES_ALPHA;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setAxisX", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {

        QString result = m_stack->_setAxis( AxisMapper::AXIS_X, params );
        return result;
    });

    addCommandCallback( "setAxisY", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) ->QString {

        QString result = m_stack->_setAxis( AxisMapper::AXIS_Y, params );
        return result;
    });

    addCommandCallback( "setCoordinateSystem", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString result = m_stack->_setCoordinateSystem( params );
        return result;
    });

     addCommandCallback( "setGridLabelFormat", [=] (const QString & /*cmd*/,
                         const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {DataGrid::FORMAT, DataGrid::LABEL_SIDE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString format = dataValues[DataGrid::FORMAT];
                     //Because the map expects colons and the label format has colons,
                     //the format colons are replaced with a - before sending from the
                     //client and the server must restore the colons.
        format = format.replace( "-", ":");
        QString labelSide = dataValues[DataGrid::LABEL_SIDE];
//                     QString result = setLabelFormat( labelSide, format );
//                     Util::commandPostProcess( result );
        QString directionLookup = Carta::State::UtilState::getLookup( DataGrid::LABEL_FORMAT, labelSide);
        QString directionFormatLookup = Carta::State::UtilState::getLookup( directionLookup, DataGrid::FORMAT );
        LabelFormats *m_formats = Util::findSingletonObject<LabelFormats>();
        QString actualFormat = m_formats->getFormat(format);
        QString result = m_stack->_setDataGridState(directionFormatLookup, actualFormat);
        QString oppositeSide = m_formats->getOppositeSide(labelSide);
        QString dLookup = Carta::State::UtilState::getLookup( DataGrid::LABEL_FORMAT, oppositeSide );
        QString dFormatLookup = Carta::State::UtilState::getLookup( dLookup, DataGrid::FORMAT );
        result = m_stack->_setDataGridState(dFormatLookup, LabelFormats::FORMAT_NONE);
        return result;
    });

    // addCommandCallback( "setGridColor", [=] (const QString & /*cmd*/,
    //                                     const QString & params, const QString & /*sessionId*/) -> QString {
    //         int redAmount = 0;
    //         int greenAmount = 0;
    //         int blueAmount = 0;
    //         QStringList result = _parseColorParams( params, DataGrid::GRID, &redAmount, &greenAmount, &blueAmount);
    //         if ( result.size() == 0 ){
    //             result = setGridColor( redAmount, greenAmount, blueAmount );
    //         }
    //         QString errors;
    //         if ( result.size() > 0 ){
    //             errors = result.join( ",");
    //         }
    //         Util::commandPostProcess( errors );
    //         return errors;
    //     });
    //
    addCommandCallback( "setFontFamily", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        // TODO: the function is unfinished
        QString stateName = DataGrid::FONT_FAMILY;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setFontSize", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::FONT_SIZE;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setGridThickness", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::GRID_WIDTH;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setGridSpacing", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SPACING;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setGridTransparency", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::GRID_ALPHA;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setLabelDecimals", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::LABEL_DECIMAL_PLACES;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    // addCommandCallback( "setLabelColor", [=] (const QString & /*cmd*/,
    //                             const QString & params, const QString & /*sessionId*/) -> QString {
    //         int redAmount = 0;
    //         int greenAmount = 0;
    //         int blueAmount = 0;
    //         QStringList result = _parseColorParams( params, "Label", &redAmount, &greenAmount, &blueAmount);
    //         if ( result.size() == 0 ){
    //             result = setLabelColor( redAmount, greenAmount, blueAmount );
    //         }
    //         QString errors;
    //         if ( result.size() > 0 ){
    //             errors = result.join( ",");
    //         }
    //         Util::commandPostProcess( errors );
    //         return errors;
    //     });
    //
    addCommandCallback( "setShowAxis", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_AXIS;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setShowCoordinateSystem", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_COORDS;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setShowDefaultCoordinateSystem", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_DEFAULT_COORDS;
        QString result = m_stack->_setDataGridState( stateName, params );

        Carta::State::StateInterface stackDataGridState = m_stack->_getDataGridState();
        bool useDefault = stackDataGridState.getValue<bool>( DataGrid::SHOW_DEFAULT_COORDS );
        if ( useDefault ){
            CoordinateSystems* m_coords = Util::findSingletonObject<CoordinateSystems>();
            QString defaultName = m_coords->getDefault();
            result = m_stack->_setCoordinateSystem( defaultName );
        }
        return result;
    });

    addCommandCallback( "setShowGridLines", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_GRID_LINES;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setShowInternalLabels", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_INTERNAL_LABELS;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setShowTicks", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::SHOW_TICKS;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });
    //
    // addCommandCallback( "setShowStatistics", [=] (const QString & /*cmd*/,
    //                     const QString & params, const QString & /*sessionId*/) -> QString {
    //                 QString result;
    //                 std::set<QString> keys = {DataGrid::SHOW_STATISTICS};
    //                 std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    //                 QString showStatisticsStr = dataValues[DataGrid::SHOW_STATISTICS];
    //                 bool validBool = false;
    //                 bool showStatistics = Util::toBool( showStatisticsStr, &validBool );
    //                 if ( validBool ){
    //                     result = setShowStatistics( showStatistics  );
    //                 }
    //                 else {
    //                     result = "Making statistics visible/invisible must be true/false:"+params;
    //                 }
    //                 Util::commandPostProcess( result );
    //                 return result;
    //             });
    //
    // addCommandCallback( "setTickColor", [=] (const QString & /*cmd*/,
    //                                        const QString & params, const QString & /*sessionId*/) -> QString {
    //            int redAmount = 0;
    //            int greenAmount = 0;
    //            int blueAmount = 0;
    //            QStringList result = _parseColorParams( params, DataGrid::TICK, &redAmount, &greenAmount, &blueAmount);
    //            if ( result.size() == 0 ){
    //                result = setTickColor( redAmount, greenAmount, blueAmount );
    //            }
    //            QString errors;
    //            if ( result.size() > 0 ){
    //                errors = result.join( ",");
    //            }
    //            Util::commandPostProcess( errors );
    //            return errors;
    //        });
    //
    addCommandCallback( "setTickLength", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::TICK_LENGTH;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setTickThickness", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::TICK_WIDTH;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });

    addCommandCallback( "setTickTransparency", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {

        QString stateName = DataGrid::TICK_ALPHA;
        QString result = m_stack->_setDataGridState( stateName, params );
        return result;
    });
    //
    // addCommandCallback( "setTheme", [=] (const QString & /*cmd*/,
    //                         const QString & params, const QString & /*sessionId*/) -> QString {
    //                     std::set<QString> keys = {DataGrid::THEME};
    //                     std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    //                     QString themeStr = dataValues[DataGrid::THEME];
    //                     QString result = setTheme( themeStr );
    //                     Util::commandPostProcess( result );
    //                     return result;
    //                 });
    addCommandCallback( "getColormaps", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
        Colormaps* colormaps = Util::findSingletonObject<Colormaps>();
        QString result = colormaps->getColorMaps().join(",");
        return result;
    });
}


void Controller::_initializeState(){

    //First the preference state.
    m_state.insertValue<bool>( AUTO_CLIP, false );
    m_state.insertValue<bool>(PAN_ZOOM_ALL, true );
    m_state.insertValue<bool>( STACK_SELECT_AUTO, true );
    m_state.insertValue<double>( CLIP_VALUE_MIN, 0.0 );
    m_state.insertValue<double>( CLIP_VALUE_MAX, 1.0 );

    //Default Tab
    m_state.insertValue<int>( Util::TAB_INDEX, 0 );
    m_state.flushState();

    //Mouse state
    m_stateMouse.insertObject( ImageView::MOUSE );
    m_stateMouse.insertValue<QString>(CURSOR, "");
    m_stateMouse.insertValue<QString>(Util::POINTER_MOVE, "");
    m_stateMouse.insertValue<int>(ImageView::MOUSE_X, 0 );
    m_stateMouse.insertValue<int>(ImageView::MOUSE_Y, 0 );
    m_stateMouse.flushState();
}

bool Controller::isStackSelectAuto() const {
    return m_state.getValue<bool>( STACK_SELECT_AUTO );
}


void Controller::_loadViewQueued( ){
    QMetaObject::invokeMethod( this, "_loadView", Qt::QueuedConnection );
}

void Controller::_emitColorChanged() {
    _loadViewQueued();
    emit colorChanged(this);
}

void Controller::_loadView(){
    //Load the image.
    bool autoClip = m_state.getValue<bool>(AUTO_CLIP);
    double clipValueMin = m_state.getValue<double>(CLIP_VALUE_MIN);
    double clipValueMax = m_state.getValue<double>(CLIP_VALUE_MAX);
    m_stack->_renderAll( autoClip, clipValueMin, clipValueMax );
    emit contextChanged();
}

QString Controller::moveSelectedLayers( bool moveDown ){
    QString result = m_stack->_moveSelectedLayers( moveDown );
    return result;
}


void Controller::_notifyFrameChange( Carta::Lib::AxisInfo::KnownType axis ){
    emit frameChanged( this, axis );
}

void Controller::refreshState(){
    CartaObject::refreshState();
    m_settings->refreshState();
    m_regionControls->refreshState();
    // m_gridControls->refreshState();
    m_contourControls->refreshState();
}

void Controller::_regionsChanged(){
//    Carta::Lib::VectorGraphics::VGList vgList = m_regionControls->vgList();
//    m_stack-> _setRegionGraphics ( vgList );
//    _loadView();
	emit dataChangedRegion( this );
}

void Controller::removeContourSet( std::shared_ptr<DataContours> contourSet ){
    m_stack->_removeContourSet( contourSet );
}

void Controller::_renderZoom( double factor ){
    int mouseX = m_stateMouse.getValue<int>(ImageView::MOUSE_X );
    int mouseY = m_stateMouse.getValue<int>(ImageView::MOUSE_Y );
    m_stack->_renderZoom( mouseX, mouseY, factor  );
}

void Controller::_renderContext( double zoomFactor ){
    m_stack->_renderContext( zoomFactor );
}


void Controller::resetState( const QString& state ){
    StateInterface restoredState( "");
    restoredState.setState( state );

    QString settingStr = restoredState.getValue<QString>(Settings::SETTINGS);
    m_settings->resetStateString( settingStr );

    QString regionControlStr = restoredState.getValue<QString>(RegionControls::CLASS_NAME);
    m_regionControls->resetStateString( regionControlStr );

    QString prefStr = restoredState.getValue<QString>(Util::PREFERENCES);
    m_state.setState( prefStr );
    m_state.flushState();
}

void Controller::resetStateData( const QString& state ){
    Carta::State::StateInterface dataState( "");
    dataState.setState( state );

    //Reset the layers
    m_stack->_resetStack( dataState );

    //Restore the region State
    QString regionControlStr = dataState.toString(RegionControls::CLASS_NAME);
    m_regionControls->_resetStateData( regionControlStr );

    //Notify others there has been a change to the data.
    emit dataChanged( this );
    emit colorChanged( this );

    //Reset the state of the grid controls based on the selected image.
    // StateInterface gridState = m_stack->_getGridState();
    // m_gridControls->_resetState( gridState );
    _loadViewQueued();
}

void Controller::resetPan(){
    bool panZoomAll = m_state.getValue<bool>( PAN_ZOOM_ALL );
    m_stack->_resetPan( panZoomAll );
}

void Controller::resetZoom(){
    bool panZoomAll = m_state.getValue<bool>( PAN_ZOOM_ALL );
    m_stack->_resetZoom( panZoomAll );
}


QString Controller::saveImage( const QString& fileName ){
    QString result;

    // DataLoader* dLoader = Util::findSingletonObject<DataLoader>();
    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    DataLoader* dLoader = objMan->createObject<DataLoader>();

    bool securityRestricted = dLoader->isSecurityRestricted();
    if ( !securityRestricted ){
        //Check and make sure the directory exists.
        int dirIndex = fileName.lastIndexOf( QDir::separator() );
        QString dirName = fileName;
        if ( dirIndex >= 0 ){
            dirName = fileName.left( dirIndex );
        }
        QDir dir( dirName );
        if ( ! dir.exists() ){
            result = "Please make sure the save path is valid: "+fileName;
        }
        else {
            result = m_stack->_saveImage( fileName );
        }
    }
    else {
        result = "Write access to the file system is not available.";
    }
    return result;
}

void Controller::saveImageResultCB( bool result ){
    if ( !result ){
        QString msg = "There was a problem saving the image.";
        Util::commandPostProcess( msg );
    }
    else {
        QString msg = "Image was successfully saved.";
        ErrorManager* errorMan = Util::findSingletonObject<ErrorManager>();
        errorMan->registerInformation( msg );
    }
    emit saveImageResult( result );
}


void Controller::setAutoClip( bool autoClip ){
    bool oldAutoClip = m_state.getValue<bool>(AUTO_CLIP );
    if ( autoClip != oldAutoClip ) {
        m_state.setValue<bool>( AUTO_CLIP, autoClip );
        m_state.flushState();
        // refresh the image viewer
        recallClipValue();
    }
}


void Controller::_setAxisMap(){
    std::vector<AxisInfo> supportedAxes = m_stack->_getAxisInfos();
    int axisCount = supportedAxes.size();
    AxisMapper::cleanAxisMap();
    for( int i=0; i<axisCount; i++ ){
        QString name = supportedAxes[i].longLabel().plain();
        AxisMapper::setAxisMap( std::pair<Carta::Lib::AxisInfo::KnownType, QString>
                                    (supportedAxes[i].knownType(), name), QString("") );
    }
}


QString Controller::setClipValue( double clipVal  ) {
    QString result;
    if ( 0 <= clipVal && clipVal <= 1 ){
        double oldClipValMin = m_state.getValue<double>( CLIP_VALUE_MIN );
        double oldClipValMax = m_state.getValue<double>( CLIP_VALUE_MAX );
        double oldClipVal = oldClipValMax - oldClipValMin;
        const double ERROR_MARGIN = 0.000001;
        if ( qAbs( clipVal - oldClipVal) >= ERROR_MARGIN ){
            double leftOver = 1 - clipVal;
            double clipValMin = leftOver / 2;
            double clipValMax = clipVal + leftOver / 2;
            result = applyClips (clipValMin, clipValMax );
        }
    }
    else {
        result = "Clip value must be in [0,1].";
    }
    return result;
}


void Controller::recallClipValue() {
    bool autoClip = m_state.getValue<bool>(AUTO_CLIP);
    double minPercent = m_state.getValue<double>(CLIP_VALUE_MIN);
    double maxPercent = m_state.getValue<double>(CLIP_VALUE_MAX);
    emit clipsChanged( minPercent, maxPercent, autoClip );
    _loadViewQueued();
}


// void Controller::_setSkyCSName(){
//     const Carta::Lib::KnownSkyCS cs = getCoordinateSystem();
//     CoordinateSystems* m_coords = Util::findSingletonObject<CoordinateSystems>();
//     QString csName = m_coords->getName(cs);
//     m_gridControls->_resetCoordinateSystem(csName);
// }


void Controller::_setFrameAxis(int value, AxisInfo::KnownType axisType ) {
    m_stack->_setFrameAxis( value, axisType );
    _updateCursorText( true );
}

void Controller::setFrameImage( int val) {
    if ( val < 0 ){
        return;
    }

    QString layerId = m_stack->_setFrameImage( val );
    if ( layerId.length() > 0 ) {
        //Reset the selected layers.
        if ( isStackSelectAuto() ){
            QStringList names;
            names.append( layerId );
            _setLayersSelected( names );
        }

//        Carta::State::StateInterface gridState = m_stack->_getGridState();
//        m_gridControls->_resetState( gridState );
//        _updateCursorText( true );
//        emit dataChanged( this );
    }
}

void Controller::setFrameRegion( int val ){
	m_regionControls->setIndexCurrent( val );
}


QString Controller::setImageVisibility( /*int dataIndex*/const QString& idStr, bool visible ){
    QString result;
    if ( idStr.length() > 0 ){
    /*int dataCount = m_datas.size();
    if ( dataIndex >= 0 && dataIndex < dataCount ){
        bool oldVisible = m_datas[dataIndex]->_isVisible();
        if ( oldVisible != visible ){
            m_datas[dataIndex]->_setVisible( visible );

            int selectedImageIndex = _getIndexCurrent();
            //Update the upper bound on the number of images available.
            int visibleCount = getStackedImageCountVisible();
            m_selectImage->setUpperBound( visibleCount );*/
        bool visibilityChanged = m_stack->_setVisible( idStr, visible );
        if ( visibilityChanged ){
            emit dataChanged( this );
            //Render the image if it is the one currently being viewed.
            /*if ( selectedImageIndex == dataIndex  ){
                _scheduleFrameReload( false );
            }*/
            //if ( visibleCount == 0 ){
            if ( m_stack->_getStackSizeVisible() == 0 ){
                _clearStatistics();
            }
            //saveState();
        }
    }
    else {
        result = "Could not set image visibility; invalid identifier.";
    }
    return result;
}

QString Controller::setLayerName( const QString& id, const QString& name ){
    bool nameSet = m_stack->_setLayerName( id, name );
    QString result;
    if ( !nameSet ){
        result = "Layer was not found so the name could not be set.";
    }
    return result;
}

// 20170420, no one use yet
// Start to try to use
QString Controller::setLayersSelected( const QStringList indices ){
    QString result;
    if ( indices.size() > 0 ){
        bool selectModeAuto = isStackSelectAuto();
        if ( !selectModeAuto ){
            result = _setLayersSelected( indices );
            emit dataChanged( this );
        }
        else {
            result = "Enable manual layer selection mode before setting layers.";
        }
    }
    else {
        result = "Please specify one or more layers to select.";
    }
    return result;
}


QString Controller::_setLayersSelected( QStringList names ){
    QString result;
    bool stackAutoSelect = m_state.getValue<bool>(STACK_SELECT_AUTO);
    QString firstName;
    if ( names.size() > 0 ){
        firstName = names[0];
    }
    bool selectStateChanged = m_stack->_setSelected( names );
    if ( selectStateChanged ){
        if ( !firstName.isEmpty() ){
            int selectIndex = m_stack->_getIndex( firstName );
            if (  !stackAutoSelect  ){
                setFrameImage( selectIndex );
            }
        }
        // refresh the map of axes immediately after read data
//        _setAxisMap();
        //emit colorChanged( this );
        // The signal below causes the duplicate behaviors
        // Use setLayersSelected() to replace the original callback
        // emit dataChanged( this );
    }
    return result;
}

QStringList Controller::setMaskColor( const QString& id, int redAmount, int greenAmount, int blueAmount ){
    QStringList result;
    m_stack->_setMaskColor( id, redAmount, greenAmount, blueAmount, result );

    return result;
}

QString Controller::setMaskAlpha( const QString& id, int alphaAmount ){
    QString result;
    m_stack->_setMaskAlpha( id, alphaAmount, result );
    return result;
}

void Controller::setPanZoomAll( bool panZoomAll ){
    bool oldPanZoomAll = m_state.getValue<bool>(PAN_ZOOM_ALL);
    if ( panZoomAll != oldPanZoomAll ){
        m_state.setValue<bool>( PAN_ZOOM_ALL, panZoomAll );
        m_state.flushState();
    }
}


void Controller::setStackSelectAuto( bool automatic ){
    bool oldStackSelectAuto = m_state.getValue<bool>(STACK_SELECT_AUTO );
    if ( oldStackSelectAuto != automatic ){
        m_state.setValue<bool>( STACK_SELECT_AUTO, automatic );
        m_state.flushState();
    }
}

QString Controller::setCompositionMode( const QString& id, const QString& compMode ){
    QString result;
    m_stack->_setCompositionMode( id, compMode, result );
    return result;
}

QString Controller::setSelectedLayersGrouped( bool grouped ){
    QString result;
    bool operationPerformed = m_stack->_setLayersGrouped( grouped );
    if ( operationPerformed ){
        //Notify others there has been a change to the data.
        emit dataChanged( this );
    }
    else {
        if ( grouped ){
            result = "The selected layer(s) could not be grouped.";
        }
        else {
            result = "Unable to remove group.";
        }
    }
    return result;
}

QString Controller::setTabIndex( int index ){
    QString result;
    if ( index >= 0 ){
        int oldIndex = m_state.getValue<int>( Util::TAB_INDEX );
        if ( index != oldIndex ){
            m_state.setValue<int>( Util::TAB_INDEX, index );
            m_state.flushState();
        }
    }
    else {
        result = "Image settings tab index must be nonnegative: "+ QString::number(index);
    }
    return result;
}

void Controller::_setViewDrawContext( std::shared_ptr<DrawStackSynchronizer> drawContext ){
    m_stack->_setViewDrawContext( drawContext );
}

void Controller::_setViewDrawZoom( std::shared_ptr<DrawStackSynchronizer> drawZoom ){
    m_stack->_setViewDrawZoom( drawZoom );
}

// used by Python Client
void Controller::setZoomLevel( double zoomFactor ){
    bool zoomPanAll = m_state.getValue<bool>(PAN_ZOOM_ALL);
    m_stack->_setZoomLevel( zoomFactor, zoomPanAll );
}

void Controller::setZoomLevelJS( double zoomFactor, double layerId ){
    m_stack->_setZoomLevelForLayerId( zoomFactor, layerId );
}

void Controller::_updateCursor( int mouseX, int mouseY ){
    if ( m_stack->_getStackSize() == 0 ){
        return;
    }

    int oldMouseX = m_stateMouse.getValue<int>( ImageView::MOUSE_X );
    int oldMouseY = m_stateMouse.getValue<int>( ImageView::MOUSE_Y );
    if ( oldMouseX != mouseX || oldMouseY != mouseY ){
        m_stateMouse.setValue<int>( ImageView::MOUSE_X, mouseX);
        m_stateMouse.setValue<int>( ImageView::MOUSE_Y, mouseY );
        _updateCursorText( false );
        m_stateMouse.flushState();
    }
}

void Controller::_updateCursorText(bool notifyClients ){
    QString formattedCursor;
    int mouseX = m_stateMouse.getValue<int>(ImageView::MOUSE_X );
    int mouseY = m_stateMouse.getValue<int>(ImageView::MOUSE_Y );

    // get Quantile information and show them on the image viewer
    double minPercent = m_state.getValue<double>(CLIP_VALUE_MIN);
    double maxPercent = m_state.getValue<double>(CLIP_VALUE_MAX);
    bool isAutoClip = m_state.getValue<bool>(AUTO_CLIP);
    QString cursorText = m_stack->_getCursorText(isAutoClip, minPercent, maxPercent, mouseX, mouseY);

    if ( !cursorText.isEmpty()){
        if ( cursorText != m_stateMouse.getValue<QString>(CURSOR)){
            m_stateMouse.setValue<QString>( CURSOR, cursorText );
            if ( notifyClients ){
                m_stateMouse.flushState();
            }
        }
    }
}


// void Controller::_updateDisplayAxes(){
//     if ( m_gridControls ){
//         std::vector<AxisInfo> supportedAxes = m_stack->_getAxisInfos();
//         m_gridControls->_setAxisInfos( supportedAxes );
//         AxisInfo::KnownType xType = m_stack->_getAxisXType();
//         AxisInfo::KnownType yType = m_stack->_getAxisYType();
//         //const Carta::Lib::KnownSkyCS cs = getCoordinateSystem();
//         QString xPurpose = AxisMapper::getPurpose( xType );
//         QString yPurpose = AxisMapper::getPurpose( yType );
//         m_gridControls->setAxis( AxisMapper::AXIS_X, xPurpose );
//         m_gridControls->setAxis( AxisMapper::AXIS_Y, yPurpose );
//     }
// }

void Controller::updatePanZoomLevelJS( double centerX, double centerY, double zoomLevel, double layerId ){
    m_stack->_updatePanZoom( centerX, centerY, -1, false, zoomLevel, layerId);
    emit contextChanged();
    emit zoomChanged();
}

void Controller::updatePanZoom( double centerX, double centerY, double zoomFactor ){
    bool zoomPanAll = m_state.getValue<bool>(PAN_ZOOM_ALL);
    m_stack->_updatePanZoom( centerX, centerY, zoomFactor, zoomPanAll, -1, -1);
    emit contextChanged();
    emit zoomChanged();
}

void Controller::updateZoom(double zoomFactor){
    bool zoomPanAll = m_state.getValue<bool>(PAN_ZOOM_ALL);
    m_stack->updateZoom( zoomFactor, zoomPanAll, -1, -1);
    emit contextChanged();
    emit zoomChanged();
}


void Controller::updatePan( double centerX , double centerY){
    bool zoomPanAll = m_state.getValue<bool>(PAN_ZOOM_ALL);
    m_stack->_updatePan( centerX, centerY, zoomPanAll );
    _updateCursorText( true );
    emit contextChanged();
    emit zoomChanged();
}



Controller::~Controller(){
    //unregisterView();
    clear();
}

}
}
