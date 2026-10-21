#pragma once

#include <cmath>

#include "cameracontrol.hpp"

class CScene;

// A request belongs to the walking camera, not its temporary collision distance.
// The retail parameter sets are never retained or modified between updates.
struct CameraZoom {
    static constexpr float min_distance = 30.0f;
    static constexpr float max_distance = 600.0f;
    static constexpr float wheel_step = 8.0f;
    // The eye keeps this much room from geometry, beyond mouse turning's own bound.
    static constexpr float clearance = 6.0f;
    // A cleared view returns this fraction of the remaining distance per tick,
    // at least release_min, once the view lookahead units farther stays clear.
    static constexpr float release_rate = 0.04f;
    static constexpr float release_min = 1.0f;
    static constexpr float lookahead = 32.0f;
    // The lookahead also checks the view turned as far as this many ticks of its
    // current turning would take it.
    static constexpr float lookahead_ticks = 60.0f;
    static constexpr int   clear_ticks = 20;
    // Ticks held after a pull-in, doubled for each recent one up to max_strikes.
    static constexpr int hold_ticks = 45;
    static constexpr int max_strikes = 2;
    // A quiet period without pull-ins forgets one of them.
    static constexpr int calm_ticks = 300;

    float requested = 0;
    // Distance the eye may use: drops at once when the view is blocked and
    // returns only after the hold and a clear lookahead.
    float allowed = 0;
    int   hold = 0;
    int   clear = 0;
    int   strikes = 0;
    int   calm = 0;
    // The pose drawn before this tick's step, which display frames blend from.
    float shown_pos[3] = {NAN, NAN, NAN};
    float shown_ref[3] = {NAN, NAN, NAN};
    float heading = NAN;
    float turn_rate = 0;
    bool  active = false;

    float HeightScale(CCameraControl &camera) const;
    void  Read(CCameraControl &camera, float wheel, bool reset, bool enabled);
    void  Move(CCameraControl &camera, CScene &scene, CCameraControl::Control &control, float *rotation,
               CCPoly *retail_polys = nullptr, int retail_count = 0);
    // Places the eye in front of any wall between it and the player in the view
    // drawn after the next step. Also run after direct mouse motion.
    void KeepVisible(CCameraControl &camera, CScene &scene, CCPoly *retail_polys = nullptr, int retail_count = 0);

private:
    // The distance below which walls limit the eye as in retail, without a hold.
    [[nodiscard]] float Floor(const CameraCtrlParam &retail) const;
    void                Strike();
    // Whether a camera wall lies between the drawn views blending to the rendered one.
    bool Swept(CCameraControl &camera, CScene &scene, CCameraControl &rendered, CCPoly *retail_polys,
               int retail_count) const;
};
