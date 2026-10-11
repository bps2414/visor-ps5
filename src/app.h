// Telas e navegacao. A UI inteira e regerada como RML a cada mudanca de
// estado: sao poucas linhas e isso dispensa um data model.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <RmlUi/Core.h>

#include "config.h"
#include "session.h"

class App : public Rml::EventListener {
public:
  ~App() override;

  bool Initialize(Rml::Context* context, const std::string& dir);
  // Uma vez por frame, antes de context->Update(). Retorna false para sair.
  bool Update();
  // Desenha o video por baixo da UI.
  void RenderVideo(SDL_Renderer* renderer, int w, int h);

  void ProcessEvent(Rml::Event& event) override;

private:
  enum class Screen { Home, Ip, Settings, Watch };

  const char* T(const char* pt, const char* en) const;
  void Rebuild();
  std::string HomeRml();
  std::string IpRml();
  std::string SettingsRml();
  std::string WatchRml();

  void Scan();
  void Connect(const std::string& host);
  void KeyHome(int key);
  void KeyIp(int key);
  void KeySettings(int key);
  void KeyWatch(int key);
  void ChangeSetting(int row, int dir);

  std::string dir_;
  Config config_;
  Session session_;
  Rml::ElementDocument* document_ = nullptr;

  Screen screen_ = Screen::Home;
  bool dirty_ = true;
  bool quit_ = false;
  int sel_ = 0;          // linha selecionada na tela atual
  int ip_[4] = {192, 168, 1, 100};
  bool show_stats_ = false;
  Session::State seen_state_ = Session::State::Idle;
  double stats_at_ = 0;    // proxima atualizacao do painel de numeros
  double restart_at_ = 0;  // reinicio pendente apos ajuste de enquadramento

  std::thread scan_thread_;
  std::atomic<bool> scanning_{false};
  std::atomic<bool> scan_done_{false};
  std::mutex scan_mutex_;
  std::vector<std::string> scan_result_;
  std::vector<std::string> hosts_;
};
