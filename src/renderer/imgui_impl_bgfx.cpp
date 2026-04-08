#include "renderer/imgui_impl_bgfx.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>

#include <bgfx/embedded_shader.h>
#include <bx/bx.h>
#include <bx/math.h>

#include "imgui.h"

#include "../../vendor/bgfx/examples/common/imgui/fs_ocornut_imgui.bin.h"
#include "../../vendor/bgfx/examples/common/imgui/vs_ocornut_imgui.bin.h"

namespace {

struct BackendData {
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::ViewId viewId = 255;
};

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

void logTransientShortage(uint32_t requiredVertices, uint32_t availableVertices, uint32_t requiredIndices, uint32_t availableIndices) {
    static int frameCounter = 0;
    if ((frameCounter++ % 120) == 0) {
        std::fprintf(
            stderr,
            "[imgui/bgfx] transient buffer pressure: vb %u/%u, ib %u/%u\n",
            availableVertices,
            requiredVertices,
            availableIndices,
            requiredIndices
        );
    }
}

BackendData* getBackendData() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return nullptr;
    }

    return static_cast<BackendData*>(ImGui::GetIO().BackendRendererUserData);
}

bgfx::TextureHandle textureHandleFromId(ImTextureID id) {
    if (id == ImTextureID_Invalid) {
        return BGFX_INVALID_HANDLE;
    }

    const uint64_t raw = static_cast<uint64_t>(id);
    if (raw > static_cast<uint64_t>(std::numeric_limits<uint16_t>::max())) {
        return BGFX_INVALID_HANDLE;
    }

    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    handle.idx = static_cast<uint16_t>(raw);
    return handle;
}

ImTextureID textureIdFromHandle(bgfx::TextureHandle handle) {
    if (!bgfx::isValid(handle)) {
        return ImTextureID_Invalid;
    }

    return static_cast<ImTextureID>(handle.idx);
}

void destroyTexture(ImTextureData* textureData) {
    if (textureData == nullptr) {
        return;
    }

    const bgfx::TextureHandle handle = textureHandleFromId(textureData->GetTexID());
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
    }

    textureData->SetTexID(ImTextureID_Invalid);
    textureData->SetStatus(ImTextureStatus_Destroyed);
}

const bgfx::Memory* buildRgbaTextureMemory(ImTextureData* textureData, const ImTextureRect* rect = nullptr) {
    if (textureData == nullptr || textureData->Width <= 0 || textureData->Height <= 0 || textureData->BytesPerPixel <= 0) {
        return nullptr;
    }

    int x = 0;
    int y = 0;
    int width = textureData->Width;
    int height = textureData->Height;

    if (rect != nullptr) {
        x = rect->x;
        y = rect->y;
        width = rect->w;
        height = rect->h;
        if (width <= 0 || height <= 0) {
            return nullptr;
        }
        if (x < 0 || y < 0 || x + width > textureData->Width || y + height > textureData->Height) {
            return nullptr;
        }
    }

    if (textureData->Format == ImTextureFormat_RGBA32) {
        const uint32_t rowBytes = static_cast<uint32_t>(width) * 4u;
        const uint32_t totalBytes = rowBytes * static_cast<uint32_t>(height);
        const bgfx::Memory* memory = bgfx::alloc(totalBytes);
        if (memory == nullptr) {
            return nullptr;
        }

        if (rect == nullptr) {
            bx::memCopy(memory->data, textureData->GetPixels(), totalBytes);
        } else {
            bx::gather(
                memory->data,
                textureData->GetPixelsAt(x, y),
                static_cast<uint32_t>(textureData->GetPitch()),
                rowBytes,
                static_cast<uint32_t>(height)
            );
        }

        return memory;
    }

    if (textureData->Format != ImTextureFormat_Alpha8) {
        return nullptr;
    }

    const uint32_t totalPixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    const bgfx::Memory* memory = bgfx::alloc(totalPixels * 4u);
    if (memory == nullptr) {
        return nullptr;
    }

    const uint8_t* source = static_cast<const uint8_t*>(rect == nullptr ? textureData->GetPixels() : textureData->GetPixelsAt(x, y));
    const int sourcePitch = textureData->GetPitch();
    uint8_t* destination = memory->data;
    for (int row = 0; row < height; ++row) {
        const uint8_t* sourceRow = source + row * sourcePitch;
        for (int column = 0; column < width; ++column) {
            const uint8_t alpha = sourceRow[column];
            const uint32_t index = static_cast<uint32_t>(row * width + column) * 4u;
            destination[index + 0u] = 255u;
            destination[index + 1u] = 255u;
            destination[index + 2u] = 255u;
            destination[index + 3u] = alpha;
        }
    }

    return memory;
}

