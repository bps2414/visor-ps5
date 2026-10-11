cmake_minimum_required(VERSION 3.16)

project(visor C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
find_package(RmlUi REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET libcrypto)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)

add_library(core STATIC src/adb.c src/session.cpp)
target_link_libraries(core PUBLIC SDL2::SDL2 PkgConfig::FFMPEG PkgConfig::OPENSSL)

add_executable(visor
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/RmlUi/Backend_SDL_Renderer.cpp
    src/RmlUi/Renderer_SDL.cpp
    src/RmlUi/Platform_SDL.cpp
)
target_link_libraries(visor PRIVATE
    core
    SDL2::SDL2main
    RmlUi::Core
    PkgConfig::FREETYPE2
    PkgConfig::SDL2_IMAGE
)

# So no PS5: instala o atalho na tela inicial.
if(PROSPERO)
  # Instalador do atalho da tela inicial. E um ELF a parte porque a
  # libSceAppInstUtil nao carrega dentro do app; o websrv o roda como daemon.
  add_executable(visor-tile src/tile.c)
  # A AppInstUtil so carrega com estas outras junto (ver samples/install_app do SDK).
  target_link_libraries(visor-tile PRIVATE SceIpmi SceAppInstUtil SceUserService SceSystemService)
else()
  # Teste do nucleo contra um headset real; so faz sentido no PC.
  add_executable(coretest tools/coretest.cpp)
  target_link_libraries(coretest PRIVATE core)
endif()
