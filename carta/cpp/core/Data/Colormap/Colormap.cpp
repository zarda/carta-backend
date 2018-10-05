#include "Colormap.h"
#include "ColorState.h"
#include "Data/Settings.h"
#include "Data/Colormap/Colormaps.h"
#include "Data/Colormap/Gamma.h"
#include "Data/Error/ErrorManager.h"
#include "Data/Image/Controller.h"
#include "Data/Image/DataSource.h"
#include "Data/Histogram/Histogram.h"
#include "Data/Units/UnitsIntensity.h"
#include "Data/Units/UnitsFrequency.h"
#include "Data/Util.h"
#include "State/StateInterface.h"
#include "State/UtilState.h"
#include "ImageView.h"
#include "Globals.h"
#include "PluginManager.h"
#include "CartaLib/PixelPipeline/CustomizablePixelPipeline.h"
#include "CartaLib/Hooks/ConversionIntensityHook.h"
#include "CartaLib/Hooks/ConversionSpectralHook.h"
#include <QtCore/qmath.h>
#include <set>
#include <QElapsedTimer>
#include <QDebug>

namespace Carta {

namespace Data {

const QString Colormap::CLASS_NAME = "Colormap";
const QString Colormap::COLOR_STOPS = "stops";
const QString Colormap::COLOR_GRADES = "colorGrades";
const QString Colormap::GLOBAL = "global";
const QString Colormap::IMAGE_UNITS = "imageUnits";
const QString Colormap::INTENSITY_MIN = "intensityMin";
const QString Colormap::INTENSITY_MAX = "intensityMax";
const QString Colormap::PERCENT_MIN = "percentMin";
const QString Colormap::PERCENT_MAX = "percentMax";
const QString Colormap::TAB_INDEX = "tabIndex";


class Colormap::Factory : public Carta::State::CartaObjectFactory {
public:
    Carta::State::CartaObject * create (const QString & path, const QString & id){
        return new Colormap (path, id);
    }
};

bool Colormap::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass ( CLASS_NAME, new Colormap::Factory());
UnitsIntensity* Colormap::m_intensityUnits = nullptr;
Gamma* Colormap::m_gammaTransform = nullptr;

Colormap::Colormap( const QString& path, const QString& id):
    CartaObject( CLASS_NAME, path, id ),
    m_linkImpl( new LinkableImpl( path ) ),
    m_stateData( Carta::State::UtilState::getLookup(path, Carta::State::StateInterface::STATE_DATA)),
    m_stateMouse(Carta::State::UtilState::getLookup(path,ImageView::VIEW)){

    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    Settings* prefObj = objMan->createObject<Settings>();
    m_settings.reset( prefObj );

    ColorState* colorStateObj = objMan->createObject<ColorState>();
    connect( colorStateObj, SIGNAL( colorStateChanged()), this, SLOT( _colorStateChanged()));
    colorStateObj->_initializeDefaultState( m_state );
    m_stateColors.push_back( std::shared_ptr<ColorState>(colorStateObj));

    _colorStateChanged();

    _initializeStatics();
    _initializeDefaultState();
    _setErrorMargin();
    _initializeCallbacks();
}

QString Colormap::addLink( CartaObject*  cartaObject ){
    Controller* target = dynamic_cast<Controller*>(cartaObject);
    bool objAdded = false;
    QString result;
    if ( target != nullptr ){
        objAdded = m_linkImpl->addLink( target );
        if ( objAdded ){
            connect( target, SIGNAL(colorChanged(Controller*)), this, SLOT(_setColorStates(Controller*)));
            _setColorStates( target );
            connect( target, SIGNAL(clipsChanged(double,double,bool)), this, SLOT(_updateIntensityBounds(double,double,bool)));
            connect( target, SIGNAL(dataChanged(Controller*)), this, SLOT( _dataChanged(Controller*)));
            _dataChanged( target );

        }
    }
    else {
        Histogram* hist = dynamic_cast<Histogram*>(cartaObject);
        if ( hist != nullptr ){
            objAdded = m_linkImpl->addLink( hist );
            if ( objAdded ){
                connect( this, SIGNAL(colorMapChanged()), hist, SLOT( updateColorMap()));
                hist->updateColorMap();
                //connect( hist,SIGNAL(colorIntensityBoundsChanged(double,double,bool)), this, SLOT(_updateIntensityBounds(double,double,bool)));
            }
        }
        else {
            result = "Colormap only supports linking to histograms and images.";
        }
    }
    return result;
}


void Colormap::clear(){
    m_linkImpl->clear();
}


void Colormap::_colorStateChanged(){
    if ( m_stateColors.size() > 0 ){
        emit colorMapChanged();

        //Calculate new stops based on the pixel pipeline
        _calculateColorStops();

        //Copy the latest color information into this state so the
        //view will update.
        m_stateColors[0]->_replicateTo( m_state );
        m_state.flushState();
    }
}

void Colormap::_calculateColorStops(){
    Controller* controller = _getControllerSelected();
    if ( controller ){
        std::shared_ptr<DataSource> dSource = controller->getDataSource();
        if ( dSource  ){
            //QString nameStr = m_state.getValue<QString>(ColorState::COLOR_MAP_NAME);
            //Colormaps* cMaps = Util::findSingletonObject<Colormaps>();
            //std::shared_ptr<Carta::Lib::PixelPipeline::IColormapNamed> map = cMaps->getColorMap( nameStr );

            std::shared_ptr<Carta::Lib::PixelPipeline::CustomizablePixelPipeline> pipe = dSource->_getPipeline();
            if ( pipe ){
                QStringList buff;

                // This is not actually correct -- the pixel pipeline should be converting the values in the image to the new units before comparing them.
                // But it's more correct than using the wrong units, which is what was happening before.
                double intensityMin;
                double intensityMax;
                bool success;
                std::tie(intensityMin, intensityMax) = _getIntensities(success);
                // we don't have to check success, because we're not doing any conversions
                
                pipe->setMinMax( intensityMin, intensityMax );
                double diff = intensityMax - intensityMin;
                double delta = diff / 100;
                for ( int i = 0; i < 100; i++ ){

                    float val = intensityMin + i*delta;

                    // fill Hex color codes in a string list
                    Carta::Lib::PixelPipeline::NormRgb normRgb;
                    pipe->convert( val, normRgb );
//                    QColor mapColor;
//                    if ( normRgb[0] >= 0 && normRgb[1] >= 0 && normRgb[2] >= 0 ){
//                    	mapColor = QColor::fromRgbF( normRgb[0], normRgb[1], normRgb[2] );
//                    }
//                    QString hexStr = mapColor.name();
//                    if ( i < 99 ){
//                        hexStr = hexStr + ",";
//                    }
//                    buff.append( hexStr );
                }
                m_state.setValue<QString>( COLOR_STOPS, buff.join("") );

                // recalculate the colormap bar labels
                _calculateColorLabels();
            }
        }
    }
}

void Colormap::_calculateColorLabels() {
    // Note that we do not convert the unit for the entire raw data (see the function Colormap::_calculateColorStops()),
    // we just convert the unit for labels in colormap bar.
    QStringList buff;
    double intensityMin;
    double intensityMax;
    bool success = false;

    // define the number of intensity labels to calculate
    int numberOfSection = 5;
    std::vector<double> intensities(numberOfSection + 1, -1);

    // get the unit converter with the current unit for colormap bar labels.
    Carta::Lib::IntensityUnitConverter::SharedPtr converter = nullptr;
    if (getImageUnits() != "N/A") {
        converter = _getIntensityConverter(getImageUnits());
    }

    if (converter && converter->frameDependent) {
        // Get intensitis for colormap bar labels with the unit converter.
        intensities = _getIntensityLables(success, numberOfSection, converter);
        qDebug() << "[colormap bar intensities] the unit conversion is dependent on the spectral frame";
    } else if (converter) {
        // Calculate the Min. and Max. intensities with the unit conversion.
        std::tie(intensityMin, intensityMax) = _getIntensities(success, converter);
        qDebug() << "[colormap bar intensities] the unit conversion is independent of the spectral frame";
    }

    if (!success) {
        // Use original intensities without unit conversion.
        bool dummySuccess;
        std::tie(intensityMin, intensityMax) = _getIntensities(dummySuccess);
        qDebug() << "[colormap bar intensities] the unit conversion is not applied";
    }

    for (int j = 0; j < numberOfSection + 1; j++) {
        double val;
        if (success) {
            // if the unit conversion is successful
            if (converter && converter->frameDependent) {
                // if the unit conversion is dependent on the spectral frame
                val = intensities[j];
            } else {
                // if the unit conversion is independent of the spectral frame
                // We use intensities which are equally distributed between Min. and Max values.
                val = intensityMin + j * (intensityMax - intensityMin) / numberOfSection;
            }
        } else {
            // if the unit conversion is not successful
            // We use intensities which are equally distributed between Min. and Max values.
            val = intensityMin + j * (intensityMax - intensityMin) / numberOfSection;
        }
        // fill in intensity labels in a string list (as scientific expressions)
        QString sci_val = QString::number(val, 'E', 3);
        if (j < numberOfSection) {
            sci_val = sci_val + ",";
        }
        buff.append(sci_val);
    }
    m_state.setValue<QString>(COLOR_GRADES, buff.join(""));
}

QString Colormap::_commandInvertColorMap( const QString& params ){
    QString result;
    std::set<QString> keys = {ColorState::INVERT};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString invertStr = dataValues[*keys.begin()];
    bool validBool = false;
    bool invert = Util::toBool( invertStr, &validBool );
    if ( validBool ){
        result = setInvert( invert );
    }
    else {
        result = "Invert color map parameters must be true/false: "+params;
    }
    Util::commandPostProcess( result );
    return result;
}

QString Colormap::_commandReverseColorMap( const QString& params ){
    QString result;
    std::set<QString> keys = {ColorState::REVERSE};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString reverseStr = dataValues[*keys.begin()];
    bool validBool = false;
    bool reverse = Util::toBool(reverseStr, &validBool);
    if ( validBool ){
        result = setReverse( reverse );
    }
    else {
        result = "Invalid color map reverse parameters: "+ params;
    }
    Util::commandPostProcess( result );
    return result;
}

QString Colormap::_commandSetColorMix( const QString& params ){
    QString result;
    std::set<QString> keys = {Util::RED, Util::GREEN, Util::BLUE};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );

