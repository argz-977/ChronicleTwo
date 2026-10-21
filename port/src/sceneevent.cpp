#include "common.h"

#include "map.hpp"
#include "mapload.hpp"
#include "mapsky.hpp"
#include "mg_camera.hpp"
#include "mg_math.hpp"
#include "scenesnd.hpp"

// Named local, not an anonymous union: an anonymous union cannot hold a member with a user-provided constructor.
PORT_OVERRIDE void CScene::DrawSky(int sky_index) {
    float            camera_info[4];
    float            sun_position[4];
    float            moon_position[4];
    float            lighting_ratio[8];
    float            sun_ratio[8];
    CMapLightingInfo lighting;
    float            sky_color_a[4];
    float            sky_color_b[4];
    CMapSky         *sky = nullptr;
    mgCCamera       *camera = nullptr;
    CMap            *map = nullptr;

    if (sky_index < 0) {
        sky = GetSky(1);

        if (sky == nullptr) {
            sky = GetSky(0);
        }
    } else {
        sky = GetSky(sky_index);
    }

    if (sky != nullptr) {
        mgZeroVector(camera_info);
        camera = GetCamera(active_camera);

        if (camera != nullptr) {
            camera->GetPos(camera_info);
            camera_info[3] = camera->GetAngleH();
        }

        map = GetMap(active_map);

        if (map != nullptr && map->map_info.sky_info != 0) {
            camera_info[1] = map->map_info.sky_height;
            map->GetLightInfo(&lighting);
            map->GetLightingRatio(lighting_ratio);
            map->GetLightingSunRatio(sun_ratio);
            GetSunPosition(sun_position);
            GetMoonPosition(moon_position);
            sceVu0CopyVector(sky_color_a, lighting.bg_color);
            sceVu0ScaleVector(sky_color_a, sky_color_a, 0.0078125f);
            sky_color_a[3] = 1.0f;
            sceVu0CopyVector(sky_color_b, lighting.bg_color2);
            sceVu0ScaleVector(sky_color_b, sky_color_b, 0.0078125f);
            sky_color_b[3] = 1.0f;
            sky->DrawSkyBack(camera_info, sky_color_a, sky_color_b);
            sky->DrawSky(camera_info, sun_position, moon_position,
                         map->GetNowTimeBand(), lighting_ratio, sun_ratio);
        }
    }
}