bgfx::TextureHandle createTextureFromImGuiData(ImTextureData* textureData) {
    bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<uint16_t>(textureData->Width),
        static_cast<uint16_t>(textureData->Height),
        false,
        1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_NONE |
            BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP
    );
    if (!bgfx::isValid(texture)) {
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::Memory* memory = buildRgbaTextureMemory(textureData);
    if (memory == nullptr) {
        bgfx::destroy(texture);
        return BGFX_INVALID_HANDLE;
    }

    bgfx::updateTexture2D(
        texture,
        0,
        0,
        0,
        0,
        static_cast<uint16_t>(textureData->Width),
        static_cast<uint16_t>(textureData->Height),
        memory
    );

    return texture;
}

void updateTexture(ImTextureData* textureData) {
    if (textureData == nullptr) {
        return;
    }

    if (textureData->Status == ImTextureStatus_WantCreate) {
        IM_ASSERT(textureData->GetTexID() == ImTextureID_Invalid);

        const bgfx::TextureHandle texture = createTextureFromImGuiData(textureData);
        if (!bgfx::isValid(texture)) {
            textureData->SetTexID(ImTextureID_Invalid);
            textureData->SetStatus(ImTextureStatus_Destroyed);
            return;
        }

        textureData->SetTexID(textureIdFromHandle(texture));
        textureData->SetStatus(ImTextureStatus_OK);
        return;
    }

    if (textureData->Status == ImTextureStatus_WantUpdates) {
        const bgfx::TextureHandle texture = textureHandleFromId(textureData->GetTexID());
        if (!bgfx::isValid(texture)) {
            textureData->SetTexID(ImTextureID_Invalid);
            textureData->SetStatus(ImTextureStatus_Destroyed);
            return;
        }

        for (ImTextureRect& rect : textureData->Updates) {
            const bgfx::Memory* memory = buildRgbaTextureMemory(textureData, &rect);
            if (memory == nullptr) {
                continue;
            }

            bgfx::updateTexture2D(
                texture,
                0,
                0,
                static_cast<uint16_t>(rect.x),
                static_cast<uint16_t>(rect.y),
                static_cast<uint16_t>(rect.w),
                static_cast<uint16_t>(rect.h),
                memory
            );
        }

        textureData->SetStatus(ImTextureStatus_OK);
        return;
    }

    if (textureData->Status == ImTextureStatus_WantDestroy && textureData->UnusedFrames > 0) {
        destroyTexture(textureData);
    }
}

void destroyManagedTextures() {
    for (ImTextureData* textureData : ImGui::GetPlatformIO().Textures) {
        destroyTexture(textureData);
    }
}

void invalidateDeviceObjects() {
    BackendData* bd = getBackendData();
    if (bd == nullptr) {
        return;
    }

    if (bgfx::isValid(bd->sampler)) {
        bgfx::destroy(bd->sampler);
        bd->sampler = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(bd->program)) {
        bgfx::destroy(bd->program);
        bd->program = BGFX_INVALID_HANDLE;
    }

    destroyManagedTextures();
}

bool createDeviceObjects() {
    BackendData* bd = getBackendData();
    if (bd == nullptr) {
        return false;
    }

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShader = bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "vs_ocornut_imgui");
    const bgfx::ShaderHandle fragmentShader = bgfx::createEmbeddedShader(kEmbeddedShaders, rendererType, "fs_ocornut_imgui");
    if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
        if (bgfx::isValid(vertexShader)) {
            bgfx::destroy(vertexShader);
        }
        if (bgfx::isValid(fragmentShader)) {
            bgfx::destroy(fragmentShader);
        }
        return false;
    }

    bd->program = bgfx::createProgram(vertexShader, fragmentShader, true);
    if (!bgfx::isValid(bd->program)) {
        return false;
    }

    bd->sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(bd->sampler)) {
        return false;
    }

    bd->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    return true;
}

} // namespace

bool ImGui_Implbgfx_Init(bgfx::ViewId viewId) {
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialised an ImGui renderer backend!");

    BackendData* backend = IM_NEW(BackendData)();
    backend->viewId = viewId;
    io.BackendRendererUserData = backend;
    io.BackendRendererName = "imgui_impl_bgfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    return createDeviceObjects();
}

void ImGui_Implbgfx_Shutdown() {
    BackendData* backend = getBackendData();
    if (backend == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    invalidateDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);

    IM_DELETE(backend);
}

void ImGui_Implbgfx_NewFrame() {
    BackendData* backend = getBackendData();
    IM_ASSERT(backend != nullptr && "Did you call ImGui_Implbgfx_Init()?");

    if (!bgfx::isValid(backend->program)) {
        createDeviceObjects();
    }
}

