#include "CasaImageLoader.h"
#include "CCImage.h"
#include "CartaLib/Hooks/Initialize.h"
#include "CartaLib/Hooks/LoadAstroImage.h"
#include <QDebug>
#include <QPainter>
#include <QTime>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/images/Images/FITSImage.h>
#include <casacore/images/Images/MIRIADImage.h>
#include <casacore/images/Images/HDF5Image.h>
#include <casacore/images/Images/ImageExpr.h>
#include <casacore/images/Images/ImageExprParse.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/Quanta.h>
#include <memory>
#include <algorithm>
#include <cstdint>
#include "CartaLib/UtilCASA.h"

CasaImageLoader::CasaImageLoader(QObject *parent) :
    QObject(parent)
{
}

bool CasaImageLoader::handleHook(BaseHook & hookData)
{
    qDebug() << "CasaImageLoader plugin is handling hook #" << hookData.hookId();
    if( hookData.is<Carta::Lib::Hooks::Initialize>()) {
        // Register FITS and Miriad image types
        casacore::FITSImage::registerOpenFunction();
        casacore::MIRIADImage::registerOpenFunction();
        return true;
    }

    else if(hookData.is<Carta::Lib::Hooks::LoadAstroImage>()) {
        Carta::Lib::Hooks::LoadAstroImage & hook
                = static_cast<Carta::Lib::Hooks::LoadAstroImage &>(hookData);
        auto fname = hook.paramsPtr->fileName;
        hook.result = loadImage(fname);
        // return true if result is not null
        return hook.result != nullptr;
    }

    qWarning() << "Sorry, dont' know how to handle this hook";
    return false;
}

std::vector<HookId> CasaImageLoader::getInitialHookList()
{
    return {
        Carta::Lib::Hooks::Initialize::staticId,
        Carta::Lib::Hooks::LoadAstroImage::staticId
    };
}

template <typename T>
static CCImageBase::SharedPtr tryCast( casacore::LatticeBase * lat)
{
    typedef casacore::ImageInterface<T> CCIT;
    CCIT * cii = dynamic_cast<CCIT *>(lat);
    typename CCImage<T>::SharedPtr res = nullptr;
    if(cii) {
        res = CCImage<T>::create(cii);
    }
    return res;
}

///
/// \brief Attempts to load an image using casacore library, namely the very first
/// frame of it. Then converts the frame to a QImage using 100% histogram clip values.
/// \param fname file name with the image
/// \param result where to store the result
/// \return true if successful, false otherwise
///
Carta::Lib::Image::ImageInterface::SharedPtr CasaImageLoader::loadImage(const QString & fname)
{
    qDebug() << "CasaImageLoader plugin trying to load image: " << fname;

    // get the image type
    casacore::ImageOpener::ImageTypes filetype;
    try {
        filetype = casacore::ImageOpener::imageType(fname.toStdString());
    } catch (...) {
        qDebug() << "Get image typed failed";
        return nullptr;
    }

    // load image: casacore::ImageOpener::openPagedImage will emit exception if failed,
    // so need to catch the exception for error handling.
    // However, it's a strange design by throwing exception instead of returning nullptr in lat when opening file failed,
    // maybe it's a bug (crashed inside openPagedImage) in casacore (because not mentioned in casacore document)
    bool success = true;
    casacore::LatticeBase * lat = nullptr;
    if(filetype == casacore::ImageOpener::ImageTypes::AIPSPP) {
        qDebug() << "\t-opened as paged image";

        casa_mutex.lock();
        try {
            lat = casacore::ImageOpener::openPagedImage(fname.toStdString());
        } catch (...) {
            success = false;
            qDebug() << "\t-ERROR: open paged image failed.";
        }
        casa_mutex.unlock();
    } else if (filetype != casacore::ImageOpener::ImageTypes::UNKNOWN) {
        qDebug() << "CasaImageLoader plugin tries to load non-casa image";
        if (filetype == casacore::ImageOpener::ImageTypes::FITS) {
            qDebug() << "\t-opened as FITS image";
        } else if(filetype == casacore::ImageOpener::ImageTypes::MIRIAD) {
            qDebug() << "\t-opened as MIRIAD image";
        } else {
            qDebug() << "\t-opened as unpaged image";
        }

        casa_mutex.lock();
        try {
            lat = casacore::ImageOpener::openImage(fname.toStdString());
        } catch (...) {
            success = false;
            qDebug() << "\t-ERROR: open image failed.";
        }
        casa_mutex.unlock();
    } else {
        success = false;
        qDebug() << "unknow format \t-out of ideas, bailing out";
    }

    if (!success) {
        qDebug() << "Load image failed.";
        return nullptr;
    }

    if (0 == lat) {
        qDebug() << "unknow format \t-out of ideas, bailing out";
        return nullptr;
    }

    qDebug() << "CasaImageLoader plugin trying to load image 5 ";

    lat->reopen();
    //qDebug() << "lat=" << lat;
    auto shape = lat->shape();
    auto shapes = shape.asStdVector();
    qDebug() << "lat.shape = " << std::string( lat->shape().toString()).c_str();
    qDebug() << "lat.dataType = " << lat->dataType();
    qDebug() << "Float type is " << casacore::TpFloat;

    CCImageBase::SharedPtr res;
    res = tryCast<float>(lat);
    // Please note that the following code will not be reached
    // even if the FITS file is defined in 64 bit
    // and FitsHeaderExtractor::_CasaFitsConverter assumes that
    // image is load in casacore::ImageInterface < casacore::Float > format
    if( ! res) res = tryCast<double>(lat);
    if( ! res) res = tryCast<u_int8_t>(lat);
    if( ! res) res = tryCast<int16_t>(lat);
    if( ! res) res = tryCast<int32_t>(lat);
    if( ! res) res = tryCast<casacore::Int>(lat);
    //Certain image related functions are defined only for Int, there is no
    //long long in the image class right now.  TempImage<int32_t> fails to
    //compile, for example.
    //if( ! res) res = tryCast<int64_t>(lat);

    qDebug() << "CasaImageLoader plugin trying to load image 6 ";

    // if we were successful, return the result
    if( res) {
        return res;
    }

    // if the initial conversion attempt failed, try a LEL expression
/*
    casacore::ImageInterface<casacore::Float> * img = 0;
    try {
        qDebug() << "Trying LEL conversion";
        std::string expr = "float('" + fname.toStdString() + "')";
        qDebug() << "Espression is " << expr.c_str();
        casacore::LatticeExpr<casacore::Float> le ( casacore::ImageExprParse::command( expr ));
        img = new casacore::ImageExpr<casacore::Float> ( le, expr );
        qDebug() << "\t-LEL conversion successful";
        return CCImage<float>::create( img);
    } catch ( ... ) {}
*/

    // indicate failure
    qWarning() << "Unsupported lattice type:" << lat-> dataType();
    delete lat;
    return nullptr;
}

CasaImageLoader::~CasaImageLoader(){
}
