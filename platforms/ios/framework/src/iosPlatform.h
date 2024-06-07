#pragma once

#include "platform.h"

@class TGMapView;
@class TGDefaultURLHandler;

namespace Tangram {

class iOSPlatform : public Platform {

public:

    iOSPlatform(__weak TGMapView* _mapView);
    void shutdown() override {}
#ifdef TANGRAM_IOS_MAPVIEW
    void requestRender() const override;
    void setContinuousRendering(bool _isContinuous) override;
#endif
    std::vector<FontSourceHandle> systemFontFallbacksHandle() const override;
    FontSourceHandle systemFont(const std::string& _name, const std::string& _weight, const std::string& _face) const override;
    bool startUrlRequestImpl(const Url& _url, const HttpOptions& _options, const UrlRequestHandle _request, UrlRequestId& _id) override;
    void cancelUrlRequestImpl(const UrlRequestId _id) override;

private:

    __weak TGMapView* m_mapView;
    TGDefaultURLHandler* m_urlHandler;
};

} // namespace Tangram
