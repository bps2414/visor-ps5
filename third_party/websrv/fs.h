/* Shims: o hbldr.c do websrv inclui estes headers; aqui so existe o que ele usa. */
#pragma once
#include <stddef.h>
#include <stdint.h>
uint8_t* fs_readfile(const char* path, size_t* size);