    bool validRed = false;
    double redValue = dataValues[Util::RED].toDouble(&validRed );

    bool validBlue = false;
    double blueValue = dataValues[Util::BLUE].toDouble(&validBlue );

    bool validGreen = false;
    double greenValue = dataValues[Util::GREEN].toDouble(&validGreen);
    if ( validRed && validBlue && validGreen ){
        result = setColorMix( redValue, greenValue, blueValue );
    }
    else {
        result = "Color mix values must be numbers: "+params;
    }
    Util::commandPostProcess( result );
    return result;
}

QString Colormap::_commandSetColorMap( const QString& params ){
    std::set<QString> keys = {Util::NAME};
    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
    QString colorMapStr = dataValues[*keys.begin()];
    QString result = setColorMap( colorMapStr );
    Util::commandPostProcess( result );
    return result;
}


Carta::Lib::IntensityUnitConverter::SharedPtr Colormap::_getIntensityConverter(const QString& toUnit) {
    Carta::Lib::IntensityUnitConverter::SharedPtr intensity_converter = nullptr;

    Controller* controller = _getControllerSelected();

    if ( controller ){
        QString fromUnit = controller->getPixelUnits();

        std::shared_ptr<DataSource> dataSource = controller->getDataSource();

        if ( dataSource ){
            std::shared_ptr<Carta::Lib::Image::ImageInterface> image = dataSource->_getImage();

            if ( image ){
                auto result2 = Globals::instance()-> pluginManager()
                    -> prepare <Carta::Lib::Hooks::ConversionIntensityHook>(image, fromUnit, toUnit, 1, "" );
                
                auto lam2 = [&intensity_converter] ( const Carta::Lib::Hooks::ConversionIntensityHook::ResultType &converter ) {
                    intensity_converter = converter;
                };
                
                result2.forEach( lam2 );
                
                // If units are unknown, or the conversion is not supported, or the conversion depends on Hz values but there is no spectral axis, we return a null converter and continue, so that we can still change the clipping values for the raw data.

                if (!intensity_converter) {
                    qWarning() << "Cannot find converter to convert intensity between" << fromUnit << "and" << toUnit <<" -- this conversion is not supported. Returning a null converter and ignoring unit conversions.";
                    return intensity_converter;
                }

                if (intensity_converter->frameDependent) {
                    int spectralIndex = Util::getAxisIndex( image, Carta::Lib::AxisInfo::KnownType::SPECTRAL);
                    
                    if (spectralIndex < 0) {                        
                        qWarning() << "Cannot find converter to convert intensity between" << fromUnit << "and" << toUnit <<" -- the image has no spectral axis. Returning a null converter and ignoring unit conversions.";
                        intensity_converter = nullptr;
                    }
                }
            }
        }
    }

    return intensity_converter;
}

std::pair<double,double> Colormap::_getIntensities(bool &success, Carta::Lib::IntensityUnitConverter::SharedPtr converter) const {
    double minPercent = m_stateData.getValue<double>( PERCENT_MIN );
    double maxPercent = m_stateData.getValue<double>( PERCENT_MAX );
    return _getIntensities(success, minPercent, maxPercent, converter);
}

std::pair<double,double> Colormap::_getIntensities(bool &success, const double minPercent, const double maxPercent, Carta::Lib::IntensityUnitConverter::SharedPtr converter) const {
    std::vector<double> intensities;
    success = false;
    
    Controller* controller = _getControllerSelected();
    if ( controller ){
        intensities = controller->getIntensity( -1, -1, {minPercent, maxPercent}, converter );
        // TODO TODO TODO error-checking regression; we need to check if we have Hz values available in DataSource or the algorithms, and handle failures
        if (intensities.size() == 2) {
            success = true;
            return std::make_pair(intensities[0], intensities[1]);
        }
    }

    return std::make_pair(-1, -1);
}

std::vector<double> Colormap::_getIntensityLables(bool &success, const int numberOfSections, Carta::Lib::IntensityUnitConverter::SharedPtr converter) const {
    double minIntensity = m_stateData.getValue<double>( INTENSITY_MIN );
    double maxIntensity = m_stateData.getValue<double>( INTENSITY_MAX );
    std::vector<double> oldIntensities;
    double delta = (maxIntensity - minIntensity) / numberOfSections;

    for (int i = 0; i < numberOfSections + 1; i++) {
        double tmpIntensity = minIntensity + i * delta;
        oldIntensities.push_back(tmpIntensity);
    }

    // get the controller
    Controller* controller = _getControllerSelected();

    // get percentiles with respect to old intensity labels
    std::vector<double> percentiles;
    if (controller) {
        percentiles = controller->getPercentiles( -1, -1, oldIntensities, converter );
    }

    // Initialize new intensity labels for unit conversion
    std::vector<double> intensities(numberOfSections + 1, -1);
    success = false;

    // If the controller exists and intensities size is greater than one (since we need at least two labels for a colormap bar).
    if (controller && percentiles.size() > 1) {
        // get new intensity labels after the unit conversion
        intensities = controller->getIntensity( -1, -1, percentiles, converter );
        success = true;
        return intensities;
    }
    return intensities;
}

void Colormap::_dataChanged( Controller* controller ){
    if ( controller ){
        bool autoClip = controller->getAutoClip();
        double colorMinPercent = controller->getClipPercentileMin();
        double colorMaxPercent = controller->getClipPercentileMax();
        
        // Attempt to set correct default / initial units after an image is loaded
        // This is a bit messy because the dataChanged signal is not only used on image load
        // It would be good to have a separate signal for that
        QString current_units = m_state.getValue<QString>(IMAGE_UNITS);
        QString latest_default_units = m_intensityUnits->getDefault();
        if (current_units == "N/A" && latest_default_units != "N/A") {
            m_state.setValue<QString>(IMAGE_UNITS, latest_default_units);
        }
        
        _updateIntensityBounds( colorMinPercent, colorMaxPercent, autoClip );
    }
}

Controller* Colormap::_getControllerSelected() const {
    //We are only supporting one linked controller.
    Controller* controller = nullptr;
    int linkCount = m_linkImpl->getLinkCount();
    for ( int i = 0; i < linkCount; i++ ){
        CartaObject* obj = m_linkImpl->getLink(i );
        Controller* control = dynamic_cast<Controller*>( obj);
        if ( control != nullptr){
            controller = control;
            break;
        }
    }
    return controller;
}

QString Colormap::getImageUnits() const {
    return m_state.getValue<QString>( IMAGE_UNITS );
}

QList<QString> Colormap::getLinks() const {
    return m_linkImpl->getLinkIds();
}

QString Colormap::_getPreferencesId() const {
    return m_settings->getPath();
}

int Colormap::getSignificantDigits() const {
    return m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
}

QString Colormap::getStateString( const QString& sessionId, SnapshotType type ) const{
    QString result("");
    if ( type == SNAPSHOT_PREFERENCES ){
        Carta::State::StateInterface prefState( "");
        prefState.setValue<QString>(Carta::State::StateInterface::OBJECT_TYPE, CLASS_NAME );
        prefState.insertValue<QString>( Util::PREFERENCES, m_state.toString());
        prefState.insertValue<QString>( Settings::SETTINGS, m_settings->getStateString(sessionId, type) );
        result = prefState.toString();
    }
    else if ( type == SNAPSHOT_LAYOUT ){
        result = m_linkImpl->getStateString(getIndex(), getSnapType(type));
    }
    return result;
}


int Colormap::getTabIndex() const {
    return m_state.getValue<int>( Util::TAB_INDEX );
}

void Colormap::_initializeDefaultState(){

    m_state.insertValue<bool>( GLOBAL, true );
    m_state.insertValue<QString>( COLOR_STOPS, "");
    m_state.insertValue<QString>( COLOR_GRADES, "");
    m_state.insertValue<int>(Util::SIGNIFICANT_DIGITS, 6 );
    m_state.insertValue<int>(TAB_INDEX, 0 );
    m_state.insertValue<QString>( IMAGE_UNITS, m_intensityUnits->getDefault() );
    m_state.flushState();

    //Image dependent intensity bounds
    m_stateData.insertValue<double>( INTENSITY_MIN, 0 );
    m_stateData.insertValue<double>( INTENSITY_MAX, 1 );
    m_stateData.insertValue<double>( PERCENT_MIN, 0 );
    m_stateData.insertValue<double>( PERCENT_MAX, 1 );

    m_stateData.flushState();

    //Mouse
    m_stateMouse.insertObject( ImageView::MOUSE );
    m_stateMouse.insertValue<QString>(ImageView::MOUSE_X, 0 );
    m_stateMouse.insertValue<QString>(ImageView::MOUSE_Y, 0 );
    m_stateMouse.flushState();

}

void Colormap::_initializeCallbacks(){

    addCommandCallback( "getColormapAllData", [=] (const QString & /*cmd*/,
                        const QString & /*params*/, const QString & /*sessionId*/) -> QString {
                    QString result =m_state.toString();
                    QJsonDocument result_d=QJsonDocument::fromJson(result.toUtf8());
                    QJsonObject resultObj= result_d.object();

                    QString intensity = m_stateData.toString();
                    QJsonDocument intensity_d=QJsonDocument::fromJson(intensity.toUtf8());
                    QJsonObject intensityObj= intensity_d.object();
                    resultObj.insert("intensity_data",intensityObj);

                    QJsonDocument new_result_doc(resultObj);
                    QString resultStr(new_result_doc.toJson());

                    qDebug() <<"result:" << resultStr;
                    return resultStr;
//                    return m_stateData.toString();
    });

    addCommandCallback( "registerPreferences", [=] (const QString & /*cmd*/,
                        const QString & /*params*/, const QString & /*sessionId*/) -> QString {
                    QString result = _getPreferencesId();
                    return result;
    });


    addCommandCallback( "setDataTransform", [=] (const QString & /*cmd*/,
                                const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {ColorState::TRANSFORM_DATA};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString dataTransformStr = dataValues[*keys.begin()];
        QString result = setDataTransform( dataTransformStr );
        Util::commandPostProcess( result );
        return result;
   });

    addCommandCallback( "setColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandSetColorMap( params );
            return result;
        });

