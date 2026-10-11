// Uma transmissao: conecta no Quest por ADB, sobe o scrcpy-server, decodifica
// o video numa thread propria e entrega o frame mais novo para o SDL.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct AVFrame;
struct adb;

struct StreamSettings {
  std::string host;
  int max_size = 1600;   // maior lado do video, em pixels
  int bitrate_mbps = 16;
  int fps = 60;
  bool hevc = false;
  int eye = 0;           // 0 esquerdo, 1 direito, 2 os dois
  int zoom = 0;          // 0 aberto, 1 medio, 2 fechado
  int offset_x = 0;      // ajuste fino do recorte, em pixels do headset
  int offset_y = 0;
  int angle = 0;         // graus, para endireitar a imagem
};

class Session {
public:
  enum class State { Idle, Connecting, WaitingAuth, Starting, Streaming, Failed };

  struct Stats {
    int width = 0, height = 0;
    float fps = 0, mbps = 0;
  };

  ~Session();

  // 'dir' e a pasta do app: tem o scrcpy-server e guarda a chave ADB.
  void Start(const StreamSettings& settings, const std::string& dir);
  void Stop();

  State state() const { return state_; }
  std::string error();
  Stats stats();

  // Thread principal: desenha o frame mais novo centralizado em w x h.
  // Retorna false enquanto nao ha imagem.
  bool Render(SDL_Renderer* renderer, int w, int h);

private:
  void Run(StreamSettings settings, std::string dir);
  bool Stream(const StreamSettings& settings, const std::string& dir);

  std::thread thread_;
  std::atomic<State> state_{State::Idle};
  std::atomic<bool> stop_{false};

  std::mutex mutex_;       // protege tudo abaixo
  adb* adb_ = nullptr;
  std::string error_;
  Stats stats_;
  AVFrame* latest_ = nullptr;
  bool fresh_ = false;

  SDL_Texture* texture_ = nullptr; // so a thread principal
  int tex_w_ = 0, tex_h_ = 0;
};

// IPs da rede local com a porta do ADB (5555) aberta. Bloqueia ~1 s.
std::vector<std::string> ScanForHeadsets();
