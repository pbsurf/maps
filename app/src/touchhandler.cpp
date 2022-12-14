#include "touchhandler.h"
#include "mapsapp.h"

#include "glm/glm.hpp"


static constexpr float flingInvTau = 1/0.05f;  // time constant for smoothing of pan speed
static constexpr float pinchThreshold = 60;  // pixels (change in distance between fingers)
static constexpr float rotateThreshold = 0.25f;  // radians
static constexpr float shoveThreshold = 60;  // pixels (translation of centroid of fingers)

static constexpr float maxTapDist = 20;  // max pixels between start and end points for a tap
static constexpr float minFlingDist = 150;
static constexpr double maxTapTime = 0.25;  // seconds
static constexpr double minDblTapTime = 0.04;  // min time between end of first tap and start of second
static constexpr double maxDblTapTime = 0.25;  // max time between end of first tap and start of second
static constexpr double minFlingTime = 0.03;
static constexpr double minLongPressTime = 0.7;  // 0.5s is typical on Android


// action: -1 = release, 0 = move, 1 = press
void TouchHandler::touchEvent(int ptrId, int action, double t, float x, float y, float p)
{
  // unfortunately, glfw doesn't provide input event timestamps, so we have to use glfwGetTime() when we
  //  receive event, which results in inaccuracies and undesired fling gestures
  //LOGW("touchEvent: %d, t: %f, x: %f, y: %f", action, t, x, y);
  Map* map = app->map;
  size_t prevpoints = touchPoints.size();
  auto it = touchPoints.begin();
  while(it < touchPoints.end() && it->id != ptrId) { ++it; }
  if(it != touchPoints.end()) {
    if(action == -1) {
      touchPoints.erase(it);
    }
    else {
      if(action == 1)
        LOGE("Duplicate touch press event received!");
      *it = TouchPoint{ptrId, x, y, p};
    }
  }
  else {
    if(action > 0)
      touchPoints.push_back({ptrId, x, y, p});
    else if(action == 0)
      app->hoverEvent(x, y);
    else
      LOGE("Release event received for unknown touch point!");
  }

  if(touchPoints.empty()) {
    if(prevpoints == 1 && multiTouchState == TOUCH_NONE) {
      double dt = t - initTime;
      float dr = glm::distance(prevCOM, initCOM);
      if(dt < maxTapTime && dr < maxTapDist) {
        map->handlePanGesture(prevCOM.x, prevCOM.y, initCOM.x, initCOM.y);  // undo any panning
        if(initTime - prevTapTime < maxDblTapTime) {
          if(initTime - prevTapTime > minDblTapTime)
            app->doubleTapEvent(initCOM.x, initCOM.y);
          prevTapTime = 0;
        }
        else {
          // TODO: this should be called after double tap delay (assuming we use SvgGui, we can use a Timer)
          app->tapEvent(initCOM.x, initCOM.y);
          prevTapTime = t;
        }
      }
      else if(dt > minFlingTime && dr > minFlingDist && (t - prevTime)*flingInvTau < 1) {
        flingV = glm::clamp(flingV, -4000.0f, 4000.0f);
        map->handleFlingGesture(prevCOM.x, prevCOM.y, flingV.x, flingV.y);
        //LOGW("Fling: v = %f, %f", flingV.x, flingV.y);
      }
      //else { LOGW("No fling: total dt = %f, dr = %f, final dt = %f", dt, dr, t - prevTime); }
    }
    multiTouchState = TOUCH_NONE;
    return;
  }

  glm::vec2 pt(touchPoints.front().x, touchPoints.front().y);
  if(touchPoints.size() > 1) {
    glm::vec2 pt2(touchPoints.back().x, touchPoints.back().y);
    glm::vec2 com = 0.5f*(pt + pt2);
    glm::vec2 dr = pt2 - pt;
    float dist = glm::length(dr);
    float angle = glm::atan(dr.y, dr.x);
    if(touchPoints.size() > prevpoints) {
      multiTouchState = TOUCH_NONE;
      canBeLongPress = false;
      flingV = {0, 0};
    }
    else {
      if(multiTouchState == TOUCH_NONE) {
        if(std::abs(dist - prevDist) > pinchThreshold)
          multiTouchState = TOUCH_PINCH;
        else if(std::abs(angle - prevAngle) > rotateThreshold)
          multiTouchState = TOUCH_ROTATE;
        else if(std::abs(com.y - prevCOM.y) > shoveThreshold)
          multiTouchState = TOUCH_SHOVE;
      }
      if(multiTouchState == TOUCH_PINCH) {
        map->handlePanGesture(prevCOM.x, prevCOM.y, com.x, com.y);
        map->handlePinchGesture(com.x, com.y, dist/prevDist, 0.f);
      }
      else if(multiTouchState == TOUCH_ROTATE)
        map->handleRotateGesture(com.x, com.y, angle - prevAngle);
      else if(multiTouchState == TOUCH_SHOVE)
        map->handleShoveGesture(com.y - prevCOM.y);
    }
    if(multiTouchState != TOUCH_NONE || touchPoints.size() > prevpoints) {
      prevCOM = com;
      prevDist = dist;
      prevAngle = angle;
    }
  }
  else if(prevpoints > 0) {
    if(prevpoints == 1) {
      // TODO: this should be done by setting a timer
      if(canBeLongPress && t - initTime > minLongPressTime) {
        if(glm::distance(prevCOM, initCOM) < maxTapDist)
          app->longPressEvent(initCOM.x, initCOM.y);
        canBeLongPress = false;
      }
      map->handlePanGesture(prevCOM.x, prevCOM.y, pt.x, pt.y);
      if(t > prevTime) {
        // single pole IIR low pass filter for fling velocity
        float a = 0;  //std::exp(-(t - prevTime)*flingInvTau);
        flingV = a*flingV + (1-a)*(pt - prevCOM)/float(t - prevTime);
      }
    }
    prevCOM = pt;
    prevTime = t;
  }
  else if(prevpoints == 0) {
    map->handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);  // cancel any previous motion
    prevTime = initTime = t;
    prevCOM = initCOM = pt;
    flingV = {0, 0};
    canBeLongPress = true;
  }
}
