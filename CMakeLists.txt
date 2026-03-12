cmake_minimum_required(VERSION 3.28)
project(logos-notes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml QuickControls2 Sql)
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

# ── Install ───────────────────────────────────────────────────────────────────

set(LOGOS_APP_DATA "$ENV{HOME}/.local/share/Logos/LogosApp")
set(NOTES_MODULE_DIR "${LOGOS_APP_DATA}/modules/notes")
set(NOTES_UI_DIR     "${LOGOS_APP_DATA}/plugins/notes_ui")

install(TARGETS logos-notes DESTINATION "$ENV{HOME}/bin")

install(TARGETS notes_plugin
    LIBRARY DESTINATION "${NOTES_MODULE_DIR}"
)
install(FILES modules/notes/manifest.json
    DESTINATION "${NOTES_MODULE_DIR}"
)
install(FILES
    plugins/notes_ui/manifest.json
    plugins/notes_ui/main.qml
    DESTINATION "${NOTES_UI_DIR}"
)
