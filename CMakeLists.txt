cmake_minimum_required(VERSION 3.16)
project(realistic_radio CXX C RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(realistic-radio SHARED
    src/Plugin.cpp src/Logger.cpp src/Config.cpp src/StationDatabase.cpp src/ReceptionCalculator.cpp
    src/RadioManager.cpp src/AudioEngine.cpp src/StreamSource.cpp src/InputManager.cpp src/MiniaudioImpl.cpp
    src/Overlay.cpp src/OverlayImage.cpp src/D3D11Overlay.cpp src/DisplayWindow.cpp src/EmbeddedData.cpp src/MediaLibrary.cpp src/MenuInput.cpp src/SettingsMenu.cpp src/InputBlock.cpp src/TunnelMap.cpp src/PluginApi.cpp
    third_party/minhook/src/buffer.c third_party/minhook/src/hook.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c
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
