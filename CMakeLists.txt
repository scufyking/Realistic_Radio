cmake_minimum_required(VERSION 3.16)
project(realistic_radio CXX C RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(realistic-radio SHARED
    src/Plugin.cpp src/Logger.cpp src/Config.cpp src/StationDatabase.cpp src/ReceptionCalculator.cpp
    src/RadioManager.cpp src/AudioEngine.cpp src/StreamSource.cpp src/InputManager.cpp src/MiniaudioImpl.cpp
    src/Overlay.cpp src/OverlayImage.cpp src/D3D11Overlay.cpp src/DisplayWindow.cpp src/EmbeddedData.cpp src/MediaLibrary.cpp src/MenuInput.cpp src/SettingsMenu.cpp src/InputBlock.cpp src/TunnelMap.cpp src/PluginApi.cpp
    src/GameAudio.cpp src/CardStyles.cpp src/Phone.cpp src/Speech.cpp src/ForeignKeys.cpp src/L10n.cpp src/ExternalMedia.cpp src/Addons.cpp third_party/minhook/src/buffer.c third_party/minhook/src/hook.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c
    ${CMAKE_BINARY_DIR}/resources.rc
    src/exports.def)
target_include_directories(realistic-radio PRIVATE src third_party third_party/minhook/include sdk/include)
target_compile_definitions(realistic-radio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(realistic-radio PRIVATE winhttp mfplat mfreadwrite mfuuid ole32 winmm user32 gdi32 d3d11 dxgi dxguid urlmon windowscodecs shlwapi dinput8 psapi shell32)
if(MSVC)
    target_compile_options(realistic-radio PRIVATE /W3 /utf-8 /EHsc)
    set_property(TARGET realistic-radio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # no VC++ redist needed
else()
    target_link_options(realistic-radio PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(realistic-radio PROPERTIES PREFIX "" OUTPUT_NAME "realistic-radio")

# Station data and sounds are embedded into the DLL (src/resources.rc.in) and unpacked by the plugin itself.
set(RR_DATA ${CMAKE_SOURCE_DIR}/data)
configure_file(${CMAKE_SOURCE_DIR}/src/resources.rc.in ${CMAKE_BINARY_DIR}/resources.rc @ONLY)
set_source_files_properties(${CMAKE_BINARY_DIR}/resources.rc PROPERTIES OBJECT_DEPENDS
    "${RR_DATA}/ets2.json;${RR_DATA}/ats.json;${RR_DATA}/audio/whitestatic.mp3;${RR_DATA}/audio/radio_switch.mp3;${RR_DATA}/custom-stations-ets2.example.json;${RR_DATA}/tunnels/tunnels-ets2.json;${RR_DATA}/tunnels/tunnels-ets2-promods.json;${RR_DATA}/truck-logos/daf/daf.png;${RR_DATA}/truck-logos/iveco/iveco.png;${RR_DATA}/truck-logos/man/man.png;${RR_DATA}/truck-logos/mercedes/blau.png;${RR_DATA}/truck-logos/mercedes/gold.png;${RR_DATA}/truck-logos/mercedes/gruen.png;${RR_DATA}/truck-logos/mercedes/kupfer.png;${RR_DATA}/truck-logos/mercedes/lila.png;${RR_DATA}/truck-logos/mercedes/perlmutt.png;${RR_DATA}/truck-logos/mercedes/rosa.png;${RR_DATA}/truck-logos/mercedes/silber.png;${RR_DATA}/truck-logos/scania/scania.png;${RR_DATA}/truck-logos/volvo/volvo.png")

# Add-on "Realistic Radio Web-App" (realistic-radio-webapp.dll): a web page (Spotify by default) as an app on the phone,
# shown in Microsoft Edge WebView2. Separate DLL in the plugins folder; talks to realistic-radio.dll through rr_app_*.
add_library(realistic-radio-webapp SHARED addons/webapp/WebApp.cpp addons/webapp/webview2/WebView2Loader.cpp addons/webapp/resources.rc addons/webapp/exports.def)
target_include_directories(realistic-radio-webapp PRIVATE addons/webapp addons/webapp/webview2 third_party sdk/include)
target_compile_definitions(realistic-radio-webapp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
target_link_libraries(realistic-radio-webapp PRIVATE windowscodecs ole32 oleaut32 psapi user32 gdi32 advapi32 shell32 version)
if(MSVC)
    target_compile_options(realistic-radio-webapp PRIVATE /W3 /utf-8 /EHsc)
    set_property(TARGET realistic-radio-webapp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(realistic-radio-webapp PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(realistic-radio-webapp PROPERTIES PREFIX "" OUTPUT_NAME "realistic-radio-webapp")

# Add-on "Realistic Radio Jobs+" (realistic-radio-jobs.dll): own jobs from the phone (single player) - the job is put into
# the quick save (F5) and is in the freight market after quick load (F9). Uses the add-on interface (sdk/addon/rr_addon.h).
add_library(realistic-radio-jobs SHARED addons/jobs/JobsApp.cpp addons/jobs/SaveGame.cpp addons/jobs/aes/aes.c addons/jobs/miniz/miniz_tinfl.c
            addons/jobs/resources.rc addons/jobs/exports.def)
target_include_directories(realistic-radio-jobs PRIVATE addons/jobs third_party sdk/include)
target_compile_definitions(realistic-radio-jobs PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(realistic-radio-jobs PRIVATE ole32 shell32 advapi32 psapi)
if(MSVC)
    target_compile_options(realistic-radio-jobs PRIVATE /W3 /utf-8 /EHsc)
    set_property(TARGET realistic-radio-jobs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(realistic-radio-jobs PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(realistic-radio-jobs PROPERTIES PREFIX "" OUTPUT_NAME "realistic-radio-jobs")

# Example add-on for add-on authors (sdk/addon/example): a small "list" app
add_library(realistic-radio-hello SHARED sdk/addon/example/HelloApp.cpp addons/jobs/exports.def)
target_include_directories(realistic-radio-hello PRIVATE sdk/addon sdk/include)
if(NOT MSVC)
    target_link_options(realistic-radio-hello PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(realistic-radio-hello PROPERTIES PREFIX "" OUTPUT_NAME "realistic-radio-hello")