void ImGui_Implbgfx_RenderDrawData(ImDrawData* drawData) {
    BackendData* backend = getBackendData();
    if (backend == nullptr || drawData == nullptr) {
        return;
    }

    if (drawData->Textures != nullptr) {
        for (ImTextureData* textureData : *drawData->Textures) {
            updateTexture(textureData);
        }
    }

    const int framebufferWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int framebufferHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }

    bgfx::setViewName(backend->viewId, "ImGui");
    bgfx::setViewMode(backend->viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(
        backend->viewId,
        0,
        0,
        static_cast<uint16_t>(framebufferWidth),
        static_cast<uint16_t>(framebufferHeight)
    );

    const bgfx::Caps* caps = bgfx::getCaps();
    float ortho[16];
    bx::mtxOrtho(
        ortho,
        drawData->DisplayPos.x,
        drawData->DisplayPos.x + drawData->DisplaySize.x,
        drawData->DisplayPos.y + drawData->DisplaySize.y,
        drawData->DisplayPos.y,
        0.0f,
        1000.0f,
        0.0f,
        caps->homogeneousDepth
    );
    bgfx::setViewTransform(backend->viewId, nullptr, ortho);

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        const uint32_t vertexCount = static_cast<uint32_t>(drawList->VtxBuffer.Size);
        const uint32_t indexCount = static_cast<uint32_t>(drawList->IdxBuffer.Size);

        const uint32_t availableVertices = bgfx::getAvailTransientVertexBuffer(vertexCount, backend->layout);
        const uint32_t availableIndices = bgfx::getAvailTransientIndexBuffer(indexCount, sizeof(ImDrawIdx) == 4);
        if (availableVertices != vertexCount || availableIndices != indexCount) {
            logTransientShortage(vertexCount, availableVertices, indexCount, availableIndices);
            continue;
        }

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::TransientIndexBuffer indexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, backend->layout);
        bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount, sizeof(ImDrawIdx) == 4);

        bx::memCopy(vertexBuffer.data, drawList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
        bx::memCopy(indexBuffer.data, drawList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

        bgfx::Encoder* encoder = bgfx::begin();

        for (int commandIndex = 0; commandIndex < drawList->CmdBuffer.Size; ++commandIndex) {
            const ImDrawCmd* command = &drawList->CmdBuffer[commandIndex];
            if (command->UserCallback != nullptr) {
                if (command->UserCallback == ImDrawCallback_ResetRenderState) {
                    continue;
                }
                command->UserCallback(drawList, command);
                continue;
            }

            if (command->ElemCount == 0) {
                continue;
            }

            ImVec4 clipRect;
            clipRect.x = (command->ClipRect.x - clipOffset.x) * clipScale.x;
            clipRect.y = (command->ClipRect.y - clipOffset.y) * clipScale.y;
            clipRect.z = (command->ClipRect.z - clipOffset.x) * clipScale.x;
            clipRect.w = (command->ClipRect.w - clipOffset.y) * clipScale.y;

            if (clipRect.x >= static_cast<float>(framebufferWidth) || clipRect.y >= static_cast<float>(framebufferHeight) ||
                clipRect.z < 0.0f || clipRect.w < 0.0f) {
                continue;
            }

            const int scissorX = static_cast<int>(std::max(clipRect.x, 0.0f));
            const int scissorY = static_cast<int>(std::max(clipRect.y, 0.0f));
            const int scissorZ = static_cast<int>(std::min(clipRect.z, 65535.0f));
            const int scissorW = static_cast<int>(std::min(clipRect.w, 65535.0f));
            if (scissorZ <= scissorX || scissorW <= scissorY) {
                continue;
            }

            const bgfx::TextureHandle texture = textureHandleFromId(command->GetTexID());
            if (!bgfx::isValid(texture)) {
                continue;
            }

            if (command->IdxOffset + command->ElemCount > indexCount) {
                continue;
            }

            const uint64_t state =
                BGFX_STATE_WRITE_RGB |
                BGFX_STATE_WRITE_A |
                BGFX_STATE_MSAA |
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

            encoder->setScissor(
                static_cast<uint16_t>(scissorX),
                static_cast<uint16_t>(scissorY),
                static_cast<uint16_t>(scissorZ - scissorX),
                static_cast<uint16_t>(scissorW - scissorY)
            );
            encoder->setState(state);
            encoder->setTexture(0, backend->sampler, texture);
            encoder->setVertexBuffer(0, &vertexBuffer, 0, vertexCount);
            encoder->setIndexBuffer(&indexBuffer, command->IdxOffset, command->ElemCount);
            encoder->submit(backend->viewId, backend->program);
        }

        bgfx::end(encoder);
    }
}

void ImGui_Implbgfx_SetViewId(bgfx::ViewId viewId) {
    BackendData* backend = getBackendData();
    if (backend != nullptr) {
        backend->viewId = viewId;
    }
}
