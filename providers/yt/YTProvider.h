#pragma once

#include <include/MusicPlayer.h>
#include "YTVManager.h"

extern yt::YTVManager* manager;
extern "C" {
    std::shared_ptr<music::manager::PlayerProvider> EXPORT create_provider();
}