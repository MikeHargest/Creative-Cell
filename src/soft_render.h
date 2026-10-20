#pragma once

#include <cstdint>
#include <vector>

#include "model_loader.h"

// The library's thumbnail without a GPU: the same 3/4 view, fit and clay
// light as Renderer::thumbnail, drawn on the CPU (Explorer's thumbnail
// handler runs in a process where OpenGL can't be counted on). `size` px
// square, RGBA with straight alpha (transparent background), rows top to
// bottom; drawn at twice the size and averaged down for smooth edges.
bool renderThumbnailCpu(const RenderMesh& mesh, int size, std::vector<uint8_t>& rgba);
