#include "gl_loader.h"

#include <GLFW/glfw3.h>

namespace glx {

PFNGENBUFFERS GenBuffers = nullptr;
PFNBINDBUFFER BindBuffer = nullptr;
PFNBUFFERDATA BufferData = nullptr;
PFNDELETEBUFFERS DeleteBuffers = nullptr;

PFNGENVERTEXARRAYS GenVertexArrays = nullptr;
PFNBINDVERTEXARRAY BindVertexArray = nullptr;
PFNDELETEVERTEXARRAYS DeleteVertexArrays = nullptr;

PFNVERTEXATTRIBPOINTER VertexAttribPointer = nullptr;
PFNENABLEVERTEXATTRIBARRAY EnableVertexAttribArray = nullptr;

PFNCREATESHADER CreateShader = nullptr;
PFNSHADERSOURCE ShaderSource = nullptr;
PFNCOMPILESHADER CompileShader = nullptr;
PFNGETSHADERIV GetShaderiv = nullptr;
PFNGETSHADERINFOLOG GetShaderInfoLog = nullptr;
PFNDELETESHADER DeleteShader = nullptr;

PFNCREATEPROGRAM CreateProgram = nullptr;
PFNATTACHSHADER AttachShader = nullptr;
PFNLINKPROGRAM LinkProgram = nullptr;
PFNGETPROGRAMIV GetProgramiv = nullptr;
PFNGETPROGRAMINFOLOG GetProgramInfoLog = nullptr;
PFNDELETEPROGRAM DeleteProgram = nullptr;
PFNUSEPROGRAM UseProgram = nullptr;

PFNGETUNIFORMLOCATION GetUniformLocation = nullptr;
PFNUNIFORMMATRIX4FV UniformMatrix4fv = nullptr;
PFNUNIFORMMATRIX3FV UniformMatrix3fv = nullptr;
PFNUNIFORM4FV Uniform4fv = nullptr;
PFNUNIFORM3FV Uniform3fv = nullptr;
PFNUNIFORM3IV Uniform3iv = nullptr;
PFNUNIFORM2FV Uniform2fv = nullptr;
PFNUNIFORM1I Uniform1i = nullptr;
PFNUNIFORM1F Uniform1f = nullptr;

PFNGENFRAMEBUFFERS GenFramebuffers = nullptr;
PFNBINDFRAMEBUFFER BindFramebuffer = nullptr;
PFNBLITFRAMEBUFFER BlitFramebuffer = nullptr;
PFNFRAMEBUFFERTEXTURE2D FramebufferTexture2D = nullptr;
PFNDELETEFRAMEBUFFERS DeleteFramebuffers = nullptr;
PFNCHECKFRAMEBUFFERSTATUS CheckFramebufferStatus = nullptr;
PFNDRAWBUFFERS DrawBuffers = nullptr;
PFNBLENDFUNCSEPARATE BlendFuncSeparate = nullptr;

PFNGENRENDERBUFFERS GenRenderbuffers = nullptr;
PFNBINDRENDERBUFFER BindRenderbuffer = nullptr;
PFNRENDERBUFFERSTORAGE RenderbufferStorage = nullptr;
PFNFRAMEBUFFERRENDERBUFFER FramebufferRenderbuffer = nullptr;
PFNDELETERENDERBUFFERS DeleteRenderbuffers = nullptr;
PFNRENDERBUFFERSTORAGEMULTISAMPLE RenderbufferStorageMultisample = nullptr;

PFNACTIVETEXTURE ActiveTexture = nullptr;
PFNTEXIMAGE3D TexImage3D = nullptr;
PFNTEXSUBIMAGE3D TexSubImage3D = nullptr;
PFNCOPYIMAGESUBDATA CopyImageSubData = nullptr;
PFNDISPATCHCOMPUTE DispatchCompute = nullptr;
PFNBINDIMAGETEXTURE BindImageTexture = nullptr;
PFNMEMORYBARRIER Barrier = nullptr;
PFNBINDBUFFERBASE BindBufferBase = nullptr;
PFNMAPBUFFERRANGE MapBufferRange = nullptr;
PFNUNMAPBUFFER UnmapBuffer = nullptr;
PFNDISPATCHCOMPUTEINDIRECT DispatchComputeIndirect = nullptr;
PFNGENQUERIES GenQueries = nullptr;
PFNDELETEQUERIES DeleteQueries = nullptr;
PFNQUERYCOUNTER QueryCounter = nullptr;
PFNGETQUERYOBJECTUI64V GetQueryObjectui64v = nullptr;
PFNCOPYBUFFERSUBDATA CopyBufferSubData = nullptr;
PFNBUFFERSUBDATA BufferSubData = nullptr;
PFNFENCESYNC FenceSync = nullptr;
PFNCLIENTWAITSYNC ClientWaitSync = nullptr;
PFNDELETESYNC DeleteSync = nullptr;
PFNGENERATEMIPMAP GenerateMipmap = nullptr;

namespace {
template <typename T>
bool load(T& fn, const char* name) {
  fn = reinterpret_cast<T>(glfwGetProcAddress(name));
  return fn != nullptr;
}
}  // namespace

bool init() {
  bool ok = true;
  ok &= load(GenBuffers, "glGenBuffers");
  ok &= load(BindBuffer, "glBindBuffer");
  ok &= load(BufferData, "glBufferData");
  ok &= load(DeleteBuffers, "glDeleteBuffers");

  ok &= load(GenVertexArrays, "glGenVertexArrays");
  ok &= load(BindVertexArray, "glBindVertexArray");
  ok &= load(DeleteVertexArrays, "glDeleteVertexArrays");

  ok &= load(VertexAttribPointer, "glVertexAttribPointer");
  ok &= load(EnableVertexAttribArray, "glEnableVertexAttribArray");

  ok &= load(CreateShader, "glCreateShader");
  ok &= load(ShaderSource, "glShaderSource");
  ok &= load(CompileShader, "glCompileShader");
  ok &= load(GetShaderiv, "glGetShaderiv");
  ok &= load(GetShaderInfoLog, "glGetShaderInfoLog");
  ok &= load(DeleteShader, "glDeleteShader");

  ok &= load(CreateProgram, "glCreateProgram");
  ok &= load(AttachShader, "glAttachShader");
  ok &= load(LinkProgram, "glLinkProgram");
  ok &= load(GetProgramiv, "glGetProgramiv");
  ok &= load(GetProgramInfoLog, "glGetProgramInfoLog");
  ok &= load(DeleteProgram, "glDeleteProgram");
  ok &= load(UseProgram, "glUseProgram");

  ok &= load(GetUniformLocation, "glGetUniformLocation");
  ok &= load(UniformMatrix4fv, "glUniformMatrix4fv");
  ok &= load(UniformMatrix3fv, "glUniformMatrix3fv");
  ok &= load(Uniform4fv, "glUniform4fv");
  ok &= load(Uniform3fv, "glUniform3fv");
  ok &= load(Uniform3iv, "glUniform3iv");
  ok &= load(Uniform2fv, "glUniform2fv");
  ok &= load(Uniform1i, "glUniform1i");
  ok &= load(Uniform1f, "glUniform1f");

  ok &= load(GenFramebuffers, "glGenFramebuffers");
  ok &= load(BindFramebuffer, "glBindFramebuffer");
  ok &= load(BlitFramebuffer, "glBlitFramebuffer");
  ok &= load(FramebufferTexture2D, "glFramebufferTexture2D");
  ok &= load(DeleteFramebuffers, "glDeleteFramebuffers");
  ok &= load(CheckFramebufferStatus, "glCheckFramebufferStatus");
  ok &= load(DrawBuffers, "glDrawBuffers");
  ok &= load(BlendFuncSeparate, "glBlendFuncSeparate");

  ok &= load(GenRenderbuffers, "glGenRenderbuffers");
  ok &= load(BindRenderbuffer, "glBindRenderbuffer");
  ok &= load(RenderbufferStorage, "glRenderbufferStorage");
  ok &= load(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
  ok &= load(DeleteRenderbuffers, "glDeleteRenderbuffers");
  ok &= load(RenderbufferStorageMultisample, "glRenderbufferStorageMultisample");

  ok &= load(ActiveTexture, "glActiveTexture");
  ok &= load(TexImage3D, "glTexImage3D");
  ok &= load(TexSubImage3D, "glTexSubImage3D");
  ok &= load(GenerateMipmap, "glGenerateMipmap");

  load(CopyImageSubData, "glCopyImageSubData");  // optional
  load(DispatchCompute, "glDispatchCompute");
  load(BindImageTexture, "glBindImageTexture");
  load(Barrier, "glMemoryBarrier");
  load(BindBufferBase, "glBindBufferBase");
  load(MapBufferRange, "glMapBufferRange");
  load(UnmapBuffer, "glUnmapBuffer");
  load(DispatchComputeIndirect, "glDispatchComputeIndirect");
  load(GenQueries, "glGenQueries");
  load(DeleteQueries, "glDeleteQueries");
  load(QueryCounter, "glQueryCounter");
  load(GetQueryObjectui64v, "glGetQueryObjectui64v");
  load(CopyBufferSubData, "glCopyBufferSubData");
  load(BufferSubData, "glBufferSubData");
  load(FenceSync, "glFenceSync");
  load(ClientWaitSync, "glClientWaitSync");
  load(DeleteSync, "glDeleteSync");
  return ok;
}

bool hasCompute() {
  GLint major = 0, minor = 0;
  glGetIntegerv(0x821B /* MAJOR_VERSION */, &major);
  glGetIntegerv(0x821C /* MINOR_VERSION */, &minor);
  return (major > 4 || (major == 4 && minor >= 3)) && DispatchCompute && BindImageTexture && Barrier &&
         BindBufferBase && CopyImageSubData && MapBufferRange && UnmapBuffer && DispatchComputeIndirect && CopyBufferSubData && BufferSubData && FenceSync &&
         ClientWaitSync && DeleteSync;
}

}  // namespace glx
