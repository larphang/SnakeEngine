#include "MemoryDebugState.hpp"
#include <malloc.h>
#include <set>
#include "../states/PlayState.hpp"
#include "../backend/savedata/ClientPrefs.hpp"
#include "../backend/SpritesheetCache.hpp"
#include "../shaders/ShaderManager.hpp"
#include "../backend/LuaManager.hpp"
#include "../objects/Character.hpp"
#include "../objects/Stage.hpp"
#include "../backend/AudioEngine.hpp"

extern C2D_Font globalVCRFont;

MemoryDebugState::MemoryDebugState() {
    memoryTextBuf = C2D_TextBufNew(2048);
    active = true;
    gatherAllMemoryItems();
    memorySelection = 0;
    memoryScrollLerp = 0.0f;
}

MemoryDebugState::~MemoryDebugState() {
    if (memoryTextBuf) {
        C2D_TextBufDelete(memoryTextBuf);
    }
}

static u32 getTexSize(C3D_Tex* tex) {
    if (!tex) return 0;
    u32 w = tex->width;
    u32 h = tex->height;
    u32 bpp = 4;
    switch (tex->fmt) {
        case GPU_RGBA8: bpp = 4; break;
        case GPU_RGB8: bpp = 3; break;
        case GPU_RGBA5551:
        case GPU_RGB565:
        case GPU_RGBA4:
        case GPU_LA8: bpp = 2; break;
        case GPU_A8:
        case GPU_L8:
        case GPU_ETC1A4: bpp = 1; break;
        case GPU_L4:
        case GPU_A4:
        case GPU_ETC1: return (w * h) / 2;
        default: bpp = 4; break;
    }
    return w * h * bpp;
}

static bool isVRAM(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    return (addr >= 0x1F000000 && addr < 0x1F600000);
}

