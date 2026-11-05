cmake_minimum_required(VERSION 3.20)
project(rawnes CXX C RC)

# ---------------------------------------------------------------------------
# Ziel: EINE Datei. Keine Begleit-DLLs ausser denen, die zu Windows gehoeren.
#
# Aufbau wie bei raw-turn: Dear ImGui statt Qt, Win32 + OpenGL2 fuer Fenster
# und Zeichnung, MinGW-Laufzeit statisch gebunden. Die FPGA-Kerne, die
# Intro-Bilder und das Programmsymbol stecken als Windows-Ressourcen in der
# Exe (siehe src/app.rc.in).
#
# Die Kernschicht (src/core) ist unveraendert aus der Qt-Fassung uebernommen:
# Bedingungsauswertung, RA-Client, ROM-Index, Mapper-Installer, Protokoll.
# Ersetzt wurden nur Oberflaeche, serieller Anschluss und HTTP.
#
# Fremdquellen liegen ABSICHTLICH ausserhalb des Projektordners, damit hier
# ausschliesslich eigener Code liegt.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# ---------------------------------------------------------------------------
# Build-Nummer. EINZIGE Quelle -- bei jeder Aenderung hochsetzen. Erscheint im
# Kopf des Fensters ("v0.1 build N"). Eigene Zaehlung dieser Fassung; sie
# beginnt nach dem letzten Qt-Stand (Build 46), damit keine Nummer doppelt
# vorkommt. Steht auch im Dateinamen (rawnes_bN.exe).
# ---------------------------------------------------------------------------
set(BUILD_NR 53)

set(DEPS_DIR "${CMAKE_SOURCE_DIR}/../ra_nes_deps" CACHE PATH "Ordner mit miniz/ und nlohmann/")
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../ra_saturn_deps/imgui" CACHE PATH "Ordner mit imgui.cpp")
set(MINIZ_DIR "${DEPS_DIR}/miniz")

foreach(d "${IMGUI_DIR}/imgui.cpp" "${MINIZ_DIR}/miniz.c" "${DEPS_DIR}/nlohmann/json.hpp")
    if(NOT EXISTS "${d}")
        message(FATAL_ERROR "Fehlt: ${d}")
    endif()
endforeach()

# Programmsymbol, FPGA-Kerne und Intro-Bilder als Windows-Ressourcen.
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
configure_file(src/app.rc.in "${CMAKE_BINARY_DIR}/erzeugt/app.rc" @ONLY)
# Die Kerne stehen nur in der .rc -- ohne diese Angabe merkt Ninja nicht, dass
# sie sich geaendert haben, und baut die alten weiter ein.
file(GLOB RAWNES_KERNE "${ASSETS_DIR}/cores/*.RBF")
set_source_files_properties("${CMAKE_BINARY_DIR}/erzeugt/app.rc" PROPERTIES OBJECT_DEPENDS "${RAWNES_KERNE}")

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    # OpenGL2 und nicht OpenGL3: Windows ohne Grafiktreiber liefert nur
    # "OpenGL 1.1.0 / GDI Generic", dort stuerzt die OpenGL3-Anbindung ab.
    "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp"
)

# Kernschicht: ohne jeden Oberflaechen-Bezug.
set(CORE_SOURCES
    src/core/Md5.cpp
    src/core/json.cpp
    src/core/ra_cache.cpp
    src/core/ra_client.cpp
    src/core/ConditionEngine.cpp
    src/core/Plausibel.cpp
    src/core/RomIndex.cpp
    src/core/ed_protocol.cpp
    src/core/fpga_installer.cpp
    "${MINIZ_DIR}/miniz.c"
)

add_executable(rawnes WIN32
    src/main.cpp
    "${CMAKE_BINARY_DIR}/erzeugt/app.rc"
    src/app.cpp
    src/intro.cpp
    src/popup.cpp
    src/bilder.cpp
    src/i18n.cpp
    src/worker.cpp
    src/ed_serial.cpp
    src/ra_http.cpp
    src/einstellungen.cpp
    src/plattform.cpp
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(rawnes PRIVATE
    src
    src/core
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${MINIZ_DIR}"
    "${DEPS_DIR}"
)

target_compile_definitions(rawnes PRIVATE
    RAWNES_BUILD=${BUILD_NR}
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(rawnes PRIVATE
    opengl32 gdi32 user32 shell32 comdlg32 dwmapi
    setupapi        # Auflisten der seriellen Anschluesse samt USB-Kennung
    winhttp         # HTTPS zu RetroAchievements
    gdiplus ole32   # PNG dekodieren, Freischalt-Einblendung
)

if(MINGW)
    # Keine libstdc++/libgcc/libwinpthread daneben. -municode, weil der
    # Einstiegspunkt wWinMain heisst.
    # -s: Symbole entfernen, die fertige Datei wird deutlich kleiner.
    target_link_options(rawnes PRIVATE
        -municode -static -static-libgcc -static-libstdc++ -s)
endif()

set(AUSGABE_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Ablage der fertigen Programme")
set_target_properties(rawnes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${AUSGABE_DIR}"
    # Build-Nummer im Dateinamen, wie bei raw-turn: ein neuer Build kollidiert
    # nie mit einer alten Exe, die noch laeuft oder gesperrt ist.
    OUTPUT_NAME "rawnes_b${BUILD_NR}")

# ---------------------------------------------------------------------------
# Selbsttest der Kernschicht. Konsolenprogramm, kein Fenster, kein Geraet.
# Baut absichtlich NUR die core/-Dateien und den seriellen Anschluss mit.
# ---------------------------------------------------------------------------
add_executable(selbsttest
    src/selbsttest.cpp
    ${CORE_SOURCES}
)
target_include_directories(selbsttest PRIVATE src src/core "${MINIZ_DIR}" "${DEPS_DIR}")
target_compile_definitions(selbsttest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    RAWNES_ASSETS="${ASSETS_DIR}")
if(MINGW)
    target_link_options(selbsttest PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(selbsttest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${AUSGABE_DIR}")
