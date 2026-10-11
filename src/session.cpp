#include "session.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include "adb.h"

// O protocolo do scrcpy muda a cada versao: o jar em assets/ e este numero
// andam sempre juntos.
#define SCRCPY_VERSION "4.1"
#define REMOTE_JAR "/data/local/tmp/visor-server.jar"

namespace {

// Recorte 16:9 de um olho. Medido num Quest 2 (tela 3664x1920); outros
// modelos precisam de outros numeros, por isso offset_x/offset_y existem.
const int kEyeCenterX[2] = {892, 2750};
const int kEyeCenterY = 960;
const int kZoomWidth[3] = {1600, 1440, 1280};

uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

std::string ServerCommand(const StreamSettings& s, uint32_t scid) {
  char buf[512];
  int n = snprintf(buf, sizeof(buf),
                   "shell:CLASSPATH=" REMOTE_JAR " app_process / "
                   "com.genymobile.scrcpy.Server " SCRCPY_VERSION
                   " scid=%08x log_level=info tunnel_forward=true audio=false"
                   " control=false cleanup=false send_dummy_byte=false"
                   " send_device_meta=false video_codec=%s"
                   " video_bit_rate=%d max_fps=%d max_size=%d",
                   scid, s.hevc ? "h265" : "h264", s.bitrate_mbps * 1000000,
                   s.fps, s.max_size);
  if (s.eye < 2) {
    int w = kZoomWidth[s.zoom], h = w * 9 / 16;
    n += snprintf(buf + n, sizeof(buf) - n, " crop=%d:%d:%d:%d", w, h,
                  kEyeCenterX[s.eye] - w / 2 + s.offset_x,
                  kEyeCenterY - h / 2 + s.offset_y);
  }
  if (s.angle) snprintf(buf + n, sizeof(buf) - n, " angle=%d", s.angle);
  return buf;
}

} // namespace

Session::~Session() {
  Stop();
  if (texture_) SDL_DestroyTexture(texture_);
}

void Session::Start(const StreamSettings& settings, const std::string& dir) {
  Stop();
  stop_ = false;
  state_ = State::Connecting;
  thread_ = std::thread(&Session::Run, this, settings, dir);
}

void Session::Stop() {
  stop_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (adb_) adb_abort(adb_);
  }
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  av_frame_free(&latest_);
  fresh_ = false;
  stats_ = Stats();
  state_ = State::Idle;
}

std::string Session::error() {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

Session::Stats Session::stats() {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

void Session::Run(StreamSettings settings, std::string dir) {
  adb* a = adb_new();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    adb_ = a;
    error_.clear();
  }
  bool ok = Stream(settings, dir);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok) error_ = adb_error(a);
    if (!ok && error_.empty()) error_ = "o servidor de video nao iniciou no headset";
    adb_ = nullptr;
  }
  adb_free(a);
  state_ = ok || stop_ ? State::Idle : State::Failed;
}

bool Session::Stream(const StreamSettings& settings, const std::string& dir) {
  adb* a = adb_;
  auto on_wait_auth = [](void* self) {
    static_cast<Session*>(self)->state_ = State::WaitingAuth;
  };
  if (adb_connect(a, settings.host.c_str(), 5555, (dir + "/adbkey").c_str(),
                  on_wait_auth, this))
    return false;

  state_ = State::Starting;
  if (adb_push(a, (dir + "/assets/scrcpy-server").c_str(), REMOTE_JAR))
    return false;

  uint32_t scid = std::random_device{}() & 0x7fffffff;
  int shell = adb_open(a, ServerCommand(settings, scid).c_str());
  if (shell < 0) return false;

  // O servidor leva um instante para comecar a escutar.
  char name[64];
  snprintf(name, sizeof(name), "localabstract:scrcpy_%08x", scid);
  int video = -1;
  for (int i = 0; i < 50 && video < 0 && !stop_; i++) {
    SDL_Delay(100);
    video = adb_open(a, name);
  }
  if (video < 0) return false;

  uint8_t hdr[12];
  if (adb_read_full(a, video, hdr, 4)) return false;
  bool hevc = be32(hdr) == 0x68323635;
  const AVCodec* codec =
      avcodec_find_decoder(hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  dec->thread_type = FF_THREAD_SLICE; // threads por frame somariam atraso
  if (avcodec_open2(dec, codec, nullptr) < 0) {
    avcodec_free_context(&dec);
    return false;
  }

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  std::vector<uint8_t> config; // SPS/PPS, vai colado no proximo pacote
  size_t bytes = 0;
  int frames = 0;
  auto tick = std::chrono::steady_clock::now();
  bool ok = true;

  while (!stop_) {
    if (adb_read_full(a, video, hdr, 12)) { ok = false; break; }
    if (hdr[0] & 0x80) continue; // pacote de sessao: o tamanho vem no frame

    bool is_config = hdr[0] & 0x40;
    uint32_t size = be32(hdr + 8);
    size_t head = is_config ? 0 : config.size();
    if (av_new_packet(pkt, int(head + size)) < 0) { ok = false; break; }
    if (head) memcpy(pkt->data, config.data(), head);
    if (adb_read_full(a, video, pkt->data + head, size)) { ok = false; break; }
    bytes += size;

    if (is_config) {
      config.assign(pkt->data, pkt->data + size);
      av_packet_unref(pkt);
      continue;
    }
    config.clear();

    if (avcodec_send_packet(dec, pkt) == 0) {
      while (avcodec_receive_frame(dec, frame) == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latest_) latest_ = av_frame_alloc();
        av_frame_unref(latest_);
        av_frame_move_ref(latest_, frame);
        fresh_ = true;
        frames++;
        state_ = State::Streaming;
      }
    }
    av_packet_unref(pkt);

    auto now = std::chrono::steady_clock::now();
    float secs = std::chrono::duration<float>(now - tick).count();
    if (secs >= 1) {
      std::lock_guard<std::mutex> lock(mutex_);
      stats_.fps = frames / secs;
      stats_.mbps = bytes * 8 / secs / 1e6f;
      if (latest_) {
        stats_.width = latest_->width;
        stats_.height = latest_->height;
      }
      frames = 0;
      bytes = 0;
      tick = now;
    }
  }

  // O que o servidor disse ajuda a explicar uma queda.
  char log[1024];
  size_t n = adb_drain(a, shell, log, sizeof(log) - 1);
  log[n] = 0;
  if (n) fprintf(stderr, "[scrcpy] %s\n", log);

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec);
  return ok;
}

