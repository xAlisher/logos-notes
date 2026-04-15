cmake_minimum_required(VERSION 3.28)
project(logos-notes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml QuickControls2 Sql RemoteObjects Widgets QuickWidgets)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW) 
qt_policy(SET QTP0004 NEW)

# ── libsodium ────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/NotesBackend.h
    src/core/NotesBackend.cpp
    src/core/CryptoManager.h
    src/core/CryptoManager.cpp
    src/core/DatabaseManager.h
    src/core/DatabaseManager.cpp
    src/core/KeyManager.h
    src/core/KeyManager.cpp
)

qt_add_executable(logos-notes ${SOURCES})

# ── QML module ───────────────────────────────────────────────────────────────
qt_add_qml_module(logos-notes
    URI LogosNotes
    VERSION 1.0
    QML_FILES
        src/ui/main.qml
        src/ui/screens/ImportScreen.qml
        src/ui/screens/UnlockScreen.qml
        src/ui/screens/NoteScreen.qml
        src/ui/components/PinInput.qml
)

# ── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(logos-notes PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Sql
    PkgConfig::sodium
)

target_include_directories(logos-notes PRIVATE src)

# ── Logos Design System ──────────────────────────────────────────────────────
# Permanent path in nix store — set QML_IMPORT_PATH at runtime for standalone runs:
#   QML_IMPORT_PATH=/nix/store/w9ra12n0yabd275v33m8x7lqnnrcgb9f-logos-design-system-1.0.0/lib \
#     ./build/logos-notes
if(DEFINED ENV{LOGOS_DESIGN_SYSTEM_ROOT})
    set(LOGOS_DS_LIB "$ENV{LOGOS_DESIGN_SYSTEM_ROOT}/lib")
else()
    set(LOGOS_DS_LIB "/nix/store/w9ra12n0yabd275v33m8x7lqnnrcgb9f-logos-design-system-1.0.0/lib")
endif()
set_target_properties(logos-notes PROPERTIES
    QT_QML_IMPORT_PATH "${LOGOS_DS_LIB}"
)
# Also bake it in for qmlsc / Qt Creator tooling
target_compile_definitions(logos-notes PRIVATE
    LOGOS_DS_LIB="${LOGOS_DS_LIB}"
)

# ── Notes Module Plugin ──────────────────────────────────────────────────────
# Builds notes_plugin.so — the core module the Logos App shell loads.

# Nix store paths (override via env vars for CI / alternative setups)
if(DEFINED ENV{LOGOS_LIBLOGOS_HEADERS})
    set(LOGOS_LIBLOGOS_HEADERS "$ENV{LOGOS_LIBLOGOS_HEADERS}")
else()
    set(LOGOS_LIBLOGOS_HEADERS
        "/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include")
endif()

if(DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK "$ENV{LOGOS_CPP_SDK_ROOT}")
else()
    set(LOGOS_CPP_SDK
        "/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk")
endif()

set(PLUGIN_SOURCES
    src/plugin/NotesPlugin.h
    src/plugin/NotesPlugin.cpp
    src/core/NotesBackend.h
    src/core/NotesBackend.cpp
    src/core/CryptoManager.h
    src/core/CryptoManager.cpp
    src/core/DatabaseManager.h
    src/core/DatabaseManager.cpp
    src/core/KeyManager.h
    src/core/KeyManager.cpp
    src/core/StorageClient.h
    src/core/StorageClient.cpp
    src/core/LogosStorageTransport.cpp
)

add_library(notes_plugin MODULE ${PLUGIN_SOURCES})

# No 'lib' prefix — output must be notes_plugin.so, not libnotes_plugin.so
set_target_properties(notes_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "notes_plugin"
    # plugin_metadata.json must be findable relative to the source file
    # for Q_PLUGIN_METADATA(FILE "plugin_metadata.json")
    COMPILE_OPTIONS ""
)