void MemoryDebugState::gatherAllMemoryItems() {
    memoryItems.clear();
    std::set<void*> addedTexData;

    auto addTex = [&](const std::string& name, C3D_Tex* tex) {
        if (!tex) return;
        MemoryItem item;
        item.name = name;
        if (tex->data && addedTexData.count(tex->data) > 0) {
            item.size = 0;
            item.name += " (Shared)";
        } else {
            item.size = getTexSize(tex);
            if (tex->data) {
                addedTexData.insert(tex->data);
            }
        }
        item.isVram = isVRAM(tex->data);
        memoryItems.push_back(item);
    };

    auto addSheet = [&](const std::string& name, C2D_SpriteSheet sheet) {
        if (!sheet) return;
        C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
        addTex(name, img.tex);
    };

    const auto& cache = SpritesheetCache::get().getCache();
    for (const auto& pair : cache) {
        if (pair.second.sheet) addSheet(pair.first, pair.second.sheet);
        for (size_t i = 0; i < pair.second.subSheets.size(); i++) {
            addSheet(pair.first + "_[" + std::to_string(i) + "]", pair.second.subSheets[i]);
        }
    }

    if (PlayState::instance->bf) addSheet("BF", PlayState::instance->bf->sheet);
    if (PlayState::instance->dad) addSheet("Dad", PlayState::instance->dad->sheet);
    if (PlayState::instance->gf) addSheet("GF", PlayState::instance->gf->sheet);
    if (PlayState::instance->deadBF) addSheet("deadBF", PlayState::instance->deadBF->sheet);
    addSheet("noteSheet", PlayState::instance->noteSheet);
    addSheet("ratingSheet", PlayState::instance->ratingSheet);

    for (const auto& s : PlayState::instance->luaSprites) {
        if (s.sheet) {
            addSheet("Lua: " + s.name, s.sheet);
        } else if (s.img.tex) {
            addTex("Lua: " + s.name, s.img.tex);
        } else if (s.isGraphic) {
            MemoryItem item;
            item.name = "Graphic: " + s.name;
            item.size = 0;
            item.isVram = false;
            memoryItems.push_back(item);
        }
    }

    for (const auto& t : PlayState::instance->luaTexts) {
        if (t.buf) {
            MemoryItem item;
            item.name = "Text: " + t.tag;
            item.size = 512;
            item.isVram = false;
            memoryItems.push_back(item);
        }
    }

    auto targets = ShaderManager::get().getActiveTargets();
    for (const auto& t : targets) {
        addTex("Shader: " + t.first, t.second);
    }

    const auto& sndCache = AudioEngine::getSoundCache();
    for (const auto& pair : sndCache) {
        if (pair.second.buffer) {
            MemoryItem item;
            item.name = "Audio: " + pair.first;
            item.size = pair.second.samplesPerChannel * pair.second.channels * 2;
            item.isVram = false;
            memoryItems.push_back(item);
        }
    }
    if (AudioEngine::isLoaded) {
        MemoryItem item;
        item.name = "Audio: Song Inst Stream";
        item.size = 4 * 2048 * 2 * 2;
        item.isVram = false;
        memoryItems.push_back(item);

        if (AudioEngine::hasVocals) {
            MemoryItem itemV;
            itemV.name = "Audio: Song Voc Stream";
            itemV.size = 4 * 2048 * 2 * 2;
            itemV.isVram = false;
            memoryItems.push_back(itemV);
        }
    }

    auto addTextBuf = [&](const std::string& name, C2D_TextBuf buf, size_t sizeVal) {
        if (!buf) return;
        MemoryItem item;
        item.name = name;
        item.size = sizeVal * 24; // text buffers typically hold glyph structs of around 24 bytes
        item.isVram = false;
        memoryItems.push_back(item);
    };

    addTextBuf("TextBuf: vcrFontBuf", PlayState::instance->vcrFontBuf, 2048);
    addTextBuf("TextBuf: botplayTextBuf", PlayState::instance->botplayTextBuf, 32);
    addTextBuf("TextBuf: timeTextBuf", PlayState::instance->timeTextBuf, 32);
    addTextBuf("TextBuf: lyricsTextBuf", PlayState::instance->lyricsTextBuf, 2048);
    addTextBuf("TextBuf: debugTextBuf", PlayState::instance->debugTextBuf, 2048);

    if (memoryTextBuf) {
        MemoryItem item;
        item.name = "MemoryDebug TextBuf";
        item.size = 2048 * 24; 
        item.isVram = false;
        memoryItems.push_back(item);
    }
}

void MemoryDebugState::update(float dt) {
    u32 kDown = hidKeysDown();

    memoryScrollLerp += (memorySelection - memoryScrollLerp) * (dt * 15.0f);

    if (kDown & (KEY_DUP | KEY_CPAD_UP)) { 
        memorySelection--;
        if (memorySelection < 0) memorySelection = memoryItems.empty() ? 0 : memoryItems.size() - 1;
    }
    if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) { 
        memorySelection++;
        if (memorySelection >= (int)memoryItems.size()) memorySelection = 0;
    }

    if (kDown & KEY_B) {
        active = false;
    }
}

static std::string formatSize(u32 bytes) {
    if (bytes == 0) return "0B";
    if (bytes < 1024) return std::to_string(bytes) + "B";
    if (bytes < 1024 * 1024) {
        float kb = (float)bytes / 1024.0f;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fKB", kb);
        return std::string(buf);
    }
    float mb = (float)bytes / (1024.0f * 1024.0f);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fMB", mb);
    return std::string(buf);
}

