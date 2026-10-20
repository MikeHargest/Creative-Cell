#pragma once

// Minimal, self-contained loader for the handful of OpenGL 3.3 core functions
// our own renderer needs. Deliberately independent from Dear ImGui's bundled
// GL loader (used internally by imgui_impl_opengl3.cpp) — pointers live in
// the glx:: namespace so the two loaders can never collide at link time.

// GL/gl.h on Windows expects WINGDIAPI/APIENTRY from windows.h to already be
// defined; pull it in first (and trimmed) rather than relying on include order
// at every call site.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <cstddef>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_MULTISAMPLE 0x809D

// SSAO, shadow maps and HDRI (sculpt_renderer.cpp) need offscreen buffers
// and float textures — these enums post-date OpenGL 1.1 (what GL/gl.h above declares),
// so they're added by hand the same way the buffer/shader enums above are.
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RGB16F 0x881B
#define GL_R8 0x8229
#define GL_RED 0x1903
#define GL_RGBA16F 0x881A
#define GL_RGBA32F 0x8814
#define GL_R16F 0x822D
#ifndef GL_RGB16F
#define GL_RGB16F 0x881B
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#define GL_TEXTURE_MAX_LEVEL 0x813D
// 3D textures (the field renderer's brick atlas) and integer textures.
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#define GL_R8_SNORM 0x8F94
#define GL_R16_SNORM 0x8F98
#define GL_R32I 0x8235
#define GL_RED_INTEGER 0x8D94

namespace glx {

typedef void (APIENTRY* PFNGENBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNBINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY* PFNBUFFERDATA)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* PFNDELETEBUFFERS)(GLsizei, const GLuint*);

typedef void (APIENTRY* PFNGENVERTEXARRAYS)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNBINDVERTEXARRAY)(GLuint);
typedef void (APIENTRY* PFNDELETEVERTEXARRAYS)(GLsizei, const GLuint*);

typedef void (APIENTRY* PFNVERTEXATTRIBPOINTER)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRY* PFNENABLEVERTEXATTRIBARRAY)(GLuint);

typedef GLuint (APIENTRY* PFNCREATESHADER)(GLenum);
typedef void (APIENTRY* PFNSHADERSOURCE)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (APIENTRY* PFNCOMPILESHADER)(GLuint);
typedef void (APIENTRY* PFNGETSHADERIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* PFNGETSHADERINFOLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY* PFNDELETESHADER)(GLuint);

typedef GLuint (APIENTRY* PFNCREATEPROGRAM)();
typedef void (APIENTRY* PFNATTACHSHADER)(GLuint, GLuint);
typedef void (APIENTRY* PFNLINKPROGRAM)(GLuint);
typedef void (APIENTRY* PFNGETPROGRAMIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* PFNGETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (APIENTRY* PFNDELETEPROGRAM)(GLuint);
typedef void (APIENTRY* PFNUSEPROGRAM)(GLuint);

typedef GLint (APIENTRY* PFNGETUNIFORMLOCATION)(GLuint, const GLchar*);
typedef void (APIENTRY* PFNUNIFORMMATRIX4FV)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY* PFNUNIFORMMATRIX3FV)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY* PFNUNIFORM4FV)(GLint, GLsizei, const GLfloat*);
typedef void (APIENTRY* PFNUNIFORM3FV)(GLint, GLsizei, const GLfloat*);
typedef void (APIENTRY* PFNUNIFORM3IV)(GLint, GLsizei, const GLint*);
typedef void (APIENTRY* PFNUNIFORM2FV)(GLint, GLsizei, const GLfloat*);
typedef void (APIENTRY* PFNUNIFORM1I)(GLint, GLint);
typedef void (APIENTRY* PFNUNIFORM1F)(GLint, GLfloat);

typedef void (APIENTRY* PFNGENFRAMEBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNBINDFRAMEBUFFER)(GLenum, GLuint);
typedef void (APIENTRY* PFNBLITFRAMEBUFFER)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (APIENTRY* PFNFRAMEBUFFERTEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY* PFNDELETEFRAMEBUFFERS)(GLsizei, const GLuint*);
typedef GLenum (APIENTRY* PFNCHECKFRAMEBUFFERSTATUS)(GLenum);
typedef void (APIENTRY* PFNDRAWBUFFERS)(GLsizei, const GLenum*);
typedef void (APIENTRY* PFNBLENDFUNCSEPARATE)(GLenum, GLenum, GLenum, GLenum);

