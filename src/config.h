// Ajustes salvos em <pasta do app>/visor.conf, um "chave=valor" por linha.
#pragma once

#include <string>

#include "session.h"

struct Config {
  StreamSettings stream;
  bool english = false;

  void Load(const std::string& dir);
  void Save(const std::string& dir) const;
};