void MemoryDebugState::draw() {
    C2D_SceneBegin(PlayState::instance->bottom);
    C2D_TargetClear(PlayState::instance->bottom, C2D_Color32(0, 0, 0, 255));
    float bottomH = 240.0f;

    if (!globalVCRFont || !memoryTextBuf) return;
    C2D_TextBufClear(memoryTextBuf);

    C2D_Text titleText;
    C2D_TextFontParse(&titleText, globalVCRFont, memoryTextBuf, "MEMORY DEBUG");
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, 5, 5, 1.0f, 0.6f, 0.6f, CWhite);

    C2D_Text headText;
    C2D_TextFontParse(&headText, globalVCRFont, memoryTextBuf, "OBJECT                  SIZE     POOL");
    C2D_TextOptimize(&headText);
    C2D_DrawText(&headText, C2D_WithColor, 5, 30, 1.0f, 0.4f, 0.4f, CWhite);

    for (int i = 0; i < (int)memoryItems.size(); i++) {
        float y = 50.0f + ((i - memoryScrollLerp) * 16.0f) + (8 / 2.0f) * 16.0f;
        if (y < 40.0f || y > bottomH - 45.0f) continue; 

        std::string nameStr = memoryItems[i].name;
        size_t pos = nameStr.find_last_of("/\\");
        if (pos != std::string::npos) nameStr = nameStr.substr(pos + 1);

        if (i == memorySelection) nameStr = "> " + nameStr;
        if (nameStr.length() > 22) nameStr = nameStr.substr(0, 19) + "...";
        std::string sizeStr = formatSize(memoryItems[i].size);
        std::string poolStr = memoryItems[i].isVram ? "GFX" : "LIN";

        C2D_Text nameObj, sizeObj, poolObj;
        C2D_TextFontParse(&nameObj, globalVCRFont, memoryTextBuf, nameStr.c_str());
        C2D_TextFontParse(&sizeObj, globalVCRFont, memoryTextBuf, sizeStr.c_str());
        C2D_TextFontParse(&poolObj, globalVCRFont, memoryTextBuf, poolStr.c_str());
        C2D_TextOptimize(&nameObj);
        C2D_TextOptimize(&sizeObj);
        C2D_TextOptimize(&poolObj);
        
        u32 color = (i == memorySelection) ? CWhite : C2D_Color32(160, 160, 160, 200);
        if (memoryItems[i].size > 2 * 1024 * 1024) {
            color = (i == memorySelection) ? C2D_Color32(255, 100, 100, 255) : C2D_Color32(200, 50, 50, 255);
        }
        float scale = 0.4f;

        C2D_DrawText(&nameObj, C2D_WithColor, 5, y, 1.0f, scale, scale, color);
        C2D_DrawText(&sizeObj, C2D_WithColor, 200, y, 1.0f, scale, scale, color);
        C2D_DrawText(&poolObj, C2D_WithColor, 270, y, 1.0f, scale, scale, color);
    }

    extern u32 __ctru_linear_heap_size;
    float lramTotal = (float)__ctru_linear_heap_size / (1024.0f * 1024.0f);
    float lramUsed = lramTotal - ((float)linearSpaceFree() / (1024.0f * 1024.0f));
    float vramUsed = (float)(6 * 1024 * 1024 - vramSpaceFree()) / (1024.0f * 1024.0f);
    float vramTotal = 6.0f;
    struct mallinfo mi = mallinfo();
    float stdRamUsed = (float)mi.uordblks / (1024.0f * 1024.0f);
    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);

    char debugStr[256];
    sprintf(debugStr, "FPS: %d | Model: %s\nRAM: %.1f MB | L-RAM: %.1f/%.1f MB\nVRAM: %.1f/%.1f MB", 
        (int)PlayState::instance->curFPS, isNew3DS ? "New" : "Old", stdRamUsed, lramUsed, lramTotal, vramUsed, vramTotal);

    C2D_Text statsText;
    C2D_TextFontParse(&statsText, globalVCRFont, memoryTextBuf, debugStr);
    C2D_TextOptimize(&statsText);
    C2D_DrawText(&statsText, C2D_WithColor, 5, 200.0f, 1.0f, 0.4f, 0.4f, CWhite);
}