typedef void (APIENTRY* PFNGENRENDERBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNBINDRENDERBUFFER)(GLenum, GLuint);
typedef void (APIENTRY* PFNRENDERBUFFERSTORAGE)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY* PFNFRAMEBUFFERRENDERBUFFER)(GLenum, GLenum, GLenum, GLuint);
typedef void (APIENTRY* PFNDELETERENDERBUFFERS)(GLsizei, const GLuint*);
typedef void (APIENTRY* PFNRENDERBUFFERSTORAGEMULTISAMPLE)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);

typedef void (APIENTRY* PFNACTIVETEXTURE)(GLenum);
typedef void (APIENTRY* PFNTEXIMAGE3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                       const void*);
typedef void (APIENTRY* PFNTEXSUBIMAGE3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum,
                                          GLenum, const void*);
typedef void (APIENTRY* PFNGENERATEMIPMAP)(GLenum);

extern PFNGENBUFFERS GenBuffers;
extern PFNBINDBUFFER BindBuffer;
extern PFNBUFFERDATA BufferData;
extern PFNDELETEBUFFERS DeleteBuffers;

extern PFNGENVERTEXARRAYS GenVertexArrays;
extern PFNBINDVERTEXARRAY BindVertexArray;
extern PFNDELETEVERTEXARRAYS DeleteVertexArrays;

extern PFNVERTEXATTRIBPOINTER VertexAttribPointer;
extern PFNENABLEVERTEXATTRIBARRAY EnableVertexAttribArray;

extern PFNCREATESHADER CreateShader;
extern PFNSHADERSOURCE ShaderSource;
extern PFNCOMPILESHADER CompileShader;
extern PFNGETSHADERIV GetShaderiv;
extern PFNGETSHADERINFOLOG GetShaderInfoLog;
extern PFNDELETESHADER DeleteShader;

extern PFNCREATEPROGRAM CreateProgram;
extern PFNATTACHSHADER AttachShader;
extern PFNLINKPROGRAM LinkProgram;
extern PFNGETPROGRAMIV GetProgramiv;
extern PFNGETPROGRAMINFOLOG GetProgramInfoLog;
extern PFNDELETEPROGRAM DeleteProgram;
extern PFNUSEPROGRAM UseProgram;

extern PFNGETUNIFORMLOCATION GetUniformLocation;
extern PFNUNIFORMMATRIX4FV UniformMatrix4fv;
extern PFNUNIFORMMATRIX3FV UniformMatrix3fv;
extern PFNUNIFORM4FV Uniform4fv;
extern PFNUNIFORM3FV Uniform3fv;
extern PFNUNIFORM3IV Uniform3iv;
extern PFNUNIFORM2FV Uniform2fv;
extern PFNUNIFORM1I Uniform1i;
extern PFNUNIFORM1F Uniform1f;

extern PFNGENFRAMEBUFFERS GenFramebuffers;
extern PFNBINDFRAMEBUFFER BindFramebuffer;
extern PFNBLITFRAMEBUFFER BlitFramebuffer;
extern PFNFRAMEBUFFERTEXTURE2D FramebufferTexture2D;
extern PFNDELETEFRAMEBUFFERS DeleteFramebuffers;
extern PFNCHECKFRAMEBUFFERSTATUS CheckFramebufferStatus;
extern PFNDRAWBUFFERS DrawBuffers;
extern PFNBLENDFUNCSEPARATE BlendFuncSeparate;

extern PFNGENRENDERBUFFERS GenRenderbuffers;
extern PFNBINDRENDERBUFFER BindRenderbuffer;
extern PFNRENDERBUFFERSTORAGE RenderbufferStorage;
extern PFNFRAMEBUFFERRENDERBUFFER FramebufferRenderbuffer;
extern PFNDELETERENDERBUFFERS DeleteRenderbuffers;
extern PFNRENDERBUFFERSTORAGEMULTISAMPLE RenderbufferStorageMultisample;
#define GL_MAX_SAMPLES 0x8D57
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#define GL_SRGB8_ALPHA8 0x8C43

