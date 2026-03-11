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

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS logos-notes DESTINATION bin)
