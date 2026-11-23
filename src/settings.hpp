#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gurumin {
struct Resolution {
  int width, height;
  bool operator==(const Resolution &other) const {
    return width == other.width && height == other.height;
  }
};
inline bool validResolution(Resolution r) {
  return r.width >= 640 && r.width <= 7680 && r.height >= 480 &&
         r.height <= 4320;
}
inline void addResolution(std::vector<Resolution> &list, Resolution r) {
  if (validResolution(r) &&
      std::find(list.begin(), list.end(), r) == list.end())
    list.push_back(r);
}
inline std::vector<Resolution> resolutionChoices(Resolution desktop,
                                                 Resolution current) {
  std::vector<Resolution> result;
  for (Resolution r : {Resolution{800, 600},
                       {1024, 600},
                       {1280, 720},
                       {1280, 960},
                       {1680, 1050},
                       {1920, 1080},
                       {1920, 1200},
                       {2560, 1080},
                       {2560, 1440},
                       {2560, 1600},
                       {3440, 1440},
                       {3840, 1600},
                       {3840, 2160},
                       {5120, 1440},
                       {5120, 2160}})
    addResolution(result, r);
  addResolution(result, desktop);
  addResolution(result, current);
  return result;
}
struct ModernSettings {
  Resolution resolution{0, 0}; // Absent keys preserve native/installed choice.
  int frameCap = 0; // 0: native VSync; -1: uncapped; 30..1000: numeric.
  bool freeCamera = false, cameraOutOfTownOnly = true;
  bool invertX = true, invertY = false;
  int cameraYawSpeed = 120, cameraPitchSpeed = 90, cameraDeadzone = 8689;
  // Free camera distance in percent of the authored distance, set in game.
  int cameraDistance = 100;
  bool cameraStickZoom = false; // Right stick up/down zooms instead of tilting.
  // Mouse wheel zoom: 0 never, 1 when the game's own wheel rotation is off
  // (Ctrl+wheel always), 2 always.
  int cameraWheelZoom = 1;
  int cameraZoomInKey = 0x21, cameraZoomOutKey = 0x22; // PageUp / PageDown.
  int shadowResolution = 256;
  int antiAliasing = 0; // Native, FXAA Low, FXAA High.
  bool uiFiltering = true, worldFiltering = true;
  bool vrEnabled = false;
  int vrScale = 0; // 0 life size, 1 diorama.
};
inline bool validVrScale(int scale) { return scale == 0 || scale == 1; }
// "-vr" on the command line enables VR for one launch without saving it.
inline bool commandLineRequestsVr(const char *line) {
  if (!line)
    return false;
  bool quoted = false;
  const char *token = nullptr;
  for (const char *p = line;; ++p) {
    char c = *p;
    bool separator = c == 0 || ((c == ' ' || c == '\t') && !quoted);
    if (c == '"')
      quoted = !quoted;
    if (separator) {
      if (token) {
        std::size_t length = std::size_t(p - token);
        auto lower = [](char x) { return x >= 'A' && x <= 'Z' ? char(x - 'A' + 'a') : x; };
        auto is = [&](const char *word) {
          std::size_t n = 0;
          while (word[n]) ++n;
          if (n != length) return false;
          for (std::size_t i = 0; i < n; ++i)
            if (lower(token[i]) != word[i]) return false;
          return true;
        };
        if (is("-vr") || is("--vr") || is("/vr"))
          return true;
        token = nullptr;
      }
      if (!c)
        return false;
    } else if (!token)
      token = p;
  }
}
// Room/shop identity remains useful for indoor collision, independently of
// the town-only freecam exclusion. Native camera mode is not an indoor flag.
inline bool cameraInterior(int scene, const char *asset, std::size_t capacity) {
  if (scene < 2 || scene > 5 || !asset) return false;
  std::size_t length=0;
  while(length<capacity && asset[length]) ++length;
  if(length==capacity || (length!=6 && length!=10)) return false;
  auto lower=[](char c) { return c>='A' && c<='Z' ? char(c-'A'+'a') : c; };
  const char *stems[]={"mp_020","mp_030","mp_040","mp_050"};
  for(std::size_t i=0;i<6;++i)
    if(lower(asset[i])!=stems[scene-2][i]) return false;
  if(length==10) {
    const char *extension=".it3";
    for(std::size_t i=0;i<4;++i)
      if(lower(asset[6+i])!=extension[i]) return false;
  }
  return true;
}
// The decoded town record is scene 0x0c, not every outdoor/fixed camera.
inline bool cameraTown(int scene, const char *asset, std::size_t capacity) {
  if(scene!=12 || !asset) return false;
  std::size_t length=0;
  while(length<capacity && asset[length]) ++length;
  if(length==capacity || (length!=6 && length!=10)) return false;
  const char *name="mp_0c0.it3";
  for(std::size_t i=0;i<length;++i) {
    char c=asset[i];
    if(c>='A' && c<='Z') c=char(c-'A'+'a');
    if(c!=name[i]) return false;
  }
  return true;
}
inline bool validShadowResolution(int edge) {
  return edge == 256 || edge == 512 || edge == 1024 || edge == 2048;
}
inline bool validAntiAliasing(int quality) {
  return quality >= 0 && quality <= 2;
}
inline bool validFrameCap(int cap) {
  return cap == -1 || cap == 0 || (cap >= 30 && cap <= 1000);
}

