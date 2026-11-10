/* PsyDoom: SDL window metadata/input only. Vulkan ICD owns VideoOut. */
#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"
static SDL_Window *owned_window;
static int available(void) { return 1; }
static int init(_THIS) {
    SDL_DisplayMode mode = { .format=SDL_PIXELFORMAT_RGBA8888, .w=1280, .h=720, .refresh_rate=60 };
    return SDL_AddBasicVideoDisplay(&mode) < 0 ? -1 : 0;
}
static int create_window(_THIS, SDL_Window *window) {
    if(owned_window || window->w!=1280 || window->h!=720)
        return SDL_SetError("PsyDoom requires one 1280x720 Vulkan output");
    owned_window=window;
    return 0;
}
static void destroy_window(_THIS, SDL_Window *window) {
    if(owned_window==window) owned_window=NULL;
}
static void quit(_THIS) { owned_window=NULL; }
static void pump(_THIS) { /* Joystick subsystem polls DS4 independently. */ }
static void free_device(SDL_VideoDevice *device) { SDL_free(device); }
static SDL_VideoDevice *create_device(int index) {
    SDL_VideoDevice *device=SDL_calloc(1,sizeof(*device));
    if(!device) { SDL_OutOfMemory(); return NULL; }
    device->VideoInit=init; device->VideoQuit=quit;
    device->CreateSDLWindow=create_window; device->DestroyWindow=destroy_window;
    device->PumpEvents=pump; device->free=free_device;
    return device;
}
VideoBootStrap PS4_bootstrap={"PS4", "PsyDoom Vulkan window",available,create_device};