extern PFNACTIVETEXTURE ActiveTexture;
extern PFNTEXIMAGE3D TexImage3D;
extern PFNTEXSUBIMAGE3D TexSubImage3D;
extern PFNGENERATEMIPMAP GenerateMipmap;

// Optional (GL 4.3 / ARB_copy_image, which most drivers offer in a 3.3
// context too): null when missing, init() doesn't fail over it.
typedef void (APIENTRY* PFNCOPYIMAGESUBDATA)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint,
                                             GLint, GLint, GLsizei, GLsizei, GLsizei);
extern PFNCOPYIMAGESUBDATA CopyImageSubData;
// Optional, GL 4.3 compute (the shared brick pool's uploads): all null
// when the context is older.
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_STREAM_DRAW 0x88E0
#define GL_WRITE_ONLY 0x88B9
#define GL_ALL_BARRIER_BITS 0xFFFFFFFF
typedef void (APIENTRY* PFNDISPATCHCOMPUTE)(GLuint, GLuint, GLuint);
typedef void (APIENTRY* PFNBINDIMAGETEXTURE)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
typedef void (APIENTRY* PFNMEMORYBARRIER)(GLbitfield);
typedef void (APIENTRY* PFNBINDBUFFERBASE)(GLenum, GLuint, GLuint);
extern PFNDISPATCHCOMPUTE DispatchCompute;
extern PFNBINDIMAGETEXTURE BindImageTexture;
extern PFNMEMORYBARRIER Barrier;
extern PFNBINDBUFFERBASE BindBufferBase;
#define GL_MAP_WRITE_BIT 0x0002
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
typedef void* (APIENTRY* PFNMAPBUFFERRANGE)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (APIENTRY* PFNUNMAPBUFFER)(GLenum);
extern PFNMAPBUFFERRANGE MapBufferRange;
extern PFNUNMAPBUFFER UnmapBuffer;
// Compute dispatched with its size from a buffer (GpuBrush's growing front).
#define GL_DISPATCH_INDIRECT_BUFFER 0x90EE
#define GL_COMMAND_BARRIER_BIT 0x00000040
typedef void (APIENTRY* PFNDISPATCHCOMPUTEINDIRECT)(GLintptr);
extern PFNDISPATCHCOMPUTEINDIRECT DispatchComputeIndirect;
// GPU time stamps (measuring, optional).
#define GL_TIMESTAMP 0x8E28
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
typedef void (APIENTRY* PFNGENQUERIES)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNDELETEQUERIES)(GLsizei, const GLuint*);
typedef void (APIENTRY* PFNQUERYCOUNTER)(GLuint, GLenum);
typedef void (APIENTRY* PFNGETQUERYOBJECTUI64V)(GLuint, GLenum, unsigned long long*);
extern PFNGENQUERIES GenQueries;
extern PFNDELETEQUERIES DeleteQueries;
extern PFNQUERYCOUNTER QueryCounter;
extern PFNGETQUERYOBJECTUI64V GetQueryObjectui64v;
// Buffer to buffer on the GPU (GpuBrush's pool growing).
#define GL_COPY_READ_BUFFER 0x8F36
#define GL_COPY_WRITE_BUFFER 0x8F37
typedef void (APIENTRY* PFNCOPYBUFFERSUBDATA)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
extern PFNCOPYBUFFERSUBDATA CopyBufferSubData;
typedef void (APIENTRY* PFNBUFFERSUBDATA)(GLenum, GLintptr, GLsizeiptr, const void*);
extern PFNBUFFERSUBDATA BufferSubData;
// Fences: when the GPU is done with what was asked so far.
typedef struct __GLsync* GLsync;
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_WAIT_FAILED 0x911D
typedef GLsync (APIENTRY* PFNFENCESYNC)(GLenum, GLbitfield);
typedef GLenum (APIENTRY* PFNCLIENTWAITSYNC)(GLsync, GLbitfield, unsigned long long);
typedef void (APIENTRY* PFNDELETESYNC)(GLsync);
extern PFNFENCESYNC FenceSync;
extern PFNCLIENTWAITSYNC ClientWaitSync;
extern PFNDELETESYNC DeleteSync;
// A 4.3 context with all of the above.
bool hasCompute();

// Loads every pointer above via glfwGetProcAddress. Call once, after
// glfwMakeContextCurrent(). Returns false if any function failed to load.
bool init();

}  // namespace glx
