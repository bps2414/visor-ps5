/* visor.elf: o payload que o usuario manda para o PS5. Fica residente e
 *   1. extrai o app e os arquivos dele (embutidos aqui) em /data/visor;
 *   2. cria o atalho "Visor" na tela inicial;
 *   3. abre o app quando o atalho e clicado.
 *
 * O atalho e um "app web": o sistema abre o navegador na deeplinkUri, que
 * aponta para o servidor HTTP minimo daqui. Ao receber o pedido, fechamos o
 * navegador e lancamos o app com o loader do websrv (third_party/websrv). */

#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <ps5/kernel.h>

#include "hbldr.h"

#define DIR "/data/visor"
#define PORT 8744
#define TILE_ID "VISR00001" /* o atalho; o app em si roda como VISR00002 */
#define TILE_DIR "/user/app/" TILE_ID

int sceUserServiceInitialize(void*);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void*);
int sceLncUtilGetAppId(const char* title_id);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceKernelGetAppState(int app_id, int*, int*);
int sceKernelSendNotificationRequest(int, void*, size_t, int);

/* Os caminhos sao relativos a raiz do repo e a pasta de build (-I no CMake). */
#define EMBED(sym, file)                                         \
  __asm__(".section .rodata\n" #sym ":\n.incbin \"" file "\"\n"  \
          #sym "_end:\n.previous\n");                            \
  extern const uint8_t sym[], sym##_end[]

EMBED(e_app, "visor-app.elf");
EMBED(e_rml, "assets/main.rml");
EMBED(e_rcss, "assets/visor.rcss");
EMBED(e_logo, "assets/logo.png");
EMBED(e_scrcpy, "assets/scrcpy-server");
EMBED(e_font, "fonts/LatoLatin-Regular.ttf");
EMBED(e_fontb, "fonts/LatoLatin-Bold.ttf");
EMBED(e_icon, "sce_sys/icon0.png");

static int write_file(const char* path, const uint8_t* from, const uint8_t* to) {
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0755);
  int ok = fd >= 0 && write(fd, from, to - from) == to - from;
  if (fd >= 0) close(fd);
  if (!ok) printf("[visor] nao consegui gravar %s\n", path);
  return ok ? 0 : -1;
}

/* Usada pelo loader do websrv. */
uint8_t* fs_readfile(const char* path, size_t* size) {
  struct stat st;
  uint8_t* buf = 0;
  int fd = open(path, O_RDONLY);
  if (fd >= 0 && !fstat(fd, &st) && (buf = malloc(st.st_size)) &&
      read(fd, buf, st.st_size) != st.st_size) {
    free(buf);
    buf = 0;
  }
  if (fd >= 0) close(fd);
  if (buf && size) *size = st.st_size;
  return buf;
}

