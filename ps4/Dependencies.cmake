if(NOT PSYDOOM_INCLUDE_VULKAN_RENDERER)
 message(FATAL_ERROR "PS4 requires the Vulkan renderer")
endif()
set(PSYDOOM_NATIVE_STACK "${PROJECT_SOURCE_DIR}/build/ps4-deps/native" CACHE PATH "Native Vulkan stack")
set(PSYDOOM_SDL_ARCHIVE "${OO_PS4_TOOLCHAIN}/lib/libSDL2.a" CACHE FILEPATH "PS4 SDL archive")
foreach(archive "${PSYDOOM_SDL_ARCHIVE}" "${PSYDOOM_NATIVE_STACK}/vulkan-ps4/libvulkan_ps4.a" "${PSYDOOM_NATIVE_STACK}/opengnm/libopengnm.a" "${PSYDOOM_NATIVE_STACK}/opengnm-psbc/libpsbc.orbis.a")
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Missing PS4 dependency: ${archive}")
    endif()
endforeach()
add_library(${LIBSDL_TGT_NAME} INTERFACE)
target_include_directories(${LIBSDL_TGT_NAME} INTERFACE "${OO_PS4_TOOLCHAIN}/include/SDL2" "${PSYDOOM_NATIVE_STACK}/Vulkan-Headers/include")
set(PSYDOOM_SDL_SOURCE "${PROJECT_SOURCE_DIR}/third_party_ps4/SDL-PS4" CACHE PATH "Pinned SDL internal headers")
file(READ "${PSYDOOM_SDL_SOURCE}/.source-revision" SDL_REV)
string(STRIP "${SDL_REV}" SDL_REV)
if(NOT SDL_REV STREQUAL "7774cf3e7d04448c44d569c50d1f9a4532a2efe7")
 message(FATAL_ERROR "SDL internal ABI revision mismatch")
endif()
add_library(PsyDoomSDLVideo OBJECT "${PROJECT_SOURCE_DIR}/ps4/platform/sdl_video.c" "${PROJECT_SOURCE_DIR}/ps4/platform/sdl_joystick.c")
target_include_directories(PsyDoomSDLVideo PRIVATE "${PSYDOOM_SDL_SOURCE}/include" "${PSYDOOM_SDL_SOURCE}/src")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a"
 COMMAND ${CMAKE_COMMAND} -E copy "${PSYDOOM_SDL_ARCHIVE}" "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a"
 COMMAND llvm-ar d "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a" SDL_ps4video.c.o SDL_sysjoystick.c.o
 COMMAND llvm-ar r "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a" $<TARGET_OBJECTS:PsyDoomSDLVideo>
 COMMAND_EXPAND_LISTS
 DEPENDS PsyDoomSDLVideo $<TARGET_OBJECTS:PsyDoomSDLVideo> "${PSYDOOM_SDL_ARCHIVE}")
add_custom_target(PsyDoomSDLArchive DEPENDS "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a")
add_dependencies(${GAME_TGT_NAME} PsyDoomSDLArchive)
target_link_libraries(${LIBSDL_TGT_NAME} INTERFACE "${CMAKE_BINARY_DIR}/libSDL2-psydoom.a")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/native-shaders/blit_frag.h"
 COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/ps4/compile-native-shader.py"
 "${PROJECT_SOURCE_DIR}/ps4/vulkan/native/blit.frag" "${CMAKE_BINARY_DIR}/native-shaders/blit_frag.h"
 DEPENDS "${PROJECT_SOURCE_DIR}/ps4/vulkan/native/blit.frag" "${PROJECT_SOURCE_DIR}/scripts/ps4/compile-native-shader.py")
add_custom_target(PsyDoomNativeShaders DEPENDS "${CMAKE_BINARY_DIR}/native-shaders/blit_frag.h")
add_library(PsyDoomNative OBJECT
 "${PROJECT_SOURCE_DIR}/ps4/vulkan/native/vk_ps4_image.c"
 "${PROJECT_SOURCE_DIR}/ps4/vulkan/native/vk_ps4_command.c"
 "${PROJECT_SOURCE_DIR}/ps4/vulkan/native/vk_ps4_queue.c")
add_dependencies(PsyDoomNative PsyDoomNativeShaders)
target_include_directories(PsyDoomNative PRIVATE "${CMAKE_BINARY_DIR}/native-shaders")
target_compile_definitions(PsyDoomNative PRIVATE VK_USE_PLATFORM_PS4 VK_PS4_HAVE_PSBC=1)
target_include_directories(PsyDoomNative PRIVATE
 "${PSYDOOM_NATIVE_STACK}/vulkan-ps4/include"
 "${PSYDOOM_NATIVE_STACK}/Vulkan-Headers/include"
 "${PSYDOOM_NATIVE_STACK}/opengnm/include"
 "${PSYDOOM_NATIVE_STACK}/opengnm-psbc/libpsbc"
 "${PSYDOOM_NATIVE_STACK}/opengnm-psbc/include"
 "${OO_PS4_TOOLCHAIN}/include/orbis")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a"
 COMMAND ${CMAKE_COMMAND} -E copy "${PSYDOOM_NATIVE_STACK}/vulkan-ps4/libvulkan_ps4.a" "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a"
 COMMAND llvm-ar d "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a" vk_ps4_command.orbis.o vk_ps4_queue.orbis.o vk_ps4_image.orbis.o
 COMMAND llvm-ar r "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a" $<TARGET_OBJECTS:PsyDoomNative>
 COMMAND_EXPAND_LISTS
 DEPENDS PsyDoomNative $<TARGET_OBJECTS:PsyDoomNative> "${PSYDOOM_NATIVE_STACK}/vulkan-ps4/libvulkan_ps4.a")
add_custom_target(PsyDoomNativeArchive DEPENDS "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a")
add_dependencies(${GAME_TGT_NAME} PsyDoomNativeArchive)
target_link_libraries(${GAME_TGT_NAME}
    "${CMAKE_BINARY_DIR}/libvulkan-psydoom.a"
    "${PSYDOOM_NATIVE_STACK}/opengnm/libopengnm.a"
    "${PSYDOOM_NATIVE_STACK}/opengnm-psbc/libpsbc.orbis.a"
    SceNet SceGnmDriver SceVideoOut SceUserService SceAudioOut ScePad SceSysmodule
    c++ c++abi unwind c kernel)

target_sources(${GAME_TGT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/ps4/platform/startup.c" "${PROJECT_SOURCE_DIR}/ps4/platform/clock.c" "${PROJECT_SOURCE_DIR}/ps4/platform/disc_search.cpp" "${PROJECT_SOURCE_DIR}/ps4/platform/capabilities.cpp")
target_link_options(${GAME_TGT_NAME} PRIVATE -Wl,--wrap=main -Wl,--wrap=_exit -Wl,--wrap=clock_gettime
 -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
target_link_libraries(${GAME_TGT_NAME} SceSystemService)
set_property(TARGET ${GAME_TGT_NAME} APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/ps4/link.x")

target_include_directories(${GAME_TGT_NAME} BEFORE PRIVATE "${PROJECT_SOURCE_DIR}/vulkan_shaders/compiled_ps4")
