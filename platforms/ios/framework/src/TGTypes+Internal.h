//
//  TGUtils.h
//  tangram
//
//  Created by Matt Blair on 8/22/18.
//

#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#import <UIKit/UIKit.h>
#import "TGTypes.h"

#include "map.h"

NS_INLINE Tangram::EaseType TGConvertTGEaseTypeToCoreEaseType(TGEaseType easeType) {
    switch (easeType) {
        case TGEaseTypeLinear:
            return Tangram::EaseType::linear;
        case TGEaseTypeSine:
            return Tangram::EaseType::sine;
        case TGEaseTypeQuint:
            return Tangram::EaseType::quint;
        case TGEaseTypeCubic:
            return Tangram::EaseType::cubic;
        default:
            return Tangram::EaseType::cubic;
    }
}

NS_INLINE TGError TGConvertCoreErrorToTGError(Tangram::Error error) {
    switch (error) {
        case Tangram::Error::scene_update_path_yaml_syntax_error:
            return TGErrorSceneUpdatePathYAMLSyntaxError;
        case Tangram::Error::scene_update_path_not_found:
            return TGErrorSceneUpdatePathNotFound;
        case Tangram::Error::scene_update_value_yaml_syntax_error:
            return TGErrorSceneUpdateValueYAMLSyntaxError;
        case Tangram::Error::no_valid_scene:
            return TGErrorNoValidScene;
        case Tangram::Error::none:
            return TGErrorNone;
    }
}

NS_INLINE CLLocationCoordinate2D TGConvertCoreLngLatToCLLocationCoordinate2D(Tangram::LngLat lngLat) {
    return CLLocationCoordinate2DMake(lngLat.latitude, lngLat.longitude);
}

NS_INLINE Tangram::LngLat TGConvertCLLocationCoordinate2DToCoreLngLat(CLLocationCoordinate2D coordinate) {
    return Tangram::LngLat(coordinate.longitude, coordinate.latitude);
}

NS_INLINE Tangram::EdgePadding TGConvertUIEdgeInsetsToCoreEdgePadding(UIEdgeInsets insets, float pixelScale) {
    // Core library expects padding values in pixels, but UIEdgeInsets conventionally uses points.
    // Convert to pixels using pixelScale. Note that the converted values are truncated to integers.
    return Tangram::EdgePadding(insets.left * pixelScale, insets.top * pixelScale, insets.right * pixelScale, insets.bottom * pixelScale);
}

NS_INLINE UIEdgeInsets TGConvertCoreEdgePaddingToUIEdgeInsets(Tangram::EdgePadding padding, float pixelScale) {
    float pointScale = 1.f / pixelScale;
    return UIEdgeInsetsMake(padding.top * pointScale, padding.left * pointScale, padding.bottom * pointScale, padding.right * pointScale);
}

NSError* TGConvertCoreSceneErrorToNSError(const Tangram::SceneError *sceneError);