static void notify(const char* msg) {
  struct {
    char unused[45];
    char message[3075];
  } req = {0};
  strncpy(req.message, msg, sizeof(req.message) - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static int extract(void) {
  mkdir(DIR, 0755);
  mkdir(DIR "/assets", 0755);
  mkdir(DIR "/fonts", 0755);
  return write_file(DIR "/visor-app.elf", e_app, e_app_end) |
         write_file(DIR "/assets/main.rml", e_rml, e_rml_end) |
         write_file(DIR "/assets/visor.rcss", e_rcss, e_rcss_end) |
         write_file(DIR "/assets/logo.png", e_logo, e_logo_end) |
         write_file(DIR "/assets/scrcpy-server", e_scrcpy, e_scrcpy_end) |
         write_file(DIR "/fonts/LatoLatin-Regular.ttf", e_font, e_font_end) |
         write_file(DIR "/fonts/LatoLatin-Bold.ttf", e_fontb, e_fontb_end);
}

/* Mesma tecnica do ftpsrv e do ps5-payload-manager: escrever
 * /user/app/<TITLE>/sce_sys e registrar o titulo. */
static void install_tile(void) {
  static const char want[] =
      "{\n"
      "  \"titleId\": \"" TILE_ID "\",\n"
      "  \"applicationCategoryType\": 65536,\n"
      "  \"deeplinkUri\": \"http://127.0.0.1:8744/\",\n"
      "  \"localizedParameters\": {\n"
      "    \"defaultLanguage\": \"en-US\",\n"
      "    \"en-US\": { \"titleName\": \"Visor\" }\n"
      "  }\n"
      "}\n";
  int (*install_title_dir)(const char*, const char*, void*) = 0;
  char have[sizeof(want) + 1] = "";
  uint32_t handle;
  int err, fd;

  /* Ja instalado e em dia: nada a fazer. */
  if ((fd = open(TILE_DIR "/sce_sys/param.json", O_RDONLY)) >= 0) {
    err = read(fd, have, sizeof(have) - 1);
    close(fd);
    if (err == sizeof(want) - 1 && !memcmp(have, want, err)) return;
  }

  mkdir(TILE_DIR, 0755);
  mkdir(TILE_DIR "/sce_sys", 0755);
  if (write_file(TILE_DIR "/sce_sys/icon0.png", e_icon, e_icon_end) ||
      write_file(TILE_DIR "/sce_sys/param.json", (const uint8_t*)want,
                 (const uint8_t*)want + sizeof(want) - 1))
    return;

  if ((err = sceAppInstUtilInitialize())) {
    printf("[visor] sceAppInstUtilInitialize: 0x%08x\n", err);
    return;
  }
  /* Firmwares novos tem uma chamada que registra so um titulo. */
  if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
    install_title_dir = (void*)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
  err = install_title_dir ? install_title_dir(TILE_ID, "/user/app/", 0)
                          : sceAppInstUtilAppInstallAll(0);
  printf("[visor] atalho instalado: 0x%08x\n", err);
  sceAppInstUtilTerminate();
}

/* Fecha o navegador que o atalho abriu; sem isso ele fica na frente do app. */
static void close_browser(void) {
  int id = sceLncUtilGetAppId(TILE_ID);
  if (id <= 0) return;
  printf("[visor] fechando o navegador (app 0x%x): 0x%08x\n", id,
         sceSystemServiceKillApp(id, -1, 0, 0));
  for (int i = 0; i < 50 && !sceKernelGetAppState(id, 0, 0); i++) usleep(100000);
}

static void launch(void) {
  char* argv[] = {DIR "/visor-app.elf", 0};
  close_browser();
  printf("[visor] app iniciado, pid %d\n",
         hbldr_launch(DIR, argv[0], STDOUT_FILENO, argv, 0));
}

static int local_socket(struct sockaddr_in* sa) {
  int on = 1, fd = socket(AF_INET, SOCK_STREAM, 0);
  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;
  sa->sin_port = htons(PORT);
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  return fd;
}

int main(void) {
  static const char page[] =
      "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
      "<body style='background:#0b1026'><script>window.close()</script>";
  struct sockaddr_in sa;
  char req[512];
  int srv, fd, n;

  signal(SIGPIPE, SIG_IGN);

  /* Uma copia antiga ainda rodando? Manda sair para liberar a porta. */
  fd = local_socket(&sa);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (!connect(fd, (struct sockaddr*)&sa, sizeof(sa))) {
    send(fd, "GET /quit \r\n\r\n", 14, 0);
    recv(fd, req, sizeof(req), 0); /* espera ela fechar */
  }
  close(fd);

  mkdir(DIR, 0755);
  if ((fd = open(DIR "/visor.log", O_CREAT | O_WRONLY | O_TRUNC, 0644)) >= 0) {
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
  }
  setvbuf(stdout, 0, _IONBF, 0);

  sceUserServiceInitialize(0);
  /* Credencial de sistema, a mesma que o websrv usa para lancar apps. */
  kernel_set_ucred_authid(-1, 0x4801000000000013L);

  if (extract()) {
    notify("Visor: could not write to " DIR);
    return 1;
  }
  install_tile();

  srv = local_socket(&sa);
  for (n = 0; bind(srv, (struct sockaddr*)&sa, sizeof(sa)); n++) {
    if (n == 20) {
      notify("Visor: port 8744 is busy");
      return 1;
    }
    usleep(250000);
  }
  listen(srv, 4);
  notify("Visor is ready.\nOpen it from the home screen.");

  while ((fd = accept(srv, 0, 0)) >= 0) {
    n = recv(fd, req, sizeof(req) - 1, 0);
    req[n > 0 ? n : 0] = 0;
    if (!strncmp(req, "GET /quit ", 10)) return 0;
    send(fd, page, sizeof(page) - 1, 0);
    close(fd);
    if (!strncmp(req, "GET / ", 6)) launch(); /* ignora favicon e afins */
  }
  return 0;
}
