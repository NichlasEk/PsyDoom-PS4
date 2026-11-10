#pragma once
// PS4 stub for GL/glew.h (OpenGL/GLEW not available on PS4)

#define GLEW_OK 0
#define GLEW_VERSION_3_3 1
#define GLEW_VERSION_4_6 1

static inline int glewInit(void) { return 0; }
