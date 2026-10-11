#include "adb.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#define A_CNXN 0x4e584e43
#define A_AUTH 0x48545541
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257

#define A_VERSION 0x01000000
#define MAX_PAYLOAD (256 * 1024)

#define AUTH_TOKEN 1
#define AUTH_SIGNATURE 2
#define AUTH_RSAPUBLICKEY 3

#define MAX_STREAMS 4
#define KEY_BYTES 256

typedef struct {
  uint32_t cmd, arg0, arg1, len, crc, magic;
} msg_t;

typedef struct {
  uint32_t local;  /* id unico por abertura: respostas atrasadas nao casam */
  uint32_t remote; /* 0 = livre */
  int opening;     /* esperando o OKAY do OPEN */
  int acked;       /* OKAY do ultimo WRTE chegou */
  int closed;
  uint8_t* buf;
  size_t len, off, cap;
} stream_t;

struct adb {
  int fd;
  uint32_t max_payload;
  uint32_t next_local;
  stream_t streams[MAX_STREAMS];
  uint8_t* payload;
  char err[256];
};

static void set_err(adb_t* a, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(a->err, sizeof(a->err), fmt, ap);
  va_end(ap);
}

const char* adb_error(const adb_t* a) { return a->err; }

adb_t* adb_new(void) {
  adb_t* a = calloc(1, sizeof(*a));
  a->fd = -1;
  a->max_payload = 4096;
  a->payload = malloc(MAX_PAYLOAD);
  return a;
}

void adb_abort(adb_t* a) {
  if (a->fd >= 0) shutdown(a->fd, SHUT_RDWR);
}

void adb_free(adb_t* a) {
  if (!a) return;
  if (a->fd >= 0) close(a->fd);
  for (int i = 0; i < MAX_STREAMS; i++) free(a->streams[i].buf);
  free(a->payload);
  free(a);
}

static int io_full(adb_t* a, void* buf, size_t len, int writing) {
  uint8_t* p = buf;
  while (len) {
    ssize_t n = writing ? send(a->fd, p, len, 0) : recv(a->fd, p, len, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      set_err(a, "conexao perdida");
      return -1;
    }
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

static int send_msg(adb_t* a, uint32_t cmd, uint32_t arg0, uint32_t arg1,
                    const void* data, uint32_t len) {
  msg_t m = {cmd, arg0, arg1, len, 0, cmd ^ 0xffffffff};
  for (uint32_t i = 0; i < len; i++) m.crc += ((const uint8_t*)data)[i];
  if (io_full(a, &m, sizeof(m), 1)) return -1;
  return len ? io_full(a, (void*)data, len, 1) : 0;
}

/* Le uma mensagem; o payload fica em a->payload. */
static int recv_msg(adb_t* a, msg_t* m) {
  if (io_full(a, m, sizeof(*m), 0)) return -1;
  if (m->magic != (m->cmd ^ 0xffffffff) || m->len > MAX_PAYLOAD) {
    set_err(a, "resposta ADB invalida");
    return -1;
  }
  return m->len ? io_full(a, a->payload, m->len, 0) : 0;
}

/* Le uma mensagem e entrega ao stream dono dela. */
static int pump(adb_t* a) {
  msg_t m;
  if (recv_msg(a, &m)) return -1;
  uint32_t local = m.arg1;
  stream_t* s = 0;
  for (int i = 0; i < MAX_STREAMS; i++)
    if (a->streams[i].local == local) s = &a->streams[i];
  if (!local || !s) return 0;

  switch (m.cmd) {
  case A_OKAY:
    s->remote = m.arg0;
    s->opening = 0;
    s->acked = 1;
    break;
  case A_WRTE:
    if (s->off && s->off == s->len) s->off = s->len = 0;
    if (s->len + m.len > s->cap) {
      s->cap = (s->len + m.len) * 2;
      s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, a->payload, m.len);
    s->len += m.len;
    return send_msg(a, A_OKAY, local, m.arg0, 0, 0);
  case A_CLSE:
    s->closed = 1;
    s->opening = 0;
    break;
  }
  return 0;
}

/* ---- Autenticacao ------------------------------------------------------ */

static EVP_PKEY* load_or_create_key(const char* keyfile) {
  EVP_PKEY* key = 0;
  FILE* f = fopen(keyfile, "r");
  if (f) {
    key = PEM_read_PrivateKey(f, 0, 0, 0);
    fclose(f);
  }
  if (key) return key;
  if (!(key = EVP_RSA_gen(KEY_BYTES * 8))) return 0;
  if ((f = fopen(keyfile, "w"))) {
    PEM_write_PrivateKey(f, key, 0, 0, 0, 0, 0);
    fclose(f);
  }
  return key;
}

/* O adbd espera PKCS#1 v1.5 com o token no lugar do digest SHA-1. O padding
 * e montado a mao para nao depender da politica de SHA-1 do OpenSSL. */
static int sign_token(EVP_PKEY* key, const uint8_t* token, size_t token_len,
                      uint8_t* sig) {
  static const uint8_t sha1_info[] = {0x30, 0x21, 0x30, 0x09, 0x06,
                                      0x05, 0x2b, 0x0e, 0x03, 0x02,
                                      0x1a, 0x05, 0x00, 0x04, 0x14};
  uint8_t em[KEY_BYTES];
  size_t tail = sizeof(sha1_info) + token_len;
  size_t sig_len = KEY_BYTES;
  int ok;

  if (token_len != 20) return -1;
  memset(em, 0xff, sizeof(em));
  em[0] = 0x00;
  em[1] = 0x01;
  em[KEY_BYTES - tail - 1] = 0x00;
  memcpy(em + KEY_BYTES - tail, sha1_info, sizeof(sha1_info));
  memcpy(em + KEY_BYTES - token_len, token, token_len);

  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(key, 0);
  ok = ctx && EVP_PKEY_sign_init(ctx) > 0 &&
       EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_NO_PADDING) > 0 &&
       EVP_PKEY_sign(ctx, sig, &sig_len, em, sizeof(em)) > 0;
  EVP_PKEY_CTX_free(ctx);
  return ok ? 0 : -1;
}

/* Chave publica no formato do Android (RSAPublicKey do mincrypt) em base64,
 * seguida de " usuario@host". Retorna o tamanho com o NUL final. */
static int encode_pubkey(EVP_PKEY* key, char* out) {
  struct {
    uint32_t len, n0inv;
    uint8_t n[KEY_BYTES], rr[KEY_BYTES];
    uint32_t e;
  } __attribute__((packed)) pk;
  BIGNUM *n = 0, *e = 0;
  BIGNUM *r32 = BN_new(), *rr = BN_new(), *rem = BN_new(), *n0inv = BN_new();
  BN_CTX* bn = BN_CTX_new();
  int len;

  EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_N, &n);
  EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_E, &e);

  BN_set_bit(r32, 32);
  BN_mod(rem, n, r32, bn);
  BN_mod_inverse(n0inv, rem, r32, bn);
  BN_set_bit(rr, KEY_BYTES * 8);
  BN_mod_sqr(rr, rr, n, bn);

  pk.len = KEY_BYTES / 4;
  pk.n0inv = 0 - (uint32_t)BN_get_word(n0inv);
  BN_bn2lebinpad(n, pk.n, KEY_BYTES);
  BN_bn2lebinpad(rr, pk.rr, KEY_BYTES);
  pk.e = (uint32_t)BN_get_word(e);

  len = EVP_EncodeBlock((uint8_t*)out, (uint8_t*)&pk, sizeof(pk));
  len += sprintf(out + len, " visor@ps5") + 1;

  BN_free(n); BN_free(e); BN_free(r32); BN_free(rr); BN_free(rem);
  BN_free(n0inv); BN_CTX_free(bn);
  return len;
}

