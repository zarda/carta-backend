! include(../common.pri) {
  error( "Could not find the common.pri file!" )
}

! include(./proto_compile.pri) {
  error( "Could not find the proto_compile.pri file!" )
}

$$system(cp Proto/control/*.proto Proto/)
$$system(cp Proto/shared/*.proto Proto/)
$$system(cp Proto/request/*.proto Proto/)
$$system(cp Proto/stream/*.proto Proto/)

QT       += network xml

TARGET = CartaLib
TEMPLATE = lib

DEFINES += CARTALIB_LIBRARY

PROTOS = Proto/enums.proto \
    Proto/defs.proto \
    Proto/register_viewer.proto \
    Proto/file_list.proto \
    Proto/file_info.proto \
    Proto/open_file.proto \
    Proto/set_image_view.proto \
    Proto/raster_image.proto \
    Proto/spectral_profile.proto \
    Proto/spatial_profile.proto \
    Proto/set_image_channels.proto \
    Proto/set_cursor.proto \
    Proto/region_stats.proto \
    Proto/region_requirements.proto \
    Proto/region_histogram.proto \
    Proto/region.proto \
    Proto/error.proto \
    Proto/contour_image.proto \
    Proto/contour.proto \
    Proto/close_file.proto \
    Proto/animation.proto

SOURCES += \
    CartaLib.cpp \
    HtmlString.cpp \
    LinearMap.cpp \
    Hooks/ColormapsScalar.cpp \
    Hooks/ConversionIntensityHook.cpp \
    Hooks/ConversionSpectralHook.cpp \
    Hooks/Fit1DHook.cpp \
    Hooks/FitResult.cpp \
    Hooks/Histogram.cpp \
    Hooks/HistogramResult.cpp \
    Hooks/ProfileHook.cpp \
    Hooks/ImageStatisticsHook.cpp \
    Hooks/LoadRegion.cpp \
    Hooks/Plot2DResult.cpp \
    Hooks/ProfileResult.cpp \
    IImage.cpp \
    PixelType.cpp \
    Slice.cpp \
    AxisInfo.cpp \
    AxisLabelInfo.cpp \
    AxisDisplayInfo.cpp \
    Fit1DInfo.cpp \
    ICoordinateFormatter.cpp \
    IPlotLabelGenerator.cpp \
    Hooks/LoadAstroImage.cpp \
    PixelPipeline/CustomizablePixelPipeline.cpp \
    ProfileInfo.cpp \
    PWLinear.cpp \
    StatInfo.cpp \
    VectorGraphics/VGList.cpp \
    VectorGraphics/BetterQPainter.cpp \
    Algorithms/ContourConrec.cpp \
    IWcsGridRenderService.cpp \
    ContourSet.cpp \
    Algorithms/LineCombiner.cpp \
    IImageRenderService.cpp \
    IRemoteVGView.cpp \
    IPCache.cpp \
    Hooks/GetPersistentCache.cpp \
    Hooks/GetProfileExtractor.cpp \
    Regions/IRegion.cpp \
    InputEvents.cpp \
    Regions/ICoordSystem.cpp \
    Hooks/CoordSystemHook.cpp \
    Regions/CoordinateSystemFormatter.cpp \
    Regions/Ellipse.cpp \
    Regions/Point.cpp \
    Regions/Rectangle.cpp \
    IntensityUnitConverter.cpp \
    IntensityCacheHelper.cpp

HEADERS += \
    CartaLib.h\
    cartalib_global.h \
    HtmlString.h \
    LinearMap.h \
    Hooks/ColormapsScalar.h \
    Hooks/ConversionIntensityHook.h \
    Hooks/ConversionSpectralHook.h \
    Hooks/Fit1DHook.h \
    Hooks/FitResult.h \
    Hooks/Histogram.h \
    Hooks/HistogramResult.h \
    Hooks/ProfileHook.h \
    Hooks/HookIDs.h \
    Hooks/ImageStatisticsHook.h \
    Hooks/LoadRegion.h \
    Hooks/Plot2DResult.h \
    Hooks/ProfileResult.h \
    IPlugin.h \
    IImage.h \
    PixelType.h \
    Nullable.h \
    Slice.h \
    AxisInfo.h \
    AxisLabelInfo.h \
    AxisDisplayInfo.h \
    Fit1DInfo.h \
    ICoordinateFormatter.h \
    IPlotLabelGenerator.h \
    Hooks/LoadAstroImage.h \
    TPixelPipeline/IScalar2Scalar.h \
    PixelPipeline/IPixelPipeline.h \
    PixelPipeline/CustomizablePixelPipeline.h \
    ProfileInfo.h \
    PWLinear.h \
    StatInfo.h \
    VectorGraphics/VGList.h \
    Hooks/GetWcsGridRenderer.h \
    Hooks/LoadPlugin.h \
    VectorGraphics/BetterQPainter.h \
    Algorithms/ContourConrec.h \
    IWcsGridRenderService.h \
    IContourGeneratorService.h \
    ContourSet.h \
    Algorithms/LineCombiner.h \
    Hooks/GetInitialFileList.h \
    Hooks/Initialize.h \
    IImageRenderService.h \
    Hooks/GetImageRenderService.h \
    IRemoteVGView.h \
    Hooks/GetProfileExtractor.h \
    Regions/IRegion.h \
    InputEvents.h \
    Regions/ICoordSystem.h \
    Hooks/CoordSystemHook.h \
    Regions/CoordinateSystemFormatter.h \
    Hooks/GetPersistentCache.h \
    Regions/Ellipse.h \
    Regions/Point.h \
    Regions/Rectangle.h \
    IPCache.h \
    IntensityUnitConverter.h \
    IPercentileCalculator.h \
    IntensityCacheHelper.h

INCLUDEPATH += ../../../ThirdParty/protobuf/include
LIBS += -L../../../ThirdParty/protobuf/lib -lprotobuf

unix {
    target.path = /usr/lib
    INSTALLS += target
}

OTHER_FILES += \
    readme.txt

DISTFILES += \
    VectorGraphics/vg.txt
