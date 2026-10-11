// Teste de ponta a ponta do nucleo, sem UI: conecta no headset, espera a
// transmissao e salva um frame.   coretest <ip> <saida.bmp> [olho] [segundos]
#include <cstdio>
#include <cstdlib>

#include <SDL.h>

#include "../src/session.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    for (auto& ip : ScanForHeadsets()) printf("achei %s\n", ip.c_str());
    return 2;
  }
  StreamSettings settings;
  settings.host = argv[1];
  if (argc > 3) settings.eye = atoi(argv[3]);
  int seconds = argc > 4 ? atoi(argv[4]) : 3;

  Session session;
  session.Start(settings, ".");

  Uint32 start = SDL_GetTicks(), streaming_at = 0;
  Session::State last = Session::State::Idle;
  for (;;) {
    Session::State st = session.state();
    if (st != last) printf("estado %d (%u ms)\n", int(st), SDL_GetTicks() - start);
    last = st;
    if (st == Session::State::Failed) {
      printf("FALHOU: %s\n", session.error().c_str());
      return 1;
    }
    if (st == Session::State::Streaming && !streaming_at) streaming_at = SDL_GetTicks();
    if (streaming_at && SDL_GetTicks() - streaming_at > Uint32(seconds) * 1000) break;
    if (SDL_GetTicks() - start > 60000) { printf("FALHOU: tempo esgotado\n"); return 1; }
    SDL_Delay(50);
  }

  Session::Stats stats = session.stats();
  printf("%dx%d %.1f fps %.1f Mbps\n", stats.width, stats.height, stats.fps, stats.mbps);

  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
  bool drawn = session.Render(renderer, 1920, 1080);
  SDL_RenderPresent(renderer);
  SDL_SaveBMP(surface, argv[2]);
  session.Stop();
  printf(drawn && stats.fps > 10 ? "OK\n" : "FALHOU: sem imagem\n");
  return drawn && stats.fps > 10 ? 0 : 1;
}
