#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include <unistd.h>

#include <SDL.h>
#include <RmlUi/Core.h>

#include "RmlUi/Backend.h"
#include "app.h"

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

#ifdef __SCE__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Pede ao websrv para rodar o visor-tile.elf (o instalador do atalho da tela
// inicial) como daemon. Ele nao faz nada se o atalho ja estiver em dia.
static void RequestTileInstall(const std::string& dir) {
  std::string req = "GET /hbldr?daemon=1&path=" + dir + "/visor-tile.elf&args=" +
                    dir + " HTTP/1.0\r\n\r\n";
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(8080);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (!connect(fd, (sockaddr*)&sa, sizeof(sa)))
    send(fd, req.data(), req.size(), 0);
  close(fd);
}
#endif

#ifndef __SCE__
// Teste sem tela (PC): VISOR_KEYS="down ok ..." injeta teclas, uma a cada
// meio segundo, e VISOR_SHOT=arquivo.bmp salva o ultimo frame e sai.
static int ScriptedKey(const std::string& name) {
  if (name == "up") return Rml::Input::KI_UP;
  if (name == "down") return Rml::Input::KI_DOWN;
  if (name == "left") return Rml::Input::KI_LEFT;
  if (name == "right") return Rml::Input::KI_RIGHT;
  if (name == "ok") return Rml::Input::KI_RETURN;
  if (name == "back") return Rml::Input::KI_ESCAPE;
  if (name == "options") return Rml::Input::KI_APPS;
  if (name == "l1") return Rml::Input::KI_PRIOR;
  if (name == "r1") return Rml::Input::KI_NEXT;
  return Rml::Input::KI_UNKNOWN; // qualquer outra palavra so espera
}

static void SaveShot(const char* path) {
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT,
                                                  32, SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(Backend::GetSDLRenderer(), nullptr,
                       SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  SDL_SaveBMP(s, path);
  SDL_FreeSurface(s);
}
#endif

int SDL_main(int argc, char* args[]) {
  const std::string cwd = std::filesystem::current_path();
  Rml::Context* ctx;
  auto app = std::make_unique<App>();

  if (!Backend::Initialize("Visor", SCREEN_WIDTH, SCREEN_HEIGHT, false)) {
    return -1;
  }

  Rml::SetSystemInterface(Backend::GetSystemInterface());
  Rml::SetRenderInterface(Backend::GetRenderInterface());
  Rml::Initialise();

  if (!(ctx = Rml::CreateContext("main", Rml::Vector2i(SCREEN_WIDTH, SCREEN_HEIGHT)))) {
    Rml::Shutdown();
    Backend::Shutdown();
    return -1;
  }

  Rml::LoadFontFace(cwd + "/fonts/LatoLatin-Regular.ttf", true);
  Rml::LoadFontFace(cwd + "/fonts/LatoLatin-Bold.ttf", false);

  if (!app->Initialize(ctx, cwd)) {
    Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load the UI from %s", cwd.c_str());
    Rml::Shutdown();
    Backend::Shutdown();
    return -1;
  }

#ifdef __SCE__
  RequestTileInstall(cwd);
#endif
#ifndef __SCE__
  std::istringstream keys(getenv("VISOR_KEYS") ? getenv("VISOR_KEYS") : "");
  const char* shot = getenv("VISOR_SHOT");
  Uint32 next_key = SDL_GetTicks() + 1500;
#endif

  while (Backend::ProcessEvents(ctx) && app->Update()) {
    ctx->Update();

    Backend::BeginFrame();
    app->RenderVideo(Backend::GetSDLRenderer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    ctx->Render();

#ifndef __SCE__
    if (shot && SDL_GetTicks() >= next_key) {
      std::string name;
      next_key = SDL_GetTicks() + 500;
      if (keys >> name) {
        ctx->ProcessKeyDown(Rml::Input::KeyIdentifier(ScriptedKey(name)), 0);
      } else {
        SaveShot(shot);
        break;
      }
    }
#endif
    Backend::PresentFrame();
  }

  app.reset(); // antes do SDL: a sessao ainda segura uma textura
  Rml::Shutdown();
  Backend::Shutdown();

  return 0;
}


#ifndef __SCE__
int main(int argc, char** argv) {
  return SDL_main(argc, argv);
}
#endif
