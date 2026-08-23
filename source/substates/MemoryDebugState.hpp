#ifndef MEMORYDEBUGSTATE_HPP
#define MEMORYDEBUGSTATE_HPP

#include <vector>
#include <string>
#include <citro2d.h>
#include <citro3d.h>

class MemoryDebugState {
public:
    MemoryDebugState();
    ~MemoryDebugState();

    void update(float dt);
    void draw();

    bool active = true;

private:
    struct MemoryItem {
        std::string name;
        u32 size;
        bool isVram;
    };
    std::vector<MemoryItem> memoryItems;
    int memorySelection = 0;
    float memoryScrollLerp = 0.0f;
    C2D_TextBuf memoryTextBuf = nullptr;

    void gatherAllMemoryItems();
};

#endif
