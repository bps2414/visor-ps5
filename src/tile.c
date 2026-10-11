/* Atalho do Visor na tela inicial do PS5. Roda como daemon, com a pasta do
 * app como diretorio de trabalho.
 *
 * O tile e um "app web": o sistema abre a deeplinkUri, que aqui e o endpoint
 * do websrv que lanca o Visor. Mesma tecnica do ftpsrv (John Tornblom) e do
 * ps5-payload-manager: escrever /user/app/<TITLE>/sce_sys e registrar. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define TITLE_ID "VISR00001"
#define APP_DIR "/user/app/" TITLE_ID

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void*);

static int copy_file(const char* from, const char* to) {
  char buf[4096];
  size_t n;
  FILE* in = fopen(from, "rb");
  FILE* out = in ? fopen(to, "wb") : 0;
  if (!out) {
    if (in) fclose(in);
    return -1;
  }
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
  fclose(in);
  fclose(out);
  return 0;
}

static void install_tile(const char* dir) {
  int (*install_title_dir)(const char*, const char*, void*) = 0;
  char want[1024], have[1024] = "", icon[512];
  uint32_t handle;
  size_t n;
  FILE* f;
  int err;

  snprintf(want, sizeof(want),
           "{\n"
           "  \"titleId\": \"" TITLE_ID "\",\n"
           "  \"applicationCategoryType\": 65536,\n"
           "  \"deeplinkUri\": \"http://127.0.0.1:8080/hbldr?path=%s/visor.elf&cwd=%s\",\n"
           "  \"localizedParameters\": {\n"
           "    \"defaultLanguage\": \"en-US\",\n"
           "    \"en-US\": { \"titleName\": \"Visor\" }\n"
           "  }\n"
           "}\n",
           dir, dir);

  /* Ja instalado e apontando para esta pasta: nada a fazer. */
  if ((f = fopen(APP_DIR "/sce_sys/param.json", "r"))) {
    n = fread(have, 1, sizeof(have) - 1, f);
    have[n] = 0;
    fclose(f);
    if (!strcmp(have, want)) return;
  }

  mkdir(APP_DIR, 0755);
  mkdir(APP_DIR "/sce_sys", 0755);
  snprintf(icon, sizeof(icon), "%s/sce_sys/icon0.png", dir);
  if (copy_file(icon, APP_DIR "/sce_sys/icon0.png") ||
      !(f = fopen(APP_DIR "/sce_sys/param.json", "w"))) {
    fprintf(stderr, "[tile] sem permissao para escrever em " APP_DIR "\n");
    return;
  }
  fputs(want, f);
  fclose(f);

  if ((err = sceAppInstUtilInitialize())) {
    fprintf(stderr, "[tile] sceAppInstUtilInitialize: 0x%08x\n", err);
    return;
  }
  /* Firmwares novos tem uma chamada que registra so um titulo. */
  if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
    install_title_dir =
        (void*)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
  err = install_title_dir ? install_title_dir(TITLE_ID, "/user/app/", 0)
                          : sceAppInstUtilAppInstallAll(0);
  fprintf(stderr, "[tile] instalacao: 0x%08x\n", err);
  sceAppInstUtilTerminate();
}

/* A pasta do app vem como ultimo argumento (daemons nao tem cwd confiavel). */
int main(int argc, char** argv) {
  const char* dir = argc > 0 && argv[argc - 1][0] == '/' &&
                    !strstr(argv[argc - 1], ".elf")
                        ? argv[argc - 1]
                        : "/data/homebrew/Visor";
  fprintf(stderr, "[tile] pasta do app: %s\n", dir);
  install_tile(dir);
  return 0;
}
