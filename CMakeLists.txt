cmake_minimum_required(VERSION 3.16)
project(local_radio_native CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(local-radio-native SHARED
    src/Plugin.cpp src/Logger.cpp src/Config.cpp src/StationDatabase.cpp src/ReceptionCalculator.cpp
    src/RadioManager.cpp src/AudioEngine.cpp src/StreamSource.cpp src/InputManager.cpp src/MiniaudioImpl.cpp
    src/Overlay.cpp src/OverlayImage.cpp src/D3D11Overlay.cpp src/DisplayWindow.cpp
    src/exports.def)
target_include_directories(local-radio-native PRIVATE src third_party sdk/include)
target_compile_definitions(local-radio-native PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(local-radio-native PRIVATE winhttp mfplat mfreadwrite mfuuid ole32 winmm user32 gdi32 d3d11 dxgi dxguid urlmon windowscodecs)
if(MSVC)
    target_compile_options(local-radio-native PRIVATE /W3 /utf-8 /EHsc)
    set_property(TARGET local-radio-native PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # no VC++ redist needed
else()
    target_link_options(local-radio-native PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(local-radio-native PROPERTIES PREFIX "" OUTPUT_NAME "local-radio-native")

# Copy runtime data next to the DLL: <out>/local-radio/{ets2.json, ats.json, audio/*}
add_custom_command(TARGET local-radio-native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:local-radio-native>/local-radio)
