#pragma once

#include <string>
#include <vector>

#if defined(BOARD_ESP32_S3_EPAPER_397)

void listMusicTracks(std::vector<std::string>& out, const char* musicDir = "/music");

#endif
