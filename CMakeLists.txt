cmake_minimum_required(VERSION 3.16)
project(realistic_radio CXX C RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(realistic-radio SHARED
    src/Plugin.cpp src/Logger.cpp src/Config.cpp src/StationDatabase.cpp src/ReceptionCalculator.cpp
    src/RadioManager.cpp src/AudioEngine.cpp src/StreamSource.cpp src/InputManager.cpp src/MiniaudioImpl.cpp
    src/Overlay.cpp src/OverlayImage.cpp src/D3D11Overlay.cpp src/DisplayWindow.cpp src/EmbeddedData.cpp src/MediaLibrary.cpp src/MenuInput.cpp src/SettingsMenu.cpp src/InputBlock.cpp
    ${CMAKE_BINARY_DIR}/resources.rc
    src/exports.def)
target_include_directories(realistic-radio PRIVATE src third_party sdk/include)
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
    "${RR_DATA}/ets2.json;${RR_DATA}/ats.json;${RR_DATA}/audio/whitestatic.mp3;${RR_DATA}/audio/radio_switch.mp3;${RR_DATA}/custom-stations-ets2.example.json")

# ---------------------------------------------------------------------------------------------------------------
# Truckers-RealisticRadio.dll: Realistic Radio + Trucker Chat (addons/trucker-chat) in ONE plugin. Two separate
# plugins would both hook Direct3D; here the chat draws through the radio's hook as a second layer.
# Install either realistic-radio.dll OR Truckers-RealisticRadio.dll (and no separate trucker-chat.dll).
set(TC_DIR ${CMAKE_SOURCE_DIR}/addons/trucker-chat)
option(RR_BUILD_TRUCKERS "also build Truckers-RealisticRadio.dll (work in progress)" OFF)
if(RR_BUILD_TRUCKERS AND EXISTS ${TC_DIR}/src/Plugin.cpp)
    set(TC_VERSION "0.1.1 Alpha")
    file(GLOB RR_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
    set(TC_SOURCES
        ${TC_DIR}/src/TextUtil.cpp ${TC_DIR}/src/Filter.cpp ${TC_DIR}/src/TwitchProtocol.cpp ${TC_DIR}/src/YouTubeProtocol.cpp
        ${TC_DIR}/src/Canvas.cpp ${TC_DIR}/src/Cards.cpp ${TC_DIR}/src/Plugin.cpp ${TC_DIR}/src/Common.cpp ${TC_DIR}/src/Config.cpp
        ${TC_DIR}/src/EmbeddedData.cpp ${TC_DIR}/src/Http.cpp ${TC_DIR}/src/ChatSource.cpp ${TC_DIR}/src/TwitchSource.cpp
        ${TC_DIR}/src/YouTubeSource.cpp ${TC_DIR}/src/GlyphGdi.cpp ${TC_DIR}/src/D3D11OverlayBridge.cpp ${TC_DIR}/src/ChatDisplay.cpp
        ${TC_DIR}/src/InputManager.cpp ${TC_DIR}/src/MenuWindow.cpp)
    set(TC_DATA ${TC_DIR}/data)
    configure_file(${CMAKE_SOURCE_DIR}/src/resources-combined.rc.in ${CMAKE_BINARY_DIR}/resources-combined.rc @ONLY)
    add_library(truckers-realistic-radio SHARED ${RR_SOURCES} ${TC_SOURCES} ${CMAKE_BINARY_DIR}/resources-combined.rc src/exports.def)
    target_include_directories(truckers-realistic-radio PRIVATE src third_party sdk/include)
    target_compile_definitions(truckers-realistic-radio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(truckers-realistic-radio PRIVATE RR_WITH_TRUCKER_CHAT)
    set_source_files_properties(${TC_SOURCES} PROPERTIES COMPILE_DEFINITIONS "TC_COMBINED;_WIN32_WINNT=0x0A00;TC_VERSION=\"${TC_VERSION}\"")
    target_link_libraries(truckers-realistic-radio PRIVATE winhttp mfplat mfreadwrite mfuuid ole32 winmm user32 gdi32 comctl32 d3d11 dxgi dxguid
                          urlmon windowscodecs shlwapi dinput8 psapi shell32)
    if(MSVC)
        target_compile_options(truckers-realistic-radio PRIVATE /W3 /utf-8 /EHsc)
        set_property(TARGET truckers-realistic-radio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_options(truckers-realistic-radio PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(truckers-realistic-radio PROPERTIES PREFIX "" OUTPUT_NAME "Truckers-RealisticRadio")
    set_source_files_properties(${CMAKE_BINARY_DIR}/resources-combined.rc PROPERTIES OBJECT_DEPENDS
        "${RR_DATA}/ets2.json;${RR_DATA}/ats.json;${TC_DATA}/fonts/Inter-Regular.otf;${TC_DATA}/fonts/Inter-Bold.otf;${TC_DATA}/trucker-chat.json")
endif()
