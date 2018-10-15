/**
 * Hook for generating profile data.
 *
 **/

#pragma once
#include "ProfileResult.h"
#include "CartaLib/CartaLib.h"
#include "CartaLib/IPlugin.h"
#include "CartaLib/ProfileInfo.h"
#include "CartaLib/Hooks/ProfileResult.h"

namespace Carta
{
namespace Lib
{
namespace Image {
class ImageInterface;
}
namespace Regions {
class RegionBase;
}
namespace Hooks
{



class ProfileHook : public BaseHook
{
    CARTA_HOOK_BOILER1( ProfileHook );

public:
   //The results from generating a profile.
    typedef Carta::Lib::Hooks::ProfileResult ResultType;

    /**
     * @brief Params
     */
     struct Params {

            Params( std::shared_ptr<Image::ImageInterface> dataSource,
                    std::shared_ptr<Carta::Lib::Regions::RegionBase> regionInfo,
                    Carta::Lib::ProfileInfo profileInfo ){
                m_dataSource = dataSource;
                m_regionInfo = regionInfo;
                m_profileInfo = profileInfo;
            }

            // overload for regionInfo == nullptr (whole image)
            Params( std::shared_ptr<Image::ImageInterface> dataSource,
                    std::shared_ptr<Carta::Lib::Regions::RegionBase> regionInfo,
                    int x, int y,
                    Carta::Lib::ProfileInfo profileInfo ){
                m_dataSource = dataSource;
                m_regionInfo = regionInfo;
                m_profileInfo = profileInfo;
                m_x = x;
                m_y = y;
            }

            std::shared_ptr<Image::ImageInterface> m_dataSource;
            std::shared_ptr<Carta::Lib::Regions::RegionBase> m_regionInfo;
            Carta::Lib::ProfileInfo m_profileInfo;
            int m_x;
            int m_y;
        };

    /**
     * @brief PreRender
     * @param pptr
     *
     * @todo make hook constructors protected, so that only hook helper can create them
     */
    ProfileHook( Params * pptr ) : BaseHook( staticId ), paramsPtr( pptr )
    {
        CARTA_ASSERT( is < Me > () );
    }

    ResultType result;
    Params * paramsPtr;
};
}
}
}
