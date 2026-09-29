cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# miniz (MIT) — header/source ZIP library used by ZipArchive for backup
# archiving. Fetched at configure time, same
# pattern already used for Catch2 below; miniz's own CMakeLists.txt detects it
# is being built as a subproject and disables its examples/tests/install
# targets automatically.
include(FetchContent)
FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 3.1.2
)
FetchContent_MakeAvailable(miniz)

# compilerconfig.cmake enables -Werror (and, on Clang, -Wcomma) project-wide
# via directory-scoped flags, which the miniz target inherits too since it's
# pulled in via add_subdirectory at this same scope. miniz's own tinfl
# decompressor uses the comma operator intentionally and isn't warning-clean
# under -Werror, so silence that specifically for miniz, same as we already
# do for our own plugin target below.
if(TARGET miniz)
  target_compile_options(miniz PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-comma>)

  # miniz.h, when included normally, also declares a block of "static inline"
  # zlib-compatible wrapper functions (deflateInit, inflate, compress, ...).
  # miniz.c itself includes miniz.h and never calls these by their zlib
  # names, so they're unused static functions under -Werror -Wunused-function
  # on Clang -- both in miniz's own build and in ours (ZipArchive.*, tests),
  # since we only ever use the mz_zip_writer_*/mz_zip_reader_* API.
  # MINIZ_NO_ZLIB_COMPATIBLE_NAMES is miniz's own documented switch to skip
  # generating them; PUBLIC so it applies to miniz's own sources too, not
  # just consumers (INTERFACE alone left miniz.c itself still tripping this).
  target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
endif()

# Monocypher (BSD-2-Clause / CC0) -- the only cryptographic dependency (Stage 7:
# password-protected backups): Argon2id key derivation, XChaCha20-Poly1305
# AEAD and secure wiping. Chosen over libsodium because it is one C file that
# builds identically under MSVC/Xcode without autotools. The upstream repo has no CMakeLists.txt, so
# FetchContent_MakeAvailable only downloads it and we define the target here.
FetchContent_Declare(
  monocypher
  GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
  GIT_TAG 4.0.2
)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC "${monocypher_SOURCE_DIR}/src/monocypher.c")
target_include_directories(monocypher PUBLIC "${monocypher_SOURCE_DIR}/src")
set_property(TARGET monocypher PROPERTY POSITION_INDEPENDENT_CODE ON)

# compilerconfig.cmake turns on -Werror / /WX project-wide, and this target
# inherits it. Monocypher is third-party code that is not warning-clean under
# those flags (e.g. a deliberate 64->32-bit conversion at monocypher.c:881,
# rejected by -Wshorten-64-to-32 on Clang and C4244 on MSVC), so silence
# warnings for this one target -- same approach as miniz below. Our own
# sources stay under the strict flags.
target_compile_options(monocypher PRIVATE
  $<$<C_COMPILER_ID:MSVC>:/W0 /WX->
  $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-w>
)

# Core with no OBS/Qt dependencies — only std::filesystem and miniz, fully
# covered by unit tests on any machine (see tests/). BackupManager here only
# contains the pure archiving logic (CreateBackup, filename generation); the
# OBS-coupled CollectFiles()/GetObsDataDir() glue lives in
# src/ObsConfigPathProvider.* and is only linked into the plugin module below.
add_library(obs-backuper-core STATIC
  src/core/ActiveSelection.cpp
  src/core/ActiveSelection.h
  src/core/SceneCollectionSnapshot.cpp
  src/core/SceneCollectionSnapshot.h
  src/core/ConfigPaths.cpp
  src/core/ConfigPaths.h
  src/core/Crypto.cpp
  src/core/Crypto.h
  src/core/EncryptedContainer.cpp
  src/core/EncryptedContainer.h
  src/core/PasswordStrength.cpp
  src/core/PasswordStrength.h
  src/core/SecureFile.cpp
  src/core/SecureFile.h
  src/core/ErrorKind.cpp
  src/core/ErrorKind.h
  src/core/FileCollector.cpp
  src/core/FileCollector.h
  src/core/Manifest.cpp
  src/core/Manifest.h
  src/core/PluginSettings.cpp
  src/core/PluginSettings.h
  src/core/ProgressEstimator.cpp
  src/core/ProgressEstimator.h
  src/core/UpdateCheck.cpp
  src/core/UpdateCheck.h
  src/core/BackupManager.cpp
  src/core/BackupManager.h
  src/core/RestoreManager.cpp
  src/core/RestoreManager.h
  src/core/ZipArchive.cpp
  src/core/ZipArchive.h
)
target_include_directories(obs-backuper-core PUBLIC src)
target_link_libraries(obs-backuper-core PUBLIC miniz monocypher)
if(WIN32)
  target_link_libraries(obs-backuper-core PUBLIC bcrypt) # BCryptGenRandom (CSPRNG)
endif()
set_property(TARGET obs-backuper-core PROPERTY POSITION_INDEPENDENT_CODE ON)

option(ENABLE_BACKUPER_TESTS "Build unit tests for the OBS/Qt-independent core logic" ON)
if(ENABLE_BACKUPER_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE obs-backuper-core)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/ObsConfigPathProvider.cpp
    src/ObsConfigPathProvider.h
    src/ui/BackupDialog.cpp
    src/ui/BackupDialog.h
    src/ui/ErrorDialog.cpp
    src/ui/ErrorDialog.h
    src/ui/PasswordDialog.cpp
    src/ui/PasswordDialog.h
    src/ui/ProgressDialog.cpp
    src/ui/ProgressDialog.h
    src/worker/BackupWorker.cpp
    src/worker/BackupWorker.h
    src/worker/RestoreWorker.cpp
    src/worker/RestoreWorker.h
    src/worker/UpdateChecker.cpp
    src/worker/UpdateChecker.h
    src/worker/ValidateWorker.cpp
    src/worker/ValidateWorker.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
