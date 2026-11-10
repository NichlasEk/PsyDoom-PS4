//----------------------------------------------------------------------------------------------------------------------
// Push constants used by most shaders.
// Depending on the shaders involved, some of these attributes may be ignored.
//
// Meaning:
//  mvpMatrix       The combined model/view/projection matrix for transforming vertices into clip space.
//  ndcToPsxScale   A scaling value to scale from normalized device coordinates to the original PSX framebuffer
//                  coordinate system of 256x240.
//  psxNdcOffset    Gives the offset in NDC space where 0,0 in the original PSX framebuffer coordinate system starts.
//                  Together with 'ndcToPsxScale' this value can be used to transform vertices into the coordinate
//                  system of the original PSX framebuffer, which is useful for things like sky rendering.
//  invDrawRes      1 / drawResolution - useful for converting from 'gl_FragCoord' to NDC coordinates
//----------------------------------------------------------------------------------------------------------------------
#ifdef PSYDOOM_PS4_UBO
#define UNIFORM_LAYOUT layout(std140, set = 0, binding = 8)
#else
#define UNIFORM_LAYOUT layout(push_constant)
#endif
#define DECLARE_UNIFORMS()\
    UNIFORM_LAYOUT uniform Uniforms {   \
        mat4    mvpMatrix;      \
        vec2    ndcToPsxScale;  \
        vec2    psxNdcOffset;   \
        vec2    invDrawRes;     \
    } uniforms;