    addCommandCallback( "invertColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandInvertColorMap( params );
            return result;
        });

    addCommandCallback( "reverseColormap", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
            QString result = _commandReverseColorMap( params );
            return result;
        });

    addCommandCallback( "setBorderAlpha", [=] (const QString & /*cmd*/,
                                    const QString & params, const QString & /*sessionId*/) -> QString {
                QString result;
                std::set<QString> keys = {Util::ALPHA};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );

                bool validAlpha = false;
                double alphaValue = dataValues[Util::ALPHA].toInt(&validAlpha );
                if ( validAlpha ){
                    result = setBorderAlpha( alphaValue );
                }
                else {
                    result = "Border color alpha value must be in [0,255]: "+params;
                }
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "setBorderColor", [=] (const QString & /*cmd*/,
                                const QString & params, const QString & /*sessionId*/) -> QString {
            QString result;
            std::set<QString> keys = {Util::RED, Util::GREEN, Util::BLUE};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );

            bool validRed = false;
            double redValue = dataValues[Util::RED].toInt(&validRed );

            bool validBlue = false;
            double blueValue = dataValues[Util::BLUE].toInt(&validBlue );

            bool validGreen = false;
            double greenValue = dataValues[Util::GREEN].toInt(&validGreen);

            if ( validRed && validBlue && validGreen ){
                result = setBorderColor( redValue, greenValue, blueValue );
            }
            else {
                result = "Border color values must be in [0,255]: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setColorMix", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
                QString result = _commandSetColorMix( params );

                return result;
            });

    addCommandCallback( "setNanColor", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {Util::RED, Util::GREEN, Util::BLUE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );

        bool validRed = false;
        double redValue = dataValues[Util::RED].toInt(&validRed );

        bool validBlue = false;
        double blueValue = dataValues[Util::BLUE].toInt(&validBlue );

        bool validGreen = false;
        double greenValue = dataValues[Util::GREEN].toInt(&validGreen);

        if ( validRed && validBlue && validGreen ){
            result = setNanColor( redValue, greenValue, blueValue );
        }
        else {
            result = "Nan color values must be in [0,255]: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setScales", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {ColorState::SCALE_1, ColorState::SCALE_2};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        bool valid = false;
        double scale1 = dataValues[ColorState::SCALE_1].toDouble( & valid );
        if ( !valid ){
            result = "Invalid color scale: "+params;
        }
        else {
            double scale2 = dataValues[ColorState::SCALE_2].toDouble( &valid );
            if ( !valid ){
                result = "Invalid color scale: "+params;
            }
            else {
                bool scaleChanged = false;
                int stateColorCount = m_stateColors.size();
                for ( int i = 0; i < stateColorCount; i++ ){
                    bool scaleChanged1 = m_stateColors[i]->_setGammaX( scale1, m_errorMargin, getSignificantDigits() );
                    bool scaleChanged2 = m_stateColors[i]->_setGammaY( scale2, m_errorMargin, getSignificantDigits() );
                    if ( scaleChanged1 || scaleChanged2 ){
                        scaleChanged = true;
                    }
                }
                if ( scaleChanged ){
                    double gamma = m_gammaTransform->getGamma( scale1, scale2 );
                    result = setGamma( gamma );
                }
            }
        }
        return result;
    });

    addCommandCallback( "setGamma", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {ColorState::GAMMA};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString gammaStr = dataValues[ColorState::GAMMA];
        bool validDigits = false;
        double gamma = gammaStr.toDouble( &validDigits );
        if ( validDigits ){
            std::pair<double,double> position = m_gammaTransform->find( gamma );
            int stateCount = m_stateColors.size();
            for ( int i = 0; i < stateCount; i++ ){
                m_stateColors[i]->_setGammaX( position.first, m_errorMargin, getSignificantDigits() );
                m_stateColors[i]->_setGammaY( position.second, m_errorMargin, getSignificantDigits() );
            }
            result = setGamma( gamma );
        }
        else {
            result = "Colormap gamma must be a number: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setSignificantDigits", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
                QString result;
                std::set<QString> keys = {Util::SIGNIFICANT_DIGITS};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                QString digitsStr = dataValues[Util::SIGNIFICANT_DIGITS];
                bool validDigits = false;
                int digits = digitsStr.toInt( &validDigits );
                if ( validDigits ){
                    result = setSignificantDigits( digits );
                }
                else {
                    result = "Colormap significant digits must be an integer: "+params;
                }
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "setDefaultBorder", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {ColorState::BORDER_DEFAULT};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString defaultBorderStr = dataValues[*keys.begin()];
            bool validBool = false;
            bool useDefaultBorder = Util::toBool( defaultBorderStr, &validBool );
            QString result;
            if ( validBool ){
                setBorderDefault( useDefaultBorder );
            }
            else {
                result = "Please specify true/false for use default border: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setDefaultNan", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {ColorState::NAN_DEFAULT};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString defaultNanStr = dataValues[*keys.begin()];
        bool validBool = false;
        bool useDefaultNan = Util::toBool( defaultNanStr, &validBool );
        QString result;
        if ( validBool ){
            setNanDefault( useDefaultNan );
        }
        else {
            result = "Please specify true/false for use default Nan: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setGlobal", [=] (const QString & /*cmd*/,
                            const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {GLOBAL};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString globalStr = dataValues[*keys.begin()];
            bool validBool = false;
            bool global = Util::toBool( globalStr, &validBool );
            QString result;
            if ( validBool ){
                setGlobal( global );
            }
            else {
                result = "Please specify true/false when setting whether or not the color map is global: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setImageUnits", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {IMAGE_UNITS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString unitsStr = dataValues[*keys.begin()];
        QString result = setImageUnits( unitsStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setIntensityRange", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {INTENSITY_MIN, INTENSITY_MAX};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString intMinStr = dataValues[INTENSITY_MIN];
            QString intMaxStr = dataValues[INTENSITY_MAX];
            bool validMin = false;
            bool validMax = false;
            double intMin = intMinStr.toDouble( &validMin );
            double intMax = intMaxStr.toDouble( &validMax );
            QString result;
            if ( validMin && validMax ){
                result = setIntensityRange( intMin, intMax );
            }
            else {
                result = "Color map intensity bounds must be numbers: "+params;
            }
            Util::commandPostProcess( result );
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
}

void Colormap::_initializeStatics(){
    //Intensity units
    if ( m_intensityUnits == nullptr ){
        m_intensityUnits = Util::findSingletonObject<UnitsIntensity>();
    }
    //Gamma transform
    if ( m_gammaTransform == nullptr ){
        m_gammaTransform = Util::findSingletonObject<Gamma>();
    }
}

bool Colormap::isBorderDefault() const {
    bool borderDefault = false;
    if ( m_stateColors.size() > 0 ){
        borderDefault = m_stateColors[0]->_isBorderDefault();
    }
    return borderDefault;
}

bool Colormap::isGlobal() const {
   return m_state.getValue<bool>( GLOBAL );
}

bool Colormap::isInverted() const {
    bool inverted = false;
    if ( m_stateColors.size() > 0 ){
        inverted = m_stateColors[0]->_isInverted();
    }
    return inverted;
}

bool Colormap::isLinked( const QString& linkId ) const {
    bool linked = false;
    CartaObject* obj = m_linkImpl->searchLinks( linkId );
    if ( obj != nullptr ){
        linked = true;
    }
    return linked;
}

bool Colormap::isNanDefault() const {
    bool nanDefault = false;
    if ( m_stateColors.size() > 0 ){
        nanDefault = m_stateColors[0]->_isNanDefault();
    }
    return nanDefault;
}


bool Colormap::isReversed() const {
    bool reversed = false;
    if ( m_stateColors.size() > 0 ){
        reversed = m_stateColors[0]->_isReversed();
    }
    return reversed;
}


void Colormap::refreshState(){
    CartaObject::refreshState();
    m_settings->refreshState();
    m_linkImpl->refreshState();
}

QString Colormap::removeLink( CartaObject* cartaObject ){
    Controller* controller = dynamic_cast<Controller*>(cartaObject);
    bool objRemoved = false;
    QString result;
    if ( controller != nullptr ){
        objRemoved = m_linkImpl->removeLink( controller );
        if ( objRemoved ){
            controller->disconnect( this );
        }
    }
    else {
       Histogram* hist = dynamic_cast<Histogram*>(cartaObject);
       if ( hist != nullptr ){
           objRemoved = m_linkImpl->removeLink( hist );
           if ( objRemoved ){
               this->disconnect( hist );
           }
       }
       else {
           result= "Color was unable to remove link, only histogram and image links supported.";
       }
    }
    return result;
}


void Colormap::resetState( const QString& state ){
    Carta::State::StateInterface restoredState( "");
    restoredState.setState( state );

    QString settingStr = restoredState.getValue<QString>(Settings::SETTINGS);
    m_settings->resetStateString( settingStr );

    QString prefStr = restoredState.getValue<QString>(Util::PREFERENCES);
    m_state.setState( prefStr );
    m_state.flushState();
}




QString Colormap::setBorderAlpha( int alphaValue ){
    int stateColorCount = m_stateColors.size();
    QString result;
    if ( isBorderDefault() ){
        setBorderDefault( false );
    }
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setBorderAlpha( alphaValue );
        if ( !result.isEmpty()){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}


QString Colormap::setBorderColor( int redValue, int greenValue, int blueValue){
    int stateColorCount = m_stateColors.size();
    QString result;
    if ( isBorderDefault() ){
        setBorderDefault( false );
    }
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setBorderColor( redValue, greenValue, blueValue );
        if ( !result.isEmpty()){
            break;
        }
    }

    if ( result.isEmpty() ){
        _colorStateChanged();
    }

    return result;
}

QString Colormap::setBorderDefault( bool borderDefault ) {
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        m_stateColors[i]->_setBorderDefault( borderDefault );
    }
    if ( stateColorCount == 0 ){
        result = "There were no color maps to for border default.";
    }
    else {
        _colorStateChanged();
    }

    return result;
}

QString Colormap::setColorMap( const QString& colorMapStr ){
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setColorMap( colorMapStr );
        if ( !result.isEmpty() ){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}

QString Colormap::setColorMix( double redValue, double greenValue, double blueValue){
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setColorMix( redValue, greenValue, blueValue );
        if ( !result.isEmpty()){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}


void Colormap::_setColorStates( Controller* controller ){
    if ( controller ){
        bool global = m_state.getValue<bool>( GLOBAL );
        std::vector< std::shared_ptr<ColorState> > selectedColorStates = controller->getSelectedColorStates( global);
        int stateColorCount = selectedColorStates.size();
        if ( stateColorCount > 0 ){
            m_stateColors.clear();

            for ( int i = 0; i < stateColorCount; i++ ){
                m_stateColors.push_back( selectedColorStates[i] );
            }

            //Update the state the client is listening to.
            _colorStateChanged();
        }
    }
}

QString Colormap::setDataTransform( const QString& transformString){
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setDataTransform( transformString );
        if ( !result.isEmpty() ){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}

void Colormap::_setErrorMargin( ){
    int significantDigits = getSignificantDigits();
    m_errorMargin = 1.0/qPow(10,significantDigits);
}


QString Colormap::setGamma( double gamma ){
    int stateColorCount = m_stateColors.size();
    QString result;
    int digits = getSignificantDigits();
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setGamma( gamma, m_errorMargin, digits );
        if ( !result.isEmpty()){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}

void Colormap::setGlobal( bool global ){
    //Update the color maps based on whether we should make a change
    //to all of them or only the selected ones.
    bool oldGlobal = isGlobal();
    if ( global != oldGlobal ){
        m_state.setValue<bool>(GLOBAL, global );

        //Update the list of color states based on the global
        //flag.
        Controller* controller = _getControllerSelected();
        _setColorStates( controller );

        _colorStateChanged();
    }
}



QString Colormap::setIntensityRange( double minValue, double maxValue ){
    QString result;
    if ( minValue < maxValue ){
        double minRounded = Util::roundToDigits( minValue, getSignificantDigits() );
        double maxRounded = Util::roundToDigits( maxValue, getSignificantDigits() );
        double oldMin = m_stateData.getValue<double>( INTENSITY_MIN );
        double oldMax = m_stateData.getValue<double>( INTENSITY_MAX );
        if ( qAbs( minRounded - oldMin) > m_errorMargin ||
                qAbs( maxRounded - oldMax) > m_errorMargin ){
            //Store the values.
            m_stateData.setValue<double>( INTENSITY_MIN, minRounded );
            m_stateData.setValue<double>( INTENSITY_MAX, maxRounded );
            m_stateData.flushState();
            _updateImageClips();
            _colorStateChanged();
        }
    }
    else {
        result = "The minimum intensity bound must be less than the maximum";
    }
    return result;
}

QString Colormap::setInvert( bool invert ){
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        m_stateColors[i]->_setInvert( invert );
    }
    if ( stateColorCount == 0 ){
        result = "There were no color maps to invert.";
    }
    else {
        _colorStateChanged();
    }
    return result;
}


QString Colormap::setImageUnits( const QString& unitsStr ){
    QString result;
    QString actualUnits = m_intensityUnits->getActualUnits( unitsStr);
    if ( !actualUnits.isEmpty() ){
        QString oldUnits = m_state.getValue<QString>( IMAGE_UNITS );
        if ( oldUnits != actualUnits ){
            // We will need to recalculate the current intensity min and max from the percentage min and max in the new units
            // So we need a converter from the pixel units to the *new* image units
            bool success(false);
            std::pair<double, double> values;
            
            try {
                Carta::Lib::IntensityUnitConverter::SharedPtr converter = _getIntensityConverter(actualUnits);
                values = _getIntensities(success, converter );
            } catch (const QString & error) {
                qWarning() << "Could not set image units:" << error;
                return "Could not set image units: " + error;
            }

            if (success) {
                //Set the units
                m_state.setValue<QString>( IMAGE_UNITS, actualUnits );
                m_state.flushState();

                //Set the converted values
                double intMin = Util::roundToDigits( values.first, getSignificantDigits() );
                m_stateData.setValue<double>( INTENSITY_MIN, intMin );
                double intMax = Util::roundToDigits( values.second, getSignificantDigits() );
                m_stateData.setValue<double>( INTENSITY_MAX, intMax );
                m_stateData.flushState();

                // flush the colormap bar labels
                _calculateColorLabels();
                // we need to flush the "m_state" again in order to catch the first unit conversion results
                m_state.flushState();
            } else {
                result = QString("Could not set image units. Unable to convert intensity values to %1.").arg(actualUnits);
            }
        }
    } else {
        result = "Could not set image units. New units are empty.";
    }
    return result;
}


QString Colormap::setNanColor( int redValue, int greenValue, int blueValue){
    int stateColorCount = m_stateColors.size();
    QString result;
    if ( isNanDefault() ){
        setNanDefault( false );
    }
    for ( int i = 0; i < stateColorCount; i++ ){
        result = m_stateColors[i]->_setNanColor( redValue, greenValue, blueValue );
        if ( !result.isEmpty()){
            break;
        }
    }
    if ( result.isEmpty() ){
        _colorStateChanged();
    }
    return result;
}

QString Colormap::setNanDefault( bool nanDefault ) {
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        m_stateColors[i]->_setNanDefault( nanDefault );
    }
    if ( stateColorCount == 0 ){
        result = "There were no color maps to for nan default.";
    }
    else {
        _colorStateChanged();
    }

    return result;
}

QString Colormap::setReverse( bool reverse ){
    int stateColorCount = m_stateColors.size();
    QString result;
    for ( int i = 0; i < stateColorCount; i++ ){
        m_stateColors[i]->_setReverse( reverse );
    }
    if ( stateColorCount == 0 ){
        result = "There were no color maps to reverse.";
    }
    else {
        _colorStateChanged();
    }

    return result;
}

QString Colormap::setSignificantDigits( int digits ){
    QString result;
    if ( digits <= 0 ){
        result = "Invalid colormap significant digits; must be positive:  "+QString::number( digits );
    }
    else {
        if ( getSignificantDigits() != digits ){
            m_state.setValue<int>(Util::SIGNIFICANT_DIGITS, digits );
            _setErrorMargin();
            //emit colorStateChanged();
        }
    }
    return result;
}

QString Colormap::setTabIndex( int index ){
    QString result;
    if ( index >= 0 ){
        int oldTabIndex = getTabIndex();
        if ( oldTabIndex != index ){
            m_state.setValue<int>( Util::TAB_INDEX, index );
            m_state.flushState();
        }
    }
    else {
        result = "Image settings tab index must be nonnegative: "+ QString::number(index);
    }
    return result;
}

void Colormap::_updateImageClips(){
    double minClip = m_stateData.getValue<double>( INTENSITY_MIN );
    double maxClip = m_stateData.getValue<double>( INTENSITY_MAX );
    
    Controller* controller = _getControllerSelected();
    
    if ( controller ){
        try {
            // NB: this is a converter *from* to pixel units *to* the image units, not the other way around,
            // because we apply it in reverse to the intensities, not to the image values
            Carta::Lib::IntensityUnitConverter::SharedPtr converter = _getIntensityConverter(getImageUnits());
            std::vector<double> percentiles = controller->getPercentiles( -1, -1, {minClip, maxClip}, converter );
            controller->applyClips( percentiles[0], percentiles[1] );
        } catch (const QString & error) {
            qWarning() << "Could not update image clips:" << error;
        }

    }
}

void Colormap::_updateIntensityBounds( double minPercent, double maxPercent, bool autoClip ){

    bool success(false);
    std::pair<double, double> intensities;

    try {
        Carta::Lib::IntensityUnitConverter::SharedPtr converter = _getIntensityConverter(getImageUnits());
        intensities = _getIntensities(success, minPercent, maxPercent, converter);
    }  catch (const QString & error) {
        qWarning() << "Could not update intensity bounds:" << error;
    }

    if ( success ){
        double minIntensity = Util::roundToDigits( intensities.first, getSignificantDigits());
        double maxIntensity = Util::roundToDigits( intensities.second, getSignificantDigits());

        bool intensityChanged = false;
        
        double oldMinIntensity = m_stateData.getValue<double>( INTENSITY_MIN );
        if ( qAbs( oldMinIntensity - minIntensity ) > m_errorMargin ){
            intensityChanged = true;
            m_stateData.setValue<double>( INTENSITY_MIN, minIntensity );
        }

        double oldMaxIntensity = m_stateData.getValue<double>( INTENSITY_MAX );
        if ( qAbs( oldMaxIntensity - maxIntensity ) > m_errorMargin ){
            intensityChanged = true;
            m_stateData.setValue<double>( INTENSITY_MAX, maxIntensity );
        }
        
        if ( intensityChanged || autoClip == false ){
            m_stateData.setValue<double>( PERCENT_MIN, minPercent );
            m_stateData.setValue<double>( PERCENT_MAX, maxPercent );
            _colorStateChanged();
            m_stateData.flushState();
        }

    }
}

Colormap::~Colormap(){
}
}
}
