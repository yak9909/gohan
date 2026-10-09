#pragma once

#include <cstdint>
#include <cmath>
#include "HhdRoomCameraCurve.hpp"

// HHD's normal room controller; photo controls are a separate native path.
// IDA-gpt-6.1-sol-F014: original ARM 0x156C0C, evidence hhd_native_frames.json.
namespace HhdRoomCameraControl {

struct State {
    float yawVelocity, zoomVelocity, zoom;
    std::uint16_t yaw;
};
struct Profile {
    float x, y, z, distance;
    std::uint16_t pitch, yaw;
};
struct PanState { float x, z; };

inline void Pan(PanState &pan, float right, float up, float sine, float cosine) {
    const float length2 = right * right + up * up;
    if (length2 < 0.0225f)                       // 15% radial dead zone.
        return;
    if (length2 > 1.0f) {
        const float inverse = 1.0f / std::sqrt(length2);
        right *= inverse;
        up *= inverse;
    }
    // Eye is on (+sin(yaw), +cos(yaw)); forward points towards the target.
    pan.x += 2.0f * (right * cosine - up * sine);
    pan.z += 2.0f * (-right * sine - up * cosine);
}
static_assert(sizeof(Profile) == 20, "Native camera profile layout");

inline float Approach(float value, float target, float step) {
    if (value < target) {
        value += step;
        return value > target ? target : value;
    }
    if (value > target) {
        value -= step;
        return value < target ? target : value;
    }
    return value;
}

inline void Step(State &state, std::uint32_t held) {
    const float yawTarget = (held & 0x40000u) ? -1.0f : (held & 0x80000u) ? 1.0f : 0.0f;
    state.yawVelocity = Approach(state.yawVelocity, yawTarget, yawTarget != 0.0f ? 0.2f : 0.1f);
    state.yaw = static_cast<std::uint16_t>(state.yaw + static_cast<int>((state.yawVelocity * 3.0f) * kAngleUnits));

    int segment = static_cast<int>(state.zoom);
    if (segment > 2)
        segment = 2;
    float zoomTarget = 0.0f, acceleration = 0.0714285746f;
    if (held & 0x20000u) {
        if ((state.zoomVelocity * 13.0f) * 0.04f < 3.0f - state.zoom) {
            zoomTarget = 1.0f;
            acceleration = 0.2f;
        }
    } else if (held & 0x10000u) {
        if ((state.zoomVelocity * 13.0f) * -0.035f < state.zoom) {
            zoomTarget = -1.0f;
            acceleration = 0.2f;
        }
    }
    state.zoomVelocity = Approach(state.zoomVelocity, zoomTarget, acceleration);
    state.zoom += state.zoomVelocity * kSegmentSteps[segment];
    if (state.zoom < 0.0f)
        state.zoom = 0.0f;
    else if (state.zoom > 3.0f)
        state.zoom = 3.0f;
}

inline Profile Sample(const State &state) {
    const int segment = static_cast<int>(state.zoom);
    const float fraction = state.zoom - static_cast<float>(segment);
    CurvePoint value = kCurve[0][segment];
    if (fraction != 0.0f) {
        value = kCurve[3][segment];
        for (int coefficient = 2; coefficient >= 0; --coefficient) {
            value.y = value.y * fraction + kCurve[coefficient][segment].y;
            value.distance = value.distance * fraction + kCurve[coefficient][segment].distance;
            value.pitch = static_cast<int>(static_cast<float>(value.pitch) * fraction)
                        + kCurve[coefficient][segment].pitch;
        }
    }
    return {0.0f, value.y, 0.0f, value.distance, static_cast<std::uint16_t>(value.pitch), state.yaw};
}

}  // namespace HhdRoomCameraControl
