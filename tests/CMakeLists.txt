include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# cmake/windows/compilerconfig.cmake defines UNICODE/_UNICODE globally (needed
# by OBS itself) via a directory-scoped add_compile_definitions, which is also
# inherited by targets pulled in through FetchContent. Catch2's amalgamated
# main (catch_main.cpp, built inside the Catch2WithMain target), when it sees
# _UNICODE on Windows, compiles "wmain" instead of "main", while our .exe is a
# plain console app that looks for "main" -> LNK2001 unresolved external
# symbol main. DO_NOT_USE_WMAIN is Catch2's built-in flag to disable this
# behavior; we set it specifically on the target where catch_main.cpp is
# actually compiled.
if(TARGET Catch2WithMain)
  target_compile_definitions(Catch2WithMain PRIVATE DO_NOT_USE_WMAIN)
endif()

add_executable(obs-backuper-core-tests
  test_ConfigPaths.cpp
  test_FileCollector.cpp
  test_Manifest.cpp
  test_ZipArchive.cpp
  test_BackupManager.cpp
  test_RestoreManager.cpp
  test_ErrorKind.cpp
  test_PluginSettings.cpp
  test_ProgressEstimator.cpp
  test_UpdateCheck.cpp
  test_Locale.cpp
  test_EncryptedContainer.cpp
  test_EncryptedBackupFlow.cpp
  test_PathUtf8.cpp
  test_ActiveSelection.cpp
  test_GuardedCall.cpp
  test_SceneCollectionSnapshot.cpp
  test_RestoreResult.cpp
  test_RestoreFlow.cpp
  test_Crypto.cpp
  test_WindowsLayout.cpp
  test_WindowsScenario.cpp
)

target_link_libraries(obs-backuper-core-tests PRIVATE obs-backuper-core Catch2::Catch2WithMain)

# test_Locale.cpp reads data/locale/*.ini and scans src/ -- it needs the repo
# root (the parent of this tests/ directory).
target_compile_definitions(obs-backuper-core-tests PRIVATE
  OBS_BACKUPER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_test(NAME obs-backuper-core-tests COMMAND obs-backuper-core-tests)
