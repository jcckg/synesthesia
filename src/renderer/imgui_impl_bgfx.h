#pragma once

#include <bgfx/bgfx.h>

struct ImDrawData;

bool ImGui_Implbgfx_Init(bgfx::ViewId viewId = 255);
void ImGui_Implbgfx_Shutdown();
void ImGui_Implbgfx_NewFrame();
void ImGui_Implbgfx_RenderDrawData(ImDrawData* drawData);
void ImGui_Implbgfx_SetViewId(bgfx::ViewId viewId);
