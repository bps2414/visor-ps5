#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::string Path(const std::string& dir) { return dir + "/visor.conf"; }

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

} // namespace

void Config::Load(const std::string& dir) {
  FILE* f = fopen(Path(dir).c_str(), "r");
  if (!f) return;
  char key[32], value[64];
  while (fscanf(f, " %31[^=\n]=%63[^\n]", key, value) == 2) {
    int n = atoi(value);
    if (!strcmp(key, "host")) stream.host = value;
    else if (!strcmp(key, "max_size")) stream.max_size = Clamp(n, 480, 1920);
    else if (!strcmp(key, "bitrate_mbps")) stream.bitrate_mbps = Clamp(n, 1, 50);
    else if (!strcmp(key, "fps")) stream.fps = Clamp(n, 15, 120);
    else if (!strcmp(key, "hevc")) stream.hevc = n;
    else if (!strcmp(key, "eye")) stream.eye = Clamp(n, 0, 2);
    else if (!strcmp(key, "zoom")) stream.zoom = Clamp(n, 0, 2);
    else if (!strcmp(key, "offset_x")) stream.offset_x = Clamp(n, -400, 400);
    else if (!strcmp(key, "offset_y")) stream.offset_y = Clamp(n, -400, 400);
    else if (!strcmp(key, "angle")) stream.angle = Clamp(n, -45, 45);
    else if (!strcmp(key, "english")) english = n;
  }
  fclose(f);
}

void Config::Save(const std::string& dir) const {
  FILE* f = fopen(Path(dir).c_str(), "w");
  if (!f) return;
  fprintf(f,
          "host=%s\nmax_size=%d\nbitrate_mbps=%d\nfps=%d\nhevc=%d\neye=%d\n"
          "zoom=%d\noffset_x=%d\noffset_y=%d\nangle=%d\nenglish=%d\n",
          stream.host.c_str(), stream.max_size, stream.bitrate_mbps,
          stream.fps, int(stream.hevc), stream.eye, stream.zoom,
          stream.offset_x, stream.offset_y, stream.angle, int(english));
  fclose(f);
}
