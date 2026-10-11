#include "app.h"

#include <cstdio>

#include <SDL.h>

namespace {

struct Preset { const char* pt; const char* en; int size, mbps, fps; };
const Preset kPresets[] = {
  {"Baixa", "Low", 960, 4, 30},
  {"Média", "Medium", 1280, 8, 60},
  {"Alta", "High", 1600, 16, 60},
  {"Máxima", "Max", 1600, 30, 72},
};
const int kSizes[] = {960, 1280, 1600, 1920};
const int kFps[] = {30, 60, 72, 90};
const int kSettingsRows = 11;
const int kNudge = 20; // pixels do headset por toque no direcional

// Proximo valor de uma lista fixa, parando nas pontas.
int Step(const int* list, int count, int current, int dir) {
  int i = 0;
  while (i < count - 1 && list[i] < current) i++;
  i += dir;
  return list[i < 0 ? 0 : i >= count ? count - 1 : i];
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int PresetOf(const StreamSettings& s) {
  for (int i = 0; i < 4; i++)
    if (kPresets[i].size == s.max_size && kPresets[i].mbps == s.bitrate_mbps &&
        kPresets[i].fps == s.fps)
      return i;
  return -1;
}

std::string Key(const char* label, const char* text) {
  return std::string("<span class=\"hint\"><span class=\"key\">") + label +
         "</span>" + text + "</span>";
}

std::string Row(bool selected, const std::string& title,
                const std::string& value, bool arrows = false) {
  std::string r = selected ? "<div class=\"row selected\">" : "<div class=\"row\">";
  r += "<span class=\"title\">" + title + "</span><span class=\"value\">";
  if (arrows && selected) r += "<span class=\"arrow\">&lt;</span>";
  r += value;
  if (arrows && selected) r += "<span class=\"arrow\">&gt;</span>";
  return r + "</span></div>";
}

} // namespace

App::~App() {
  // O documento sobrevive ao App e avisaria um ouvinte ja destruido.
  if (document_) document_->RemoveEventListener(Rml::EventId::Keydown, this, true);
  if (scan_thread_.joinable()) scan_thread_.join();
}

const char* App::T(const char* pt, const char* en) const {
  return config_.english ? en : pt;
}

bool App::Initialize(Rml::Context* context, const std::string& dir) {
  dir_ = dir;
  config_.Load(dir);
  if (!(document_ = context->LoadDocument(dir + "/assets/main.rml"))) return false;
  document_->AddEventListener(Rml::EventId::Keydown, this, true);
  document_->Show();
  Scan();
  return true;
}

void App::Scan() {
  if (scanning_.exchange(true)) return;
  if (scan_thread_.joinable()) scan_thread_.join();
  scan_thread_ = std::thread([this] {
    auto found = ScanForHeadsets();
    std::lock_guard<std::mutex> lock(scan_mutex_);
    scan_result_ = std::move(found);
    scan_done_ = true;
  });
  dirty_ = true;
}

void App::Connect(const std::string& host) {
  config_.stream.host = host;
  config_.Save(dir_);
  screen_ = Screen::Watch;
  show_stats_ = false;
  session_.Start(config_.stream, dir_);
  dirty_ = true;
}

bool App::Update() {
  double now = SDL_GetTicks() / 1000.0;

  if (scan_done_.exchange(false)) {
    std::lock_guard<std::mutex> lock(scan_mutex_);
    hosts_ = scan_result_;
    // O ultimo headset usado aparece mesmo se a busca nao o achou.
    const std::string& last = config_.stream.host;
    bool listed = false;
    for (auto& h : hosts_) listed |= h == last;
    if (!last.empty() && !listed) hosts_.push_back(last);
    scanning_ = false;
    fprintf(stderr, "[visor] busca achou %d headset(s)\n", int(hosts_.size()));
    if (screen_ == Screen::Home) sel_ = 0;
    dirty_ = true;
  }

  if (screen_ == Screen::Watch) {
    Session::State st = session_.state();
    if (st != seen_state_) {
      seen_state_ = st;
      // Vai para o log do launcher; e o unico jeito de depurar no console.
      fprintf(stderr, "[visor] estado %d %s\n", int(st), session_.error().c_str());
      dirty_ = true;
    }
    if (show_stats_ && now >= stats_at_) {
      stats_at_ = now + 1;
      dirty_ = true;
    }
    if (restart_at_ && now >= restart_at_) {
      restart_at_ = 0;
      config_.Save(dir_);
      session_.Start(config_.stream, dir_);
    }
  }

  if (dirty_) Rebuild();
  return !quit_;
}

void App::RenderVideo(SDL_Renderer* renderer, int w, int h) {
  if (screen_ == Screen::Watch) session_.Render(renderer, w, h);
}

// ---- RML ---------------------------------------------------------------

void App::Rebuild() {
  dirty_ = false;
  const char* cls = "home";
  std::string rml;
  switch (screen_) {
  case Screen::Home: rml = HomeRml(); break;
  case Screen::Ip: rml = IpRml(); cls = "ip"; break;
  case Screen::Settings: rml = SettingsRml(); cls = "settings"; break;
  case Screen::Watch: rml = WatchRml(); cls = "watch"; break;
  }
  document_->SetClassNames(cls);
  document_->GetElementById("root")->SetInnerRML(rml);
}

std::string App::HomeRml() {
  std::string r = "<div class=\"brand\"><img src=\"logo.png\"/><div>"
                  "<h1>Visor</h1><p class=\"tagline\">";
  r += T("Seu Quest na TV, direto pelo PS5", "Your Quest on the TV, straight from the PS5");
  r += "</p></div></div><div class=\"list\"><p class=\"eyebrow\">";
  if (scanning_) r += T("Procurando headsets na rede…", "Looking for headsets on the network…");
  else if (hosts_.empty()) r += T("Nenhum headset encontrado", "No headset found");
  else r += T("Headsets encontrados", "Headsets found");
  r += "</p>";

  int i = 0;
  for (auto& h : hosts_)
    r += Row(sel_ == i++, "Quest", h);
  r += Row(sel_ == i, T("Procurar de novo", "Search again"), "");
  r += Row(sel_ == i + 1, T("Digitar o IP", "Enter IP address"), "");
  r += Row(sel_ == i + 2, T("Ajustes", "Settings"), "");
  r += "</div>";

  if (hosts_.empty() && !scanning_) {
    r += "<p class=\"help\">";
    r += T("O headset precisa estar ligado, na mesma rede e com o ADB por "
           "Wi-Fi ativo (adb tcpip 5555, uma vez a cada reinício do Quest).",
           "The headset must be on, on the same network and with ADB over "
           "Wi-Fi enabled (adb tcpip 5555, once per Quest reboot).");
    r += "</p>";
  }
  r += "<div class=\"footer\">" + Key("X", T("Selecionar", "Select")) +
       Key("O", T("Sair", "Quit")) + "</div>";
  return r;
}

std::string App::IpRml() {
  std::string r = "<div class=\"card\"><h2>";
  r += T("IP do headset", "Headset IP address");
  r += "</h2><div class=\"octets\">";
  for (int i = 0; i < 4; i++) {
    r += sel_ == i ? "<span class=\"octet selected\">" : "<span class=\"octet\">";
    r += std::to_string(ip_[i]) + "</span>";
    if (i < 3) r += "<span class=\"dot\">.</span>";
  }
  r += "</div><p class=\"help\">";
  r += T("No Quest: Configurações, Wi-Fi, sua rede, Avançado.",
         "On the Quest: Settings, Wi-Fi, your network, Advanced.");
  r += "</p></div><div class=\"footer\">" +
       Key(T("Direcional", "D-pad"), T("Alterar", "Change")) +
       Key("L1 R1", "10") + Key("X", T("Conectar", "Connect")) +
       Key("O", T("Voltar", "Back")) + "</div>";
  return r;
}

std::string App::SettingsRml() {
  const StreamSettings& s = config_.stream;
  int preset = PresetOf(s);
  const char* eyes[] = {T("Esquerdo", "Left"), T("Direito", "Right"),
                        T("Os dois", "Both")};
  const char* zooms[] = {T("Aberto", "Wide"), T("Médio", "Medium"),
                         T("Fechado", "Tight")};
  std::string values[kSettingsRows] = {
    preset < 0 ? T("Personalizada", "Custom")
               : T(kPresets[preset].pt, kPresets[preset].en),
    std::to_string(s.max_size) + " px",
    std::to_string(s.bitrate_mbps) + " Mbps",
    std::to_string(s.fps) + " fps",
    s.hevc ? "H.265" : "H.264",
    eyes[s.eye],
    zooms[s.zoom],
    std::to_string(s.offset_x),
    std::to_string(s.offset_y),
    std::to_string(s.angle) + "°",
    config_.english ? "English" : "Português",
  };
  const char* titles[kSettingsRows] = {
    T("Qualidade", "Quality"), T("Resolução", "Resolution"), "Bitrate",
    T("Quadros por segundo", "Frame rate"), "Codec", T("Olho", "Eye"), "Zoom",
    T("Ajuste horizontal", "Horizontal offset"),
    T("Ajuste vertical", "Vertical offset"), T("Inclinação", "Tilt"),
    T("Idioma", "Language"),
  };

  std::string r = "<h2 class=\"page\">";
  r += T("Ajustes", "Settings");
  r += "</h2><div class=\"list tight\">";
  for (int i = 0; i < kSettingsRows; i++)
    r += Row(sel_ == i, titles[i], values[i], true);
  r += "</div><div class=\"footer\">" +
       Key(T("Direcional", "D-pad"), T("Alterar", "Change")) +
       Key("O", T("Voltar", "Back")) + "</div>";
  return r;
}

std::string App::WatchRml() {
  std::string r;
  Session::State st = session_.state();

  if (st == Session::State::Streaming) {
    if (!show_stats_) return r;
    Session::Stats stats = session_.stats();
    char buf[160];
    snprintf(buf, sizeof(buf), "%dx%d &nbsp; %.0f fps &nbsp; %.1f Mbps",
             stats.width, stats.height, stats.fps, stats.mbps);
    r = std::string("<div class=\"stats\">") + buf + "</div>";
    r += "<div class=\"footer\">";
    if (config_.stream.eye < 2)
      r += Key(T("Direcional", "D-pad"), T("Enquadrar", "Reframe")) +
           Key("L1 R1", "Zoom");
    r += Key("OPTIONS", T("Ocultar", "Hide")) + Key("O", T("Parar", "Stop")) +
         "</div>";
    return r;
  }

  r = "<div class=\"card\"><h2>";
  if (st == Session::State::Failed) {
    r += T("Não deu para conectar", "Could not connect");
    r += "</h2><p class=\"help\">" + session_.error() + "</p></div>";
    r += "<div class=\"footer\">" + Key("X", T("Tentar de novo", "Try again")) +
         Key("O", T("Voltar", "Back")) + "</div>";
    return r;
  }
  if (st == Session::State::WaitingAuth) {
    r += T("Coloque o headset", "Put the headset on");
    r += "</h2><p class=\"help\">";
    r += T("Aceite o aviso de depuração USB e marque Sempre permitir.",
           "Accept the USB debugging prompt and tick Always allow.");
  } else {
    r += st == Session::State::Starting
             ? T("Iniciando a transmissão…", "Starting the stream…")
             : T("Conectando…", "Connecting…");
    r += "</h2><p class=\"help\">" + config_.stream.host;
  }
  r += "</p></div><div class=\"footer\">" + Key("O", T("Cancelar", "Cancel")) +
       "</div>";
  return r;
}

// ---- Teclas ------------------------------------------------------------

void App::ProcessEvent(Rml::Event& event) {
  int key = event.GetParameter<int>("key_identifier", 0);
  switch (screen_) {
  case Screen::Home: KeyHome(key); break;
  case Screen::Ip: KeyIp(key); break;
  case Screen::Settings: KeySettings(key); break;
  case Screen::Watch: KeyWatch(key); break;
  }
  dirty_ = true;
  event.StopPropagation();
}

void App::KeyHome(int key) {
  int hosts = int(hosts_.size());
  switch (key) {
  case Rml::Input::KI_UP: sel_ = Clamp(sel_ - 1, 0, hosts + 2); break;
  case Rml::Input::KI_DOWN: sel_ = Clamp(sel_ + 1, 0, hosts + 2); break;
  case Rml::Input::KI_ESCAPE: quit_ = true; break;
  case Rml::Input::KI_RETURN:
    if (sel_ < hosts) {
      Connect(hosts_[sel_]);
    } else if (sel_ == hosts) {
      Scan();
    } else if (sel_ == hosts + 1) {
      // Comeca do ultimo IP usado, se houver.
      sscanf(config_.stream.host.c_str(), "%d.%d.%d.%d", &ip_[0], &ip_[1],
             &ip_[2], &ip_[3]);
      screen_ = Screen::Ip;
      sel_ = 3;
    } else {
      screen_ = Screen::Settings;
      sel_ = 0;
    }
    break;
  }
}

void App::KeyIp(int key) {
  int step = 0;
  switch (key) {
  case Rml::Input::KI_LEFT: sel_ = Clamp(sel_ - 1, 0, 3); break;
  case Rml::Input::KI_RIGHT: sel_ = Clamp(sel_ + 1, 0, 3); break;
  case Rml::Input::KI_UP: step = 1; break;
  case Rml::Input::KI_DOWN: step = -1; break;
  case Rml::Input::KI_NEXT: step = 10; break;
  case Rml::Input::KI_PRIOR: step = -10; break;
  case Rml::Input::KI_ESCAPE: screen_ = Screen::Home; sel_ = 0; return;
  case Rml::Input::KI_RETURN: {
    char host[32];
    snprintf(host, sizeof(host), "%d.%d.%d.%d", ip_[0], ip_[1], ip_[2], ip_[3]);
    Connect(host);
    return;
  }
  }
  ip_[sel_] = Clamp(ip_[sel_] + step, 0, 255);
}

void App::ChangeSetting(int row, int dir) {
  StreamSettings& s = config_.stream;
  switch (row) {
  case 0: {
    int p = Clamp(PresetOf(s) < 0 ? 2 : PresetOf(s) + dir, 0, 3);
    s.max_size = kPresets[p].size;
    s.bitrate_mbps = kPresets[p].mbps;
    s.fps = kPresets[p].fps;
    break;
  }
  case 1: s.max_size = Step(kSizes, 4, s.max_size, dir); break;
  case 2: s.bitrate_mbps = Clamp(s.bitrate_mbps + dir * 2, 2, 50); break;
  case 3: s.fps = Step(kFps, 4, s.fps, dir); break;
  case 4: s.hevc = !s.hevc; break;
  case 5: s.eye = Clamp(s.eye + dir, 0, 2); break;
  case 6: s.zoom = Clamp(s.zoom + dir, 0, 2); break;
  case 7: s.offset_x = Clamp(s.offset_x + dir * kNudge, -400, 400); break;
  case 8: s.offset_y = Clamp(s.offset_y + dir * kNudge, -400, 400); break;
  case 9: s.angle = Clamp(s.angle + dir, -45, 45); break;
  case 10: config_.english = !config_.english; break;
  }
}

void App::KeySettings(int key) {
  switch (key) {
  case Rml::Input::KI_UP: sel_ = Clamp(sel_ - 1, 0, kSettingsRows - 1); break;
  case Rml::Input::KI_DOWN: sel_ = Clamp(sel_ + 1, 0, kSettingsRows - 1); break;
  case Rml::Input::KI_LEFT: ChangeSetting(sel_, -1); break;
  case Rml::Input::KI_RIGHT:
  case Rml::Input::KI_RETURN: ChangeSetting(sel_, 1); break;
  case Rml::Input::KI_ESCAPE:
    config_.Save(dir_);
    screen_ = Screen::Home;
    sel_ = 0;
    break;
  }
}

void App::KeyWatch(int key) {
  Session::State st = session_.state();
  if (key == Rml::Input::KI_ESCAPE) {
    session_.Stop();
    restart_at_ = 0;
    config_.Save(dir_);
    screen_ = Screen::Home;
    sel_ = 0;
    return;
  }
  if (st == Session::State::Failed && key == Rml::Input::KI_RETURN) {
    session_.Start(config_.stream, dir_);
    return;
  }
  if (st != Session::State::Streaming) return;
  if (key == Rml::Input::KI_APPS || key == Rml::Input::KI_INSERT) {
    show_stats_ = !show_stats_;
    return;
  }

  // Enquadramento ao vivo: o recorte e feito no headset, entao cada mudanca
  // reinicia a transmissao. O atraso junta varios toques num reinicio so.
  StreamSettings& s = config_.stream;
  if (s.eye == 2) return;
  StreamSettings before = s;
  switch (key) {
  case Rml::Input::KI_LEFT: ChangeSetting(7, -1); break;
  case Rml::Input::KI_RIGHT: ChangeSetting(7, 1); break;
  case Rml::Input::KI_UP: ChangeSetting(8, -1); break;
  case Rml::Input::KI_DOWN: ChangeSetting(8, 1); break;
  case Rml::Input::KI_PRIOR: ChangeSetting(6, -1); break;
  case Rml::Input::KI_NEXT: ChangeSetting(6, 1); break;
  }
  if (s.offset_x != before.offset_x || s.offset_y != before.offset_y ||
      s.zoom != before.zoom)
    restart_at_ = SDL_GetTicks() / 1000.0 + 0.5;
}