static int tcp_connect(adb_t* a, const char* host, int port) {
  struct sockaddr_in sa = {0};
  struct pollfd pfd;
  int one = 1, err = 0;
  socklen_t errlen = sizeof(err);

  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
    set_err(a, "IP invalido: %s", host);
    return -1;
  }
  a->fd = socket(AF_INET, SOCK_STREAM, 0);
  fcntl(a->fd, F_SETFL, fcntl(a->fd, F_GETFL) | O_NONBLOCK);
  connect(a->fd, (struct sockaddr*)&sa, sizeof(sa));
  pfd.fd = a->fd;
  pfd.events = POLLOUT;
  if (poll(&pfd, 1, 4000) != 1 ||
      getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) || err) {
    set_err(a, "sem resposta de %s:%d", host, port);
    return -1;
  }
  fcntl(a->fd, F_SETFL, fcntl(a->fd, F_GETFL) & ~O_NONBLOCK);
  setsockopt(a->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return 0;
}

int adb_connect(adb_t* a, const char* host, int port, const char* keyfile,
                void (*on_wait_auth)(void*), void* ctx) {
  static const char banner[] = "host::";
  uint8_t sig[KEY_BYTES];
  char pub[1024];
  EVP_PKEY* key;
  int signed_once = 0, rc = -1;
  msg_t m;

  if (tcp_connect(a, host, port)) return -1;
  if (!(key = load_or_create_key(keyfile))) {
    set_err(a, "nao consegui criar a chave em %s", keyfile);
    return -1;
  }
  if (send_msg(a, A_CNXN, A_VERSION, MAX_PAYLOAD, banner, sizeof(banner)))
    goto out;

  for (;;) {
    if (recv_msg(a, &m)) goto out;
    if (m.cmd == A_CNXN) {
      a->max_payload = m.arg1 < MAX_PAYLOAD ? m.arg1 : MAX_PAYLOAD;
      rc = 0;
      goto out;
    }
    if (m.cmd != A_AUTH || m.arg0 != AUTH_TOKEN) continue;
    if (!signed_once) {
      signed_once = 1;
      if (sign_token(key, a->payload, m.len, sig)) {
        set_err(a, "falha ao assinar o token");
        goto out;
      }
      if (send_msg(a, A_AUTH, AUTH_SIGNATURE, 0, sig, sizeof(sig))) goto out;
    } else {
      /* Chave desconhecida: manda a publica e espera o popup no aparelho. */
      int len = encode_pubkey(key, pub);
      if (send_msg(a, A_AUTH, AUTH_RSAPUBLICKEY, 0, pub, (uint32_t)len))
        goto out;
      if (on_wait_auth) on_wait_auth(ctx);
    }
  }

out:
  EVP_PKEY_free(key);
  return rc;
}

