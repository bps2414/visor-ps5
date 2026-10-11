/* Cliente ADB minimo sobre TCP: autenticacao RSA, streams e sync push.
 * Uma thread so por conexao; adb_abort() e a unica chamada segura de outra. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct adb adb_t;

adb_t* adb_new(void);
void adb_free(adb_t* a);

/* Derruba o socket; qualquer chamada bloqueada na outra thread retorna erro. */
void adb_abort(adb_t* a);

/* Conecta e autentica. 'keyfile' guarda a chave RSA (criada se nao existir).
 * 'on_wait_auth' e chamado quando o aparelho mostra o popup de autorizacao;
 * a chamada fica bloqueada ate o usuario aceitar. Retorna 0 ou -1. */
int adb_connect(adb_t* a, const char* host, int port, const char* keyfile,
                void (*on_wait_auth)(void*), void* ctx);

const char* adb_error(const adb_t* a);

/* Abre um servico ("shell:...", "sync:", "localabstract:..."). Retorna o id
 * do stream (>= 0) ou -1 se o aparelho recusou. */
int adb_open(adb_t* a, const char* service);

/* Bloqueia ate ter dados; retorna bytes lidos, 0 se o stream fechou, -1 em erro. */
int adb_read(adb_t* a, int stream, void* buf, size_t len);
/* Le exatamente 'len' bytes. Retorna 0 ou -1. */
int adb_read_full(adb_t* a, int stream, void* buf, size_t len);
/* Pega so o que ja esta no buffer, sem bloquear. */
size_t adb_drain(adb_t* a, int stream, void* buf, size_t len);

int adb_write(adb_t* a, int stream, const void* buf, size_t len);
void adb_close(adb_t* a, int stream);

/* Envia um arquivo local para 'remote' (modo 0644). Retorna 0 ou -1. */
int adb_push(adb_t* a, const char* local, const char* remote);

#ifdef __cplusplus
}
#endif