// An absolute presentation schedule. A slow frame never causes catch-up bursts.
struct FramePacer {
  double deadline = 0;
  int activeCap = 0;
  void reset() {
    deadline = 0;
    activeCap = 0;
  }
  double next(double now, int cap) {
    if (cap <= 0 || !std::isfinite(now)) {
      reset();
      return now;
    }
    double interval = 1. / cap;
    if (activeCap != cap || deadline == 0 || now < deadline - interval)
      deadline = now;
    activeCap = cap;
    deadline += interval;
    if (deadline < now)
      // Rendering already missed the cap's budget: don't impose another full
      // interval on a GPU-bound frame. Rebase here; the next frame gets one
      // interval, so no catch-up burst occurs.
      deadline = now;
    return deadline;
  }
};
struct Stick {
  float x = 0, y = 0;
};
inline Stick radialStick(int x, int y, int deadzone) {
  float fx = std::clamp(x / 32767.f, -1.f, 1.f);
  float fy = std::clamp(y / 32767.f, -1.f, 1.f);
  float length = std::sqrt(fx * fx + fy * fy);
  float dz = std::clamp(deadzone / 32767.f, 0.f, .9f);
  if (length <= dz)
    return {};
  float gain = (std::min(length, 1.f) - dz) / ((1.f - dz) * length);
  return {fx * gain, fy * gain};
}
// Native movement converter uses X/Y as the ground plane. The visible
// horizontal forward vector defines direction, independent of camera pitch.
inline bool cameraRelativeMovement(const float *eye, const float *target,
                                   float x, float y, float &outX, float &outY) {
  if(!eye || !target || !std::isfinite(x) || !std::isfinite(y)) return false;
  for(int i=0;i<3;++i)
    if(!std::isfinite(eye[i]) || !std::isfinite(target[i])) return false;
  double fx=double(target[0])-eye[0], fy=double(target[1])-eye[1];
  double length=std::hypot(fx,fy);
  if(length<.001) return false;
  fx/=length; fy/=length;
  float nx=float(fy*x+fx*y), ny=float(-fx*x+fy*y);
  if(!std::isfinite(nx) || !std::isfinite(ny)) return false;
  outX=nx; outY=ny;
  return true;
}
// Native yaw rotates X/Y, with Z untouched. Keep the target and radius intact.
inline bool orbitYaw(float *eye, const float *target, float radians) {
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(eye[i]) || !std::isfinite(target[i]))
      return false;
  if (!std::isfinite(radians))
    return false;
  float x = eye[0] - target[0], y = eye[1] - target[1];
  float c = std::cos(radians), s = std::sin(radians);
  eye[0] = target[0] + x * c - y * s;
  eye[1] = target[1] + x * s + y * c;
  return true;
}
// Orbit the returned authored anchor, without feeding user offsets back into
// native camera tracking/carry state. Zero offsets retain the native bits.
inline bool orbitFromAnchor(float *eye, const float *target, float yaw,
                            float pitch, float *acceptedPitch = nullptr) {
  if(!eye || !target || !std::isfinite(yaw) || !std::isfinite(pitch)) return false;
  for(int i=0;i<4;++i)
    if(!std::isfinite(eye[i]) || !std::isfinite(target[i])) return false;
  if(yaw==0 && pitch==0) { if(acceptedPitch) *acceptedPitch=0; return true; }
  double dx=double(eye[0])-target[0], dy=double(eye[1])-target[1];
  double dz=double(eye[2])-target[2];
  double horizontal=std::hypot(dx,dy), radius=std::hypot(horizontal,dz);
  if(radius<.001 || horizontal<.001) return false;
  double baseElevation=std::atan2(dz,horizontal);
  constexpr double limit=1.3962634015954636; // 80 degrees.
  double elevation=std::clamp(baseElevation+pitch,-limit,limit);
  double azimuth=std::atan2(dy,dx)+yaw;
  double groundRadius=radius*std::cos(elevation);
  float next[3]={float(target[0]+groundRadius*std::cos(azimuth)),
      float(target[1]+groundRadius*std::sin(azimuth)),
      float(target[2]+radius*std::sin(elevation))};
  for(float value:next) if(!std::isfinite(value)) return false;
  for(int i=0;i<3;++i) eye[i]=next[i];
  if(acceptedPitch) *acceptedPitch=float(elevation-baseElevation);
  return true;
}
constexpr int MinCameraDistance = 60, MaxCameraDistance = 250;
inline int clampCameraDistance(int percent) {
  return std::clamp(percent, MinCameraDistance, MaxCameraDistance);
}
// Move the eye along the target->eye line; the view direction is unchanged.
inline bool zoomCamera(float *eye, const float *target, float scale) {
  if (!eye || !target || !std::isfinite(scale) || scale <= 0)
    return false;
  float next[3];
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(eye[i]) || !std::isfinite(target[i]))
      return false;
    next[i] = target[i] + (eye[i] - target[i]) * scale;
    if (!std::isfinite(next[i]))
      return false;
  }
  for (int i = 0; i < 3; ++i)
    eye[i] = next[i];
  return true;
}
// Zoom requests: wheel notches and held keys change a target distance that
// the rendered distance eases toward each camera tick.
struct CameraZoom {
  float target = 100, current = 100;
  void reset(int percent) { target = current = float(clampCameraDistance(percent)); }
  void step(float percent) {
    if (std::isfinite(percent))
      target = std::clamp(target + percent, float(MinCameraDistance),
                          float(MaxCameraDistance));
  }
  // `ticks` elapsed 30 Hz camera ticks; returns true while still easing.
  bool ease(unsigned ticks) {
    for (unsigned i = 0; i < std::min(ticks, 30u); ++i)
      current += (target - current) * .25f;
    if (std::abs(target - current) < .05f)
      current = target;
    return current != target;
  }
  float scale() const { return current / 100.f; }
};
// Whether a wheel message zooms instead of reaching the game.
inline bool wheelZooms(int mode, bool gameRotatesWithWheel, bool control) {
  return mode == 2 || (mode == 1 && (!gameRotatesWithWheel || control));
}
inline bool loweringOrbit(Stick stick, const ModernSettings &settings) {
  if (settings.cameraStickZoom)
    return false;
  return (settings.invertY ? stick.y : -stick.y)<0;
}
inline bool cameraPitchCall(uintptr_t returnRva) {
  return returnRva == 0x314164 || returnRva == 0x3141e7 ||
         returnRva == 0x3145bb || returnRva == 0x3149d1 ||
         returnRva == 0x314b80;
}
// Bound geometric elevation while keeping native collision correction separate.
inline float cameraPitchAngle(float y, float z, float correction,
                              float &userPitch, bool primary) {
  if (!std::isfinite(y) || !std::isfinite(z) ||
      !std::isfinite(correction) || !std::isfinite(userPitch) ||
      y*y + z*z < .001f)
    return correction + userPitch;
  constexpr float limit = 1.3962634016f; // 80 degrees, avoids either pole.
  float base = std::atan2(-z, y);
  float angle = std::clamp(correction + userPitch, -limit - base, limit - base);
  if (primary)
    userPitch = angle - correction; // No stored input beyond the visible limit.
  return angle;
}
inline bool contractCamera(float *eye, const float *target, const float *hit,
                           float margin) {
  float delta[3], length2 = 0, hitProjection = 0;
  for (unsigned i=0;i<3;++i) {
    if (!std::isfinite(eye[i]) || !std::isfinite(target[i]) ||
        !std::isfinite(hit[i])) return false;
    delta[i] = eye[i]-target[i]; length2 += delta[i]*delta[i];
    hitProjection += (hit[i]-target[i])*delta[i];
  }
  if (length2 < .001f || hitProjection < 0 || hitProjection > length2)
    return false;
  float length = std::sqrt(length2);
  float hitDistance = hitProjection/length;
  if (hitDistance <= .0001f) return false;
  float distance = std::max(std::min(.01f, hitDistance*.5f), hitDistance - margin);
  if (distance >= length) return false;
  for (unsigned i=0;i<3;++i) eye[i] = target[i] + delta[i]*distance/length;
  return true;
}
// Only controller integration uses the tick; native camera/collision can still
// run as often as the engine needs. Suspensions rebase instead of accumulating.
struct OrbitControls {
  unsigned tick = ~0u;
  float pitch = 0;
  bool engaged = false;
  void suspend(bool discardPitch) {
    engaged = false;
    tick = ~0u;
    if (discardPitch)
      pitch = 0;
  }
  float advance(unsigned currentTick, Stick stick,
                const ModernSettings &settings) {
    if (engaged && tick == currentTick)
      return 0;
    unsigned elapsed = currentTick - tick;
    bool consecutive = engaged && elapsed >= 1 && elapsed <= 3;
    tick = currentTick;
    engaged = true;
    if (!consecutive)
      return 0;
    constexpr float radiansPerDegree = 0.017453292519943295f;
    // With stick zoom, up/down belongs to distance and the height stays put.
    float pitchStep = settings.cameraStickZoom ? 0.f :
        stick.y * settings.cameraPitchSpeed * radiansPerDegree * elapsed / 30;
    pitch += settings.invertY ? pitchStep : -pitchStep;
    pitch = std::clamp(pitch, -2.8f, 2.8f);
    float yaw = stick.x * settings.cameraYawSpeed * radiansPerDegree * elapsed / 30;
    return settings.invertX ? -yaw : yaw;
  }
};
} // namespace gurumin
