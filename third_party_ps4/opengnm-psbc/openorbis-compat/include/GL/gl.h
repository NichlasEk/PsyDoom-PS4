#pragma once
// PS4 stub for GL/gl.h (OpenGL not available on PS4)

#include <stdint.h>

/* GL API calling-convention macros (needed by glext.h and GLES headers) */
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#ifndef GLAPIENTRYP
#define GLAPIENTRYP GLAPIENTRY *
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef intptr_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef void GLvoid;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef signed char GLbyte;
typedef unsigned char GLubyte;
typedef short GLshort;
typedef unsigned short GLushort;
typedef double GLdouble;
typedef float GLclampf;
typedef double GLclampd;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync* GLsync;

#define GL_NO_ERROR 0
#define GL_OUT_OF_MEMORY 0x0905
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_UNSIGNALED 0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117

/* GL type enums used by glsl_types */
#define GL_DOUBLE     0x140A
#define GL_FLOAT      0x1406
#define GL_INT        0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_BOOL       0x8B56
#define GL_BYTE       0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT      0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_HALF_FLOAT 0x140B
#define GL_FIXED      0x140C
#define GL_INT64_ARB  0x140D
#define GL_UNSIGNED_INT64_ARB 0x140E

static inline GLenum glGetError(void) { return GL_NO_ERROR; }
static inline void glInsertEventMarkerEXT(GLsizei length, const GLchar* marker) {}
static inline GLsync glFenceSync(GLenum condition, GLbitfield flags) { return NULL; }
static inline void glDeleteSync(GLsync sync) {}
static inline GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) { return GL_ALREADY_SIGNALED; }
static inline void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {}
