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

# No PS5 o app vai embutido no visor.elf (o launcher), entao muda de nome.
if(PROSPERO)
  set(APP visor-app)
else()
  set(APP visor)
endif()

add_executable(${APP}
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/RmlUi/Backend_SDL_Renderer.cpp
    src/RmlUi/Renderer_SDL.cpp
    src/RmlUi/Platform_SDL.cpp
)
target_link_libraries(${APP} PRIVATE
    core
    SDL2::SDL2main
    RmlUi::Core
    PkgConfig::FREETYPE2
    PkgConfig::SDL2_IMAGE
)

if(PROSPERO)
  # visor.elf: extrai o app, cria o atalho na tela inicial e abre o app.
  set(APP_ELF ${CMAKE_BINARY_DIR}/visor-app.elf)
  add_custom_command(OUTPUT ${APP_ELF} DEPENDS visor-app
      COMMAND ${CMAKE_STRIP} -o ${APP_ELF} $<TARGET_FILE:visor-app>)
  file(GLOB EMBEDDED assets/* fonts/* sce_sys/*)
  set_source_files_properties(src/launcher.c PROPERTIES
      OBJECT_DEPENDS "${APP_ELF};${EMBEDDED}")

  add_executable(visor
      src/launcher.c
      third_party/websrv/hbldr.c
      third_party/websrv/elfldr.c
      third_party/websrv/pt.c
  )
  # Os dois primeiros sao onde o .incbin do launcher procura os arquivos.
  target_include_directories(visor PRIVATE
      ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} third_party/websrv)
  # A AppInstUtil so carrega com a Ipmi junto (ver samples/install_app do SDK).
  target_link_libraries(visor PRIVATE
      kernel_sys SceIpmi SceAppInstUtil SceUserService SceSystemService)
else()
  # Teste do nucleo contra um headset real; so faz sentido no PC.
  add_executable(coretest tools/coretest.cpp)
  target_link_libraries(coretest PRIVATE core)
endif()