target_include_directories(notes_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

target_link_libraries(notes_plugin PRIVATE
    Qt6::Core
    Qt6::Sql
    PkgConfig::sodium
    "${LOGOS_CPP_SDK}/lib/liblogos_sdk.a"
)

# Q_PLUGIN_METADATA(FILE "plugin_metadata.json") resolves relative to the
# source file. Copy it next to NotesPlugin.cpp so moc can find it.
configure_file(
    src/plugin/plugin_metadata.json
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin/plugin_metadata.json
    COPYONLY
)

# ── Notes UI Plugin (IComponent wrapper that embeds Main.qml) ─────────────────
# Needed so loadLegacyUiModule pre-loads the notes core dep before QML starts.

set(LOGOS_VIEW_MODULE_RUNTIME
    "/nix/store/s0jhc3nwm0yd23kn3xh4cvk5g4v3a8xd-logos-view-module-runtime-1.0.0")

# IComponent.h lives in the tictactoe-ui package (same IID, same layout)
set(ICOMPONENT_INCLUDE_DIR
    "/nix/store/1lz55df1b2sw6gl8f0x1s7hrrvfr2rh3-tictactoe-ui/interfaces")

set(UI_PLUGIN_SOURCES
    src/ui_plugin/NotesUiPlugin.h
    src/ui_plugin/NotesUiPlugin.cpp
    # Compile LogosQmlBridge from the logos-app source used to build the AppImage
    /nix/store/ci0j8w9vrpkvr6cci5h8msbfav78y34f-source/src/LogosQmlBridge.cpp
)

add_library(notes_ui_plugin MODULE ${UI_PLUGIN_SOURCES})

set_target_properties(notes_ui_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "notes_ui_plugin"
)

target_include_directories(notes_ui_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
    "${LOGOS_VIEW_MODULE_RUNTIME}/include"
    "${ICOMPONENT_INCLUDE_DIR}"
    /nix/store/ci0j8w9vrpkvr6cci5h8msbfav78y34f-source/src
)

target_link_libraries(notes_ui_plugin PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Widgets
    Qt6::QuickWidgets
    "${LOGOS_CPP_SDK}/lib/liblogos_sdk.a"
)

configure_file(
    src/ui_plugin/ui_plugin_metadata.json
    ${CMAKE_CURRENT_BINARY_DIR}/src/ui_plugin/ui_plugin_metadata.json
    COPYONLY
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

# qmllint — run as a ctest entry so QML errors surface alongside C++ failures.
# Skipped gracefully if the binary is not found (e.g. CI without Qt installed).
find_program(QMLLINT_EXECUTABLE qmllint
    HINTS "${CMAKE_PREFIX_PATH}/bin"
    DOC "Qt QML linter"
)
if(QMLLINT_EXECUTABLE)
    add_test(NAME lint_main_qml
        COMMAND ${QMLLINT_EXECUTABLE} "${CMAKE_SOURCE_DIR}/plugins/notes_ui/Main.qml"
    )
    set_tests_properties(lint_main_qml PROPERTIES
        # Fail on Error only. Unqualified-access Warnings for `logos` are
        # expected false positives — it is an injected bridge object that
        # qmllint cannot resolve without a .qmltypes file.
        FAIL_REGULAR_EXPRESSION "Error:"
        LABELS "qml"
    )
else()
    message(STATUS "qmllint not found — QML lint test skipped")
endif()

# Common core sources shared by all test targets
set(TEST_CORE_SOURCES
    src/core/DatabaseManager.h
    src/core/DatabaseManager.cpp
    src/core/CryptoManager.h
    src/core/CryptoManager.cpp
    src/core/KeyManager.h
    src/core/KeyManager.cpp
    src/core/NotesBackend.h
    src/core/NotesBackend.cpp
)

set(TEST_CORE_LIBS
    Qt6::Core
    Qt6::Test
    Qt6::Sql
    PkgConfig::sodium
)

add_executable(test_multi_note tests/test_multi_note.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_multi_note PRIVATE src)
target_link_libraries(test_multi_note PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_multi_note COMMAND test_multi_note)

add_executable(test_security tests/test_security.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_security PRIVATE src)
target_link_libraries(test_security PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_security COMMAND test_security)

add_executable(test_backup tests/test_backup.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_backup PRIVATE src)
target_link_libraries(test_backup PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_backup COMMAND test_backup)

add_executable(test_account tests/test_account.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_account PRIVATE src)
target_link_libraries(test_account PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_account COMMAND test_account)

add_executable(test_note_api tests/test_note_api.cpp ${TEST_CORE_SOURCES})
target_include_directories(test_note_api PRIVATE src)
target_link_libraries(test_note_api PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_note_api COMMAND test_note_api)

add_executable(test_plugin
    tests/test_plugin.cpp
    tests/logos_api_stub.cpp
    "${LOGOS_CPP_SDK}/include/cpp/logos_api.h"
    src/plugin/NotesPlugin.h
    src/plugin/NotesPlugin.cpp
    ${TEST_CORE_SOURCES}
)
target_include_directories(test_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)
target_link_libraries(test_plugin PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_plugin COMMAND test_plugin)

# StorageClient tests — do NOT link the logos SDK; the transport
# abstraction lets the test use a mock with no real IPC.
add_executable(test_storage_client
    tests/test_storage_client.cpp
    src/core/StorageClient.h
    src/core/StorageClient.cpp
)
target_include_directories(test_storage_client PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)
target_link_libraries(test_storage_client PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME test_storage_client COMMAND test_storage_client)

# Backup + Storage integration tests — chains NotesBackend (local export/import)
# with StorageClient (mock transport). No SDK linkage needed.
add_executable(test_backup_storage
    tests/test_backup_storage.cpp
    src/core/StorageClient.h
    src/core/StorageClient.cpp
    ${TEST_CORE_SOURCES}
)
target_include_directories(test_backup_storage PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)
target_link_libraries(test_backup_storage PRIVATE ${TEST_CORE_LIBS})
add_test(NAME test_backup_storage COMMAND test_backup_storage)

# ── Install ───────────────────────────────────────────────────────────────────

# Dev install convention (mirrors keycard-basecamp pattern):
#   Primary:  LogosApp   — cmake --install target
#   Mirror:   LogosBasecamp — AppImage runtime dir
set(LOGOS_APP_DATA "$ENV{HOME}/.local/share/Logos/LogosApp")
set(NOTES_MODULE_DIR "${LOGOS_APP_DATA}/modules/notes")
set(NOTES_UI_DIR     "${LOGOS_APP_DATA}/plugins/notes_ui")

# Clean up old module versions before installing to prevent version conflicts.
# Logos App may load old backup directories (notes.bak, notes.old) instead of current build.
# Honor DESTDIR for staged installs (e.g. DESTDIR=/tmp/staged cmake --install).
install(CODE "
    set(_modules_dir \"\$ENV{DESTDIR}${LOGOS_APP_DATA}/modules\")
    file(GLOB _old_notes \"\${_modules_dir}/notes.*\")
    foreach(_dir \${_old_notes})
        message(STATUS \"Removing old notes module: \${_dir}\")
        file(REMOVE_RECURSE \"\${_dir}\")
    endforeach()
")

install(TARGETS logos-notes DESTINATION "$ENV{HOME}/bin")

install(TARGETS notes_plugin
    LIBRARY DESTINATION "${NOTES_MODULE_DIR}"
)
install(FILES modules/notes/manifest.json
    DESTINATION "${NOTES_MODULE_DIR}"
)
install(TARGETS notes_ui_plugin
    LIBRARY DESTINATION "${NOTES_UI_DIR}"
)
install(FILES
    plugins/notes_ui/manifest.json
    plugins/notes_ui/metadata.json
    plugins/notes_ui/Main.qml
    assets/icons/Add.svg
    assets/icons/Lock.svg
    assets/icons/close.svg
    DESTINATION "${NOTES_UI_DIR}"
)
install(FILES assets/icons/app_icon.png
    DESTINATION "${NOTES_UI_DIR}/icons"
)

# Patch RUNPATH on notes_plugin.so after install:
#   1. Collect Nix store lib dirs (Qt, libsodium, etc.) from the build artifact.
#   2. Add system lib paths so Nix ld-linux can find system libs (libpcsclite etc.).
find_program(PATCHELF_EXECUTABLE patchelf)
if(PATCHELF_EXECUTABLE)
    install(CODE "
        set(_plugin \"${NOTES_MODULE_DIR}/notes_plugin.so\")
        set(_build_so \"${CMAKE_BINARY_DIR}/notes_plugin.so\")
        if(EXISTS \"\${_plugin}\")
            execute_process(
                COMMAND bash -c \"ldd '\${_build_so}' | grep nix/store | awk '{print \\\$3}' | xargs -I{} dirname {} | sort -u | tr '\\n' ':' | sed 's/:$//'\"\
                OUTPUT_VARIABLE _nix_paths
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(_rpath \"\\\$ORIGIN:\${_nix_paths}:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu\")
            execute_process(
                COMMAND ${PATCHELF_EXECUTABLE} --set-rpath \"\${_rpath}\" \"\${_plugin}\"
            )
            message(STATUS \"Patched RUNPATH on notes_plugin.so\")
        endif()
    ")
endif()

# Mirror notes module + UI plugin to LogosBasecamp (AppImage runtime dir).
install(CODE "
    set(_mod_src \"${NOTES_MODULE_DIR}\")
    set(_mod_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/modules/notes\")
    message(STATUS \"Mirroring notes module to LogosBasecamp: \${_mod_dst}\")
    file(COPY \"\${_mod_src}/\" DESTINATION \"\${_mod_dst}\")

    set(_ui_src \"${NOTES_UI_DIR}\")
    set(_ui_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/plugins/notes_ui\")
    message(STATUS \"Mirroring notes UI to LogosBasecamp: \${_ui_dst}\")
    file(GLOB _ui_files \"\${_ui_src}/*\")
    foreach(_f \${_ui_files})
        if(NOT IS_DIRECTORY \"\${_f}\")
            file(COPY \"\${_f}\" DESTINATION \"\${_ui_dst}\")
        endif()
    endforeach()
    file(GLOB _ui_icons \"\${_ui_src}/icons/*\")
    foreach(_f \${_ui_icons})
        file(COPY \"\${_f}\" DESTINATION \"\${_ui_dst}/icons\")
    endforeach()
    message(STATUS \"Mirror complete\")
")