/* ---- Streams ----------------------------------------------------------- */

int adb_open(adb_t* a, const char* service) {
  int id = -1;
  for (int i = 0; i < MAX_STREAMS && id < 0; i++)
    if (!a->streams[i].remote && !a->streams[i].opening) id = i;
  if (id < 0) return -1;
  stream_t* s = &a->streams[id];
  s->local = ++a->next_local;
  s->opening = 1;
  s->closed = 0;
  s->len = s->off = 0;
  if (send_msg(a, A_OPEN, s->local, 0, service,
               (uint32_t)strlen(service) + 1))
    return -1;
  while (s->opening)
    if (pump(a)) return -1;
  if (s->closed) {
    s->remote = 0;
    s->local = 0;
    return -1;
  }
  return id;
}

size_t adb_drain(adb_t* a, int stream, void* buf, size_t len) {
  stream_t* s = &a->streams[stream];
  size_t n = s->len - s->off;
  if (n > len) n = len;
  memcpy(buf, s->buf + s->off, n);
  s->off += n;
  return n;
}

int adb_read(adb_t* a, int stream, void* buf, size_t len) {
  stream_t* s = &a->streams[stream];
  while (s->off == s->len) {
    if (s->closed) return 0;
    if (pump(a)) return -1;
  }
  return (int)adb_drain(a, stream, buf, len);
}

int adb_read_full(adb_t* a, int stream, void* buf, size_t len) {
  uint8_t* p = buf;
  while (len) {
    int n = adb_read(a, stream, p, len);
    if (n <= 0) {
      if (!n) set_err(a, "o aparelho fechou a transmissao");
      return -1;
    }
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

int adb_write(adb_t* a, int stream, const void* buf, size_t len) {
  stream_t* s = &a->streams[stream];
  const uint8_t* p = buf;
  while (len) {
    uint32_t n = len < a->max_payload ? (uint32_t)len : a->max_payload;
    s->acked = 0;
    if (send_msg(a, A_WRTE, s->local, s->remote, p, n)) return -1;
    while (!s->acked) {
      if (s->closed) return -1;
      if (pump(a)) return -1;
    }
    p += n;
    len -= n;
  }
  return 0;
}

void adb_close(adb_t* a, int stream) {
  stream_t* s = &a->streams[stream];
  if (s->remote) send_msg(a, A_CLSE, s->local, s->remote, 0, 0);
  s->remote = 0;
  s->local = 0;
  s->closed = 1;
}

/* ---- Sync -------------------------------------------------------------- */

static int sync_req(adb_t* a, int st, const char* id, const void* data,
                    uint32_t len) {
  uint8_t hdr[8];
  memcpy(hdr, id, 4);
  memcpy(hdr + 4, &len, 4);
  if (adb_write(a, st, hdr, 8)) return -1;
  return data ? adb_write(a, st, data, len) : 0;
}

int adb_push(adb_t* a, const char* local, const char* remote) {
  static uint8_t chunk[64 * 1024];
  char spec[512], resp[8];
  FILE* f = fopen(local, "rb");
  int st, rc = -1;
  size_t n;

  if (!f) {
    set_err(a, "arquivo ausente: %s", local);
    return -1;
  }
  if ((st = adb_open(a, "sync:")) < 0) {
    set_err(a, "o aparelho recusou o envio de arquivo");
    fclose(f);
    return -1;
  }
  snprintf(spec, sizeof(spec), "%s,%d", remote, 0100644);
  if (sync_req(a, st, "SEND", spec, (uint32_t)strlen(spec))) goto out;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    if (sync_req(a, st, "DATA", chunk, (uint32_t)n)) goto out;
  if (sync_req(a, st, "DONE", 0, (uint32_t)time(0))) goto out;
  if (adb_read_full(a, st, resp, 8)) goto out;
  if (memcmp(resp, "OKAY", 4)) {
    set_err(a, "o aparelho rejeitou o arquivo");
    goto out;
  }
  sync_req(a, st, "QUIT", 0, 0);
  rc = 0;

out:
  adb_close(a, st);
  fclose(f);
  return rc;
}
