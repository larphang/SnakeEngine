#ifndef CHARACTEREDITORSTATE_HPP
#define CHARACTEREDITORSTATE_HPP

#include "../backend/MusicBeatState.hpp"
#include "../objects/Character.hpp"
#include <citro2d.h>
#include <citro3d.h>
#include <vector>
#include <string>

class CharacterEditorState : public MusicBeatState {
public:
    struct UIWindow {
        std::string title;
        float x;
        float y;
        float w;
        float h;
        bool expanded;
        int id; // 0 = Settings, 1 = Ghost, 2 = Character, 3 = Animations
    };

    CharacterEditorState();
    ~CharacterEditorState();

    void init() override;
    void update(float dt) override;
    void draw(C3D_RenderTarget* top, C3D_RenderTarget* bottom) override;
    void exitState() override;

private:
    C2D_TextBuf textBuf = nullptr;
    Character* charObj = nullptr;
    Character* ghostObj = nullptr;
    
    std::string currentCharacter = "bf";
    std::vector<std::string> characterList;
    int curCharIndex = 0;

    std::vector<std::string> animList;
    int curAnimIndex = 0;
    int curGhostAnimIndex = 0;

    bool showGhost = true;
    bool pcMode = false;
    bool uiExpanded = true;
    
    std::vector<UIWindow> windows;
    int draggedWindowId = -1;
    float dragWindowOffsetX = 0.0f;
    float dragWindowOffsetY = 0.0f;

    int lastTouchX = -1;
    int lastTouchY = -1;
    float saveMessageTimer = 0.0f;
    
    // UI state
    int currentTab = 0; // 0 = Settings, 1 = Ghost, 2 = Character, 3 = Animations
    int curSelected = 0;
    float ghostAlpha = 0.5f;
    bool ghostHighlight = false;
    int animSliderIndex = 0;
    float charScrollY = 0.0f;
    float animScrollY = 0.0f;
    bool isDraggingCharList = false;
    bool isDraggingAnimList = false;
    float dragStartY = 0.0f;
    float dragStartScroll = 0.0f;
    bool touchHeldLastFrame = false;
    float touchStartPx = 0.0f;
    float touchStartPy = 0.0f;

    // Camera panning
    float camX = 0;
    float camY = 0;
    float camZoom = 1.0f;

    // Helpers
    void loadCharacter(const std::string& name);
    void updateAnimList();
    void playCurAnim();
    void saveCharacter();
};

#endif
