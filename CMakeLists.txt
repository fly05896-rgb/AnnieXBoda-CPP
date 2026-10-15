cmake_minimum_required(VERSION 3.22)
project(AnnieXBodaCPP VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# 1. Locate TDLib (either pre-installed in /usr/local or built from third_party/td)
find_package(Td QUIET PATHS "${CMAKE_SOURCE_DIR}/deps/td/lib/cmake/Td" "/usr/local/lib/cmake/Td")
if(NOT Td_FOUND)
    message(STATUS "TDLib package config not found in /usr/local; using third_party/td headers & libs")
    include_directories(
        "${CMAKE_SOURCE_DIR}/third_party/td"
        "${CMAKE_SOURCE_DIR}/deps/td/include"
        "/usr/local/include"
    )
    link_directories("${CMAKE_SOURCE_DIR}/deps/td/lib" "/usr/local/lib")
    set(TD_LIBS tdclient tdcore tdapi tdactor tdnet tddb tdsqlite tdutils)
else()
    set(TD_LIBS Td::TdStatic)
endif()

# 2. Locate Native C++ NTgCalls & WRTC headers and libraries (NO PyTgCalls)
include_directories(
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/third_party/ntgcalls/ntgcalls/include"
    "${CMAKE_SOURCE_DIR}/third_party/ntgcalls/wrtc/include"
    "${CMAKE_SOURCE_DIR}/deps/ntgcalls/include"
)
link_directories(
    "${CMAKE_SOURCE_DIR}/third_party/ntgcalls/static-output/lib"
    "${CMAKE_SOURCE_DIR}/third_party/ntgcalls/shared-output/lib"
    "${CMAKE_SOURCE_DIR}/deps/ntgcalls/lib"
    "/usr/local/lib"
)

set(BOT_SOURCES
    config.cpp
    strings/strings.cpp
    AnnieXMedia/misc.cpp
    AnnieXMedia/core/dir.cpp
    AnnieXMedia/core/git.cpp
    AnnieXMedia/core/mongo.cpp
    AnnieXMedia/core/bot.cpp
    AnnieXMedia/core/userbot.cpp
    AnnieXMedia/core/call.cpp
    AnnieXMedia/mongo/couples_db.cpp
    AnnieXMedia/mongo/pretenderdb.cpp
    AnnieXMedia/mongo/welcomedb.cpp
    AnnieXMedia/utils/formatters.cpp
    AnnieXMedia/utils/database.cpp
    AnnieXMedia/utils/cookie_handler.cpp
    AnnieXMedia/utils/thumbnails.cpp
    AnnieXMedia/utils/inline.cpp
    AnnieXMedia/utils/stream/autoclear.cpp
    AnnieXMedia/utils/stream/queue.cpp
    AnnieXMedia/utils/stream/stream.cpp
    AnnieXMedia/platforms/Youtube.cpp
    AnnieXMedia/platforms/SongDownloader.cpp
    AnnieXMedia/platforms/Spotify.cpp
    AnnieXMedia/platforms/Soundcloud.cpp
    AnnieXMedia/platforms/Apple.cpp
    AnnieXMedia/platforms/Resso.cpp
    AnnieXMedia/platforms/Carbon.cpp
    AnnieXMedia/platforms/Telegram.cpp
    AnnieXMedia/plugins/plugin_manager.cpp
    AnnieXMedia/plugins/play/play_plugins.cpp
    AnnieXMedia/plugins/admins/admin_plugins.cpp
    AnnieXMedia/plugins/bot/bot_plugins.cpp
    AnnieXMedia/plugins/sudo/sudo_plugins.cpp
    AnnieXMedia/plugins/AzanSystem/azan_system.cpp
    AnnieXMedia/plugins/security/security.cpp
    AnnieXMedia/plugins/ai/ai.cpp
    AnnieXMedia/plugins/Manager/manager_plugins.cpp
    AnnieXMedia/plugins/tools/tools_misc_kishu.cpp
    AnnieXMedia/main.cpp
)

add_executable(annie_music_bot ${BOT_SOURCES})

target_link_libraries(annie_music_bot PRIVATE
    ntgcalls-native
    wrtc
    ${TD_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    dl
)