bool Session::Render(SDL_Renderer* renderer, int w, int h) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    AVFrame* f = latest_;
    // ponytail: so YUV 4:2:0 de 8 bits, que e o que o encoder do Quest manda.
    // Outro formato (HDR/10 bits) precisaria de swscale.
    if (fresh_ && f && (f->format == AV_PIX_FMT_YUV420P ||
                        f->format == AV_PIX_FMT_YUVJ420P)) {
      if (!texture_ || tex_w_ != f->width || tex_h_ != f->height) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                     SDL_TEXTUREACCESS_STREAMING, f->width,
                                     f->height);
        SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);
        tex_w_ = f->width;
        tex_h_ = f->height;
      }
      SDL_UpdateYUVTexture(texture_, nullptr, f->data[0], f->linesize[0],
                           f->data[1], f->linesize[1], f->data[2],
                           f->linesize[2]);
      fresh_ = false;
    }
  }
  if (!texture_ || state_ != State::Streaming) return false;

  SDL_Rect dst;
  if (tex_w_ * h > tex_h_ * w) {
    dst.w = w;
    dst.h = tex_h_ * w / tex_w_;
  } else {
    dst.h = h;
    dst.w = tex_w_ * h / tex_h_;
  }
  dst.x = (w - dst.w) / 2;
  dst.y = (h - dst.h) / 2;
  SDL_RenderCopy(renderer, texture_, nullptr, &dst);
  return true;
}

std::vector<std::string> ScanForHeadsets() {
  std::vector<std::string> found;

  // O IP local sai de um socket UDP "conectado": nada e enviado.
  sockaddr_in probe{}, self{};
  socklen_t len = sizeof(self);
  probe.sin_family = AF_INET;
  probe.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &probe.sin_addr);
  int udp = socket(AF_INET, SOCK_DGRAM, 0);
  bool have_ip = !connect(udp, (sockaddr*)&probe, sizeof(probe)) &&
                 !getsockname(udp, (sockaddr*)&self, &len);
  close(udp);
  if (!have_ip) return found;

  // ponytail: assume mascara /24, que e o caso de rede domestica.
  uint32_t base = ntohl(self.sin_addr.s_addr) & 0xffffff00;
  pollfd fds[254];
  for (int i = 0; i < 254; i++) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    sa.sin_addr.s_addr = htonl(base | uint32_t(i + 1));
    fds[i].fd = socket(AF_INET, SOCK_STREAM, 0);
    fds[i].events = POLLOUT;
    fds[i].revents = 0;
    fcntl(fds[i].fd, F_SETFL, fcntl(fds[i].fd, F_GETFL) | O_NONBLOCK);
    connect(fds[i].fd, (sockaddr*)&sa, sizeof(sa));
  }
  // Quem responde em 5555 termina o connect dentro da janela; os outros nao.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
  for (;;) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) break;
    poll(fds, 254, int(left));
    SDL_Delay(20);
  }
  for (int i = 0; i < 254; i++) {
    int err = 1;
    socklen_t errlen = sizeof(err);
    if ((fds[i].revents & POLLOUT) && !(fds[i].revents & (POLLERR | POLLHUP)) &&
        !getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &err, &errlen) && !err &&
        (base | uint32_t(i + 1)) != ntohl(self.sin_addr.s_addr)) {
      in_addr ia;
      ia.s_addr = htonl(base | uint32_t(i + 1));
      found.push_back(inet_ntoa(ia));
    }
    close(fds[i].fd);
  }
  return found;
}
