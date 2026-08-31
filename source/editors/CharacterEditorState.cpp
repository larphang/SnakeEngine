#include "CharacterEditorState.hpp"
#include "../backend/Paths.hpp"
#include "../backend/AudioEngine.hpp"
#include "../shaders/ShaderManager.hpp"
#include "../backend/savedata/ClientPrefs.hpp"
#include "../debug/DebugMenuState.hpp"
#include <citro2d.h>
#include <dirent.h>
#include <algorithm>

static std::string getIndicesString(const std::vector<int>& indices, bool pcMode) {
    if (indices.empty()) return "NONE";
    std::string s = "[";
    for (size_t i = 0; i < indices.size(); i++) {
        s += std::to_string(indices[i]);
        if (i < indices.size() - 1) {
            s += ",";
            if (s.length() > (pcMode ? 10 : 26)) {
                s += "...";
                break;
            }
        }
    }
    s += "]";
    return s;
}

struct UIElement {
    int id;              // -1 for accordion headers, 0..20 for option controls
    int headerIndex;     // 0..3 if it is a header
    float x, y, w, h;
    std::string label;
};

static std::vector<UIElement> getPCLayout(const std::vector<CharacterEditorState::UIWindow>& windows) {
    std::vector<UIElement> elements;
    
    for (int winIdx = 0; winIdx < 4; winIdx++) {
        const CharacterEditorState::UIWindow* winPtr = nullptr;
        for (const auto& w : windows) {
            if (w.id == winIdx) {
                winPtr = &w;
                break;
            }
        }
        if (!winPtr) continue;
        const auto& win = *winPtr;

        // Title bar (elem.id == -1, elem.headerIndex == win.id)
        elements.push_back({ -1, win.id, win.x, win.y, win.w, 12.0f, win.title });
        
        if (win.expanded) {
            float contentY = win.y + 12.0f;
            if (win.id == 0) { // Settings
                // Character Listbox (id = 0)
                elements.push_back({ 0, -1, win.x + 4.0f, contentY + 4.0f, win.w - 8.0f, 54.0f, "CharList" });
                // Playable Checkbox (id = 1)
                elements.push_back({ 1, -1, win.x + 4.0f, contentY + 62.0f, win.w - 8.0f, 10.0f, "Playable" });
                // Reload Button (id = 2)
                elements.push_back({ 2, -1, win.x + 4.0f, contentY + 75.0f, (win.w - 10.0f) * 0.5f, 12.0f, "Reload" });
                // Save Button (id = 3)
                elements.push_back({ 3, -1, win.x + win.w * 0.5f + 1.0f, contentY + 75.0f, (win.w - 10.0f) * 0.5f, 12.0f, "Save" });
            }
            else if (win.id == 1) { // Ghost
                // Make Ghost Button (id = 4)
                elements.push_back({ 4, -1, win.x + 4.0f, contentY + 4.0f, win.w - 8.0f, 12.0f, "Make Ghost" });
                // Show Ghost Checkbox (id = 5)
                elements.push_back({ 5, -1, win.x + 4.0f, contentY + 20.0f, win.w - 8.0f, 10.0f, "Show Ghost" });
                // Highlight Ghost Checkbox (id = 6)
                elements.push_back({ 6, -1, win.x + 4.0f, contentY + 32.0f, win.w - 8.0f, 10.0f, "Highlight" });
                // Ghost Alpha Slider (id = 7)
                elements.push_back({ 7, -1, win.x + 4.0f, contentY + 44.0f, win.w - 8.0f, 18.0f, "Alpha" });
            }
            else if (win.id == 2) { // Character
                std::string charLabels[] = {
                    "Sing Length", "Scale", "Flip X", "Antialiasing",
                    "Pos X", "Pos Y", "Cam Off X", "Cam Off Y"
                };
                float elementYs[] = { 4.0f, 18.0f, 32.0f, 44.0f, 58.0f, 72.0f, 86.0f, 100.0f };
                float elementHeights[] = { 12.0f, 12.0f, 10.0f, 10.0f, 12.0f, 12.0f, 12.0f, 12.0f };
                for (int i = 0; i < 8; i++) {
                    elements.push_back({ 8 + i, -1, win.x + 4.0f, contentY + elementYs[i], win.w - 8.0f, elementHeights[i], charLabels[i] });
                }
            }
            else if (win.id == 3) { // Animations
                // Animations List Box (id = 16)
                elements.push_back({ 16, -1, win.x + 4.0f, contentY + 4.0f, win.w - 8.0f, 54.0f, "AnimList" });
                // FPS (id = 17)
                elements.push_back({ 17, -1, win.x + 4.0f, contentY + 74.0f, win.w - 8.0f, 12.0f, "FPS" });
                // Loop Checkbox (id = 18)
                elements.push_back({ 18, -1, win.x + 4.0f, contentY + 88.0f, win.w - 8.0f, 10.0f, "Loop" });
                // Offset X (id = 19)
                elements.push_back({ 19, -1, win.x + 4.0f, contentY + 100.0f, win.w - 8.0f, 12.0f, "Offset X" });
                // Offset Y (id = 20)
                elements.push_back({ 20, -1, win.x + 4.0f, contentY + 114.0f, win.w - 8.0f, 12.0f, "Offset Y" });
            }
        }
    }
    return elements;
}

#include "../backend/ModHandler.hpp"

CharacterEditorState::CharacterEditorState() {
    // 1. Scan default characters
    DIR* dir = opendir(Paths::resolve("romfs:/preload/characters").c_str());
    if (dir) {
        struct dirent* dp;
        while ((dp = readdir(dir)) != nullptr) {
            std::string file = dp->d_name;
            if (file.find(".json") != std::string::npos) {
                std::string charName = file.substr(0, file.find_last_of('.'));
                if (std::find(characterList.begin(), characterList.end(), charName) == characterList.end()) {
                    characterList.push_back(charName);
                }
            }
        }
        closedir(dir);
    }

    // 2. Scan custom characters folder (sdmc:/SnakeEngine/characters/)
    DIR* customDir = opendir(Paths::resolve("sdmc:/SnakeEngine/characters").c_str());
    if (customDir) {
        struct dirent* dp;
        while ((dp = readdir(customDir)) != nullptr) {
            std::string file = dp->d_name;
            if (file.find(".json") != std::string::npos) {
                std::string charName = file.substr(0, file.find_last_of('.'));
                if (std::find(characterList.begin(), characterList.end(), charName) == characterList.end()) {
                    characterList.push_back(charName);
                }
            }
        }
        closedir(customDir);
    }

    // 3. Scan characters in active mods (mods/<folder>/characters/)
    for (const auto& mod : ModHandler::get().getMods()) {
        if (!mod.active) continue;
        std::string modCharPath = ModHandler::getWorkingBase() + mod.folder + "/characters";
        DIR* modDir = opendir(modCharPath.c_str());
        if (modDir) {
            struct dirent* dp;
            while ((dp = readdir(modDir)) != nullptr) {
                std::string file = dp->d_name;
                if (file.find(".json") != std::string::npos) {
                    std::string charName = file.substr(0, file.find_last_of('.'));
                    if (std::find(characterList.begin(), characterList.end(), charName) == characterList.end()) {
                        characterList.push_back(charName);
                    }
                }
            }
            closedir(modDir);
        }
    }

    if (characterList.empty()) characterList.push_back("bf");

    // Initialize floating windows for PC Mode
    windows = {
        { "Settings", 5.0f, 15.0f, 110.0f, 114.0f, true, 0 },
        { "Ghost", 5.0f, 134.0f, 110.0f, 82.0f, false, 1 },
        { "Character", 120.0f, 15.0f, 110.0f, 134.0f, true, 2 },
        { "Animations", 120.0f, 155.0f, 110.0f, 178.0f, false, 3 }
    };
}

void CharacterEditorState::loadCharacter(const std::string& name) {
    if (charObj) delete charObj;
    if (ghostObj) delete ghostObj;

    currentCharacter = name;

    // Detect which mod folder contains the character JSON to load assets from it
    std::string targetModFolder = "";
    std::string customPath = "sdmc:/SnakeEngine/characters/" + name + ".json";
    if (!Paths::fileExists(customPath)) {
        for (const auto& mod : ModHandler::get().getMods()) {
            if (!mod.active) continue;
            std::string path = ModHandler::getWorkingBase() + mod.folder + "/characters/" + name + ".json";
            if (Paths::fileExists(path)) {
                targetModFolder = mod.folder;
                break;
            }
        }
    }
    ModHandler::get().currentModFolder = targetModFolder;
    
    charObj = new Character();
    charObj->loadFromPsychJson(Paths::characterJson(name));
    charObj->isPlayer = false; // Default face
    charObj->dance();

    ghostObj = new Character();
    ghostObj->loadFromPsychJson(Paths::characterJson(name));
    ghostObj->isPlayer = false;
    ghostObj->alpha = ghostAlpha;
    ghostObj->isHighlighted = ghostHighlight;
    ghostObj->dance();
    ghostObj->animFinished = true; // freeze ghost initially

    // Find and sync character index
    auto it = std::find(characterList.begin(), characterList.end(), name);
    if (it != characterList.end()) {
        curCharIndex = std::distance(characterList.begin(), it);
    }

    charScrollY = 0.0f;
    animScrollY = 0.0f;
    animSliderIndex = 0;
    updateAnimList();
}

void CharacterEditorState::updateAnimList() {
    animList.clear();
    for (const auto& kv : charObj->animations) {
        animList.push_back(kv.first);
    }
    curAnimIndex = 0;
    animSliderIndex = 0;
    animScrollY = 0.0f;
    playCurAnim();
}

void CharacterEditorState::playCurAnim() {
    if (animList.empty()) return;
    charObj->playAnim(animList[curAnimIndex], true);
}

void CharacterEditorState::update(float dt) {
    MusicBeatState::update(dt);

    if (saveMessageTimer > 0.0f) {
        saveMessageTimer -= dt;
    }

    if (charObj) charObj->update(dt);

    if (keyJustPressed(KEY_X)) {
        pcMode = !pcMode;
        uiExpanded = true;
    }

    // Camera panning & zooming
    if (hidKeysHeld() & KEY_CPAD_UP) camY -= 200 * dt;
    if (hidKeysHeld() & KEY_CPAD_DOWN) camY += 200 * dt;
    if (hidKeysHeld() & KEY_CPAD_LEFT) camX -= 200 * dt;
    if (hidKeysHeld() & KEY_CPAD_RIGHT) camX += 200 * dt;
    if (hidKeysHeld() & KEY_L) camZoom += 1.0f * dt;
    if (hidKeysHeld() & KEY_R) camZoom -= 1.0f * dt;
    if (camZoom < 0.1f) camZoom = 0.1f;

    if (keyJustPressed(KEY_SELECT) || keyJustPressed(KEY_B)) {
        exitState();
        return;
    }


    // Populate visibleOptionIds for PC Mode accordion navigation
    std::vector<int> visibleOptionIds;
    if (pcMode && uiExpanded) {
        auto layout = getPCLayout(windows);
        for (const auto& elem : layout) {
            if (elem.id != -1) {
                visibleOptionIds.push_back(elem.id);
            }
        }
        if (curSelected >= (int)visibleOptionIds.size()) {
            curSelected = visibleOptionIds.empty() ? 0 : (int)visibleOptionIds.size() - 1;
        }
    }

    // Calculate tab maximum options for DPAD navigation
    int maxOpts = 4;
    if (!pcMode) {
        if (currentTab == 2) maxOpts = 8;
        else if (currentTab == 3) maxOpts = 5;
    } else {
        maxOpts = visibleOptionIds.size();
    }

    if (maxOpts > 0) {
        if (keyJustPressed(KEY_DUP)) {
            curSelected--;
            if (curSelected < 0) curSelected = maxOpts - 1;
            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.3f);
        }
        if (keyJustPressed(KEY_DDOWN)) {
            curSelected++;
            if (curSelected >= maxOpts) curSelected = 0;
            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.3f);
        }
    }

    bool pressLeft = keyJustPressed(KEY_DLEFT);
    bool pressRight = keyJustPressed(KEY_DRIGHT);
    
    // Y button held: fast adjust
    float change = 1.0f;
    if (hidKeysHeld() & KEY_Y) change = 10.0f;

    // Simulate pressRight with KEY_A on action rows
    if (keyJustPressed(KEY_A)) {
        if (!pcMode) {
            if (currentTab == 0 && (curSelected == 1 || curSelected == 2 || curSelected == 3)) {
                pressRight = true;
            } else if (currentTab == 1 && (curSelected == 0 || curSelected == 1 || curSelected == 2)) {
                pressRight = true;
            } else if (currentTab == 2 && (curSelected == 2 || curSelected == 3)) {
                pressRight = true;
            } else if (currentTab == 3 && curSelected == 2) {
                pressRight = true;
            }
        } else if (!visibleOptionIds.empty()) {
            int optId = visibleOptionIds[curSelected];
            if (optId == 1 || optId == 2 || optId == 3 || optId == 4 || optId == 5 || optId == 6 || optId == 10 || optId == 11 || optId == 18) {
                pressRight = true;
            }
        }
    }

    // Process Dpad / Button Changes
    if (pressLeft || pressRight) {
        if (!pcMode) {
            if (currentTab == 0) { // Settings
                if (curSelected == 0) { // Character list selection via D-pad
                    if (pressLeft) {
                        curCharIndex--;
                        if (curCharIndex < 0) curCharIndex = characterList.size() - 1;
                    } else {
                        curCharIndex++;
                        if (curCharIndex >= (int)characterList.size()) curCharIndex = 0;
                    }
                    loadCharacter(characterList[curCharIndex]);
                } else if (curSelected == 1) { // Playable
                    charObj->isPlayer = !charObj->isPlayer;
                    ghostObj->isPlayer = charObj->isPlayer;
                    playCurAnim();
                } else if (curSelected == 2) { // Reload
                    loadCharacter(currentCharacter);
                } else if (curSelected == 3) { // Save
                    saveCharacter();
                }
            }
            else if (currentTab == 1) { // Ghost
                if (curSelected == 0) { // Make Ghost from Current
                    ghostObj->isPlayer = charObj->isPlayer;
                    ghostObj->flipX = charObj->flipX;
                    ghostObj->charScale = charObj->charScale;
                    ghostObj->charScaleX = charObj->charScale;
                    ghostObj->charScaleY = charObj->charScale;
                    if (ghostObj->isSpritemap) {
                        ghostObj->spritemapAnim.scaleX = charObj->charScale;
                        ghostObj->spritemapAnim.scaleY = charObj->charScale;
                    }
                    ghostObj->animations = charObj->animations;
                    ghostObj->playAnim(charObj->curAnim, true);
                    ghostObj->curFrame = charObj->curFrame;
                    ghostObj->frameTimer = charObj->frameTimer;
                    ghostObj->animFinished = charObj->animFinished;
                    ghostObj->spritemapAnim.smLogicalFrame = charObj->spritemapAnim.smLogicalFrame;
                    ghostObj->spritemapAnim.animFinished = charObj->spritemapAnim.animFinished;
                } else if (curSelected == 1) { // Show Ghost
                    showGhost = !showGhost;
                } else if (curSelected == 2) { // Highlight Ghost
                    ghostHighlight = !ghostHighlight;
                    ghostObj->isHighlighted = ghostHighlight;
                } else if (curSelected == 3) { // Ghost Alpha Slider
                    ghostAlpha += pressLeft ? -0.1f : 0.1f;
                    if (ghostAlpha < 0.0f) ghostAlpha = 0.0f;
                    if (ghostAlpha > 1.0f) ghostAlpha = 1.0f;
                    ghostObj->alpha = ghostAlpha;
                }
            }
            else if (currentTab == 2) { // Character Config
                if (curSelected == 0) { // Sing Anim Length
                    charObj->singDuration += pressLeft ? -0.5f : 0.5f;
                    if (charObj->singDuration < 0.1f) charObj->singDuration = 0.1f;
                    ghostObj->singDuration = charObj->singDuration;
                } else if (curSelected == 1) { // Scale
                    charObj->charScale += pressLeft ? -change * 0.1f : change * 0.1f;
                    if (charObj->charScale < 0.1f) charObj->charScale = 0.1f;
                    charObj->charScaleX = charObj->charScale;
                    charObj->charScaleY = charObj->charScale;
                    if (charObj->isSpritemap) {
                        charObj->spritemapAnim.scaleX = charObj->charScale;
                        charObj->spritemapAnim.scaleY = charObj->charScale;
                    }
                    ghostObj->charScale = charObj->charScale;
                    ghostObj->charScaleX = charObj->charScale;
                    ghostObj->charScaleY = charObj->charScale;
                    if (ghostObj->isSpritemap) {
                        ghostObj->spritemapAnim.scaleX = charObj->charScale;
                        ghostObj->spritemapAnim.scaleY = charObj->charScale;
                    }
                    playCurAnim();
                } else if (curSelected == 2) { // Flip X
                    charObj->flipX = !charObj->flipX;
                    ghostObj->flipX = charObj->flipX;
                    playCurAnim();
                } else if (curSelected == 3) { // Antialiasing
                    charObj->noAntialiasing = !charObj->noAntialiasing;
                    ghostObj->noAntialiasing = charObj->noAntialiasing;
                    charObj->setAntialiasing(!charObj->noAntialiasing);
                    ghostObj->setAntialiasing(!ghostObj->noAntialiasing);
                } else if (curSelected == 4) { // Pos X
                    charObj->x += pressLeft ? -change : change;
                    charObj->baseX = charObj->x;
                    ghostObj->x = charObj->x;
                    ghostObj->baseX = charObj->x;
                } else if (curSelected == 5) { // Pos Y
                    charObj->y += pressLeft ? -change : change;
                    charObj->baseY = charObj->y;
                    ghostObj->y = charObj->y;
                    ghostObj->baseY = charObj->y;
                } else if (curSelected == 6) { // Cam Offset X
                    charObj->camOffsetX += pressLeft ? -change * 5.0f : change * 5.0f;
                    ghostObj->camOffsetX = charObj->camOffsetX;
                } else if (curSelected == 7) { // Cam Offset Y
                    charObj->camOffsetY += pressLeft ? -change * 5.0f : change * 5.0f;
                    ghostObj->camOffsetY = charObj->camOffsetY;
                }
            }
            else if (currentTab == 3) { // Animations List selection via D-pad
                if (!animList.empty()) {
                    std::string curAnimName = animList[animSliderIndex];
                    if (curSelected == 0) { // Select Anim
                        if (pressLeft) {
                            animSliderIndex--;
                            if (animSliderIndex < 0) animSliderIndex = animList.size() - 1;
                        } else {
                            animSliderIndex++;
                            if (animSliderIndex >= (int)animList.size()) animSliderIndex = 0;
                        }
                        curAnimIndex = animSliderIndex;
                        playCurAnim();
                    } else if (curSelected == 1) { // FPS
                        charObj->animations[curAnimName].fps += pressLeft ? -1 : 1;
                        if (charObj->animations[curAnimName].fps < 1) charObj->animations[curAnimName].fps = 1;
                        ghostObj->animations[curAnimName].fps = charObj->animations[curAnimName].fps;
                        playCurAnim();
                    } else if (curSelected == 2) { // Loop
                        charObj->setAnimLoop(curAnimName, !charObj->animations[curAnimName].loop);
                        ghostObj->setAnimLoop(curAnimName, charObj->animations[curAnimName].loop);
                        playCurAnim();
                    } else if (curSelected == 3) { // Offset X
                        charObj->animations[curAnimName].offsetX += pressLeft ? -change : change;
                        ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                        playCurAnim();
                    } else if (curSelected == 4) { // Offset Y
                        charObj->animations[curAnimName].offsetY += pressLeft ? -change : change;
                        ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                        playCurAnim();
                    }
                }
            }
        } else if (!visibleOptionIds.empty()) {
            // PC Mode Accordion-based value adjustments
            int optId = visibleOptionIds[curSelected];
            if (optId == 0) { // Character list
                if (pressLeft) {
                    curCharIndex--;
                    if (curCharIndex < 0) curCharIndex = characterList.size() - 1;
                } else {
                    curCharIndex++;
                    if (curCharIndex >= (int)characterList.size()) curCharIndex = 0;
                }
                loadCharacter(characterList[curCharIndex]);
            } else if (optId == 1) { // Playable
                charObj->isPlayer = !charObj->isPlayer;
                ghostObj->isPlayer = charObj->isPlayer;
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 2) { // Reload
                loadCharacter(currentCharacter);
                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
            } else if (optId == 3) { // Save
                saveCharacter();
                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
            } else if (optId == 4) { // Make Ghost
                ghostObj->isPlayer = charObj->isPlayer;
                ghostObj->flipX = charObj->flipX;
                ghostObj->charScale = charObj->charScale;
                ghostObj->charScaleX = charObj->charScale;
                ghostObj->charScaleY = charObj->charScale;
                if (ghostObj->isSpritemap) {
                    ghostObj->spritemapAnim.scaleX = charObj->charScale;
                    ghostObj->spritemapAnim.scaleY = charObj->charScale;
                }
                ghostObj->animations = charObj->animations;
                ghostObj->playAnim(charObj->curAnim, true);
                ghostObj->curFrame = charObj->curFrame;
                ghostObj->frameTimer = charObj->frameTimer;
                ghostObj->animFinished = charObj->animFinished;
                ghostObj->spritemapAnim.smLogicalFrame = charObj->spritemapAnim.smLogicalFrame;
                ghostObj->spritemapAnim.animFinished = charObj->spritemapAnim.animFinished;
                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
            } else if (optId == 5) { // Show Ghost
                showGhost = !showGhost;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 6) { // Highlight Ghost
                ghostHighlight = !ghostHighlight;
                ghostObj->isHighlighted = ghostHighlight;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 7) { // Ghost Alpha Slider
                ghostAlpha += pressLeft ? -0.05f : 0.05f;
                if (ghostAlpha < 0.0f) ghostAlpha = 0.0f;
                if (ghostAlpha > 1.0f) ghostAlpha = 1.0f;
                ghostObj->alpha = ghostAlpha;
            } else if (optId == 8) { // Sing Anim Length
                charObj->singDuration += pressLeft ? -0.5f : 0.5f;
                if (charObj->singDuration < 0.1f) charObj->singDuration = 0.1f;
                ghostObj->singDuration = charObj->singDuration;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 9) { // Scale
                charObj->charScale += pressLeft ? -change * 0.05f : change * 0.05f;
                if (charObj->charScale < 0.1f) charObj->charScale = 0.1f;
                charObj->charScaleX = charObj->charScale;
                charObj->charScaleY = charObj->charScale;
                if (charObj->isSpritemap) {
                    charObj->spritemapAnim.scaleX = charObj->charScale;
                    charObj->spritemapAnim.scaleY = charObj->charScale;
                }
                ghostObj->charScale = charObj->charScale;
                ghostObj->charScaleX = charObj->charScale;
                ghostObj->charScaleY = charObj->charScale;
                if (ghostObj->isSpritemap) {
                    ghostObj->spritemapAnim.scaleX = charObj->charScale;
                    ghostObj->spritemapAnim.scaleY = charObj->charScale;
                }
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 10) { // Flip X
                charObj->flipX = !charObj->flipX;
                ghostObj->flipX = charObj->flipX;
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 11) { // Antialiasing
                charObj->noAntialiasing = !charObj->noAntialiasing;
                ghostObj->noAntialiasing = charObj->noAntialiasing;
                charObj->setAntialiasing(!charObj->noAntialiasing);
                ghostObj->setAntialiasing(!ghostObj->noAntialiasing);
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 12) { // Pos X
                charObj->x += pressLeft ? -change : change;
                charObj->baseX = charObj->x;
                ghostObj->x = charObj->x;
                ghostObj->baseX = charObj->x;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 13) { // Pos Y
                charObj->y += pressLeft ? -change : change;
                charObj->baseY = charObj->y;
                ghostObj->y = charObj->y;
                ghostObj->baseY = charObj->y;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 14) { // Cam Offset X
                charObj->camOffsetX += pressLeft ? -change * 5.0f : change * 5.0f;
                ghostObj->camOffsetX = charObj->camOffsetX;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 15) { // Cam Offset Y
                charObj->camOffsetY += pressLeft ? -change * 5.0f : change * 5.0f;
                ghostObj->camOffsetY = charObj->camOffsetY;
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 16) { // Animation selection list
                if (!animList.empty()) {
                    if (pressLeft) {
                        animSliderIndex--;
                        if (animSliderIndex < 0) animSliderIndex = animList.size() - 1;
                    } else {
                        animSliderIndex++;
                        if (animSliderIndex >= (int)animList.size()) animSliderIndex = 0;
                    }
                    curAnimIndex = animSliderIndex;
                    playCurAnim();
                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                }
            } else if (optId == 17 && !animList.empty()) { // FPS
                std::string curAnimName = animList[animSliderIndex];
                charObj->animations[curAnimName].fps += pressLeft ? -1 : 1;
                if (charObj->animations[curAnimName].fps < 1) charObj->animations[curAnimName].fps = 1;
                ghostObj->animations[curAnimName].fps = charObj->animations[curAnimName].fps;
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 18 && !animList.empty()) { // Loop
                std::string curAnimName = animList[animSliderIndex];
                charObj->setAnimLoop(curAnimName, !charObj->animations[curAnimName].loop);
                ghostObj->setAnimLoop(curAnimName, charObj->animations[curAnimName].loop);
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 19 && !animList.empty()) { // Offset X
                std::string curAnimName = animList[animSliderIndex];
                charObj->animations[curAnimName].offsetX += pressLeft ? -change : change;
                ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            } else if (optId == 20 && !animList.empty()) { // Offset Y
                std::string curAnimName = animList[animSliderIndex];
                charObj->animations[curAnimName].offsetY += pressLeft ? -change : change;
                ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                playCurAnim();
                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            }
        }
    }

    // Touch Screen Inputs
    touchPosition touch;
    bool touchHeld = (hidKeysHeld() & KEY_TOUCH);
    if (touchHeld) {
        hidTouchRead(&touch);
    }
    bool touchDown = keyJustPressed(KEY_TOUCH);
    bool touchUp = (!touchHeld && touchHeldLastFrame);

    // Toggle button tap check (expand/collapse UI in PC mode)
    if (touchDown && pcMode) {
        if (touch.px >= 2 && touch.px <= 18 && touch.py >= 0 && touch.py <= 14) {
            uiExpanded = !uiExpanded;
            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
            touchHeld = false;
            touchDown = false;
            touchUp = false;
        }
    }

    if (touchHeld) {
        if (pcMode) {
            if (draggedWindowId != -1) {
                // Drag the window
                for (auto& win : windows) {
                    if (win.id == draggedWindowId) {
                        win.x = touch.px - dragWindowOffsetX;
                        win.y = touch.py - dragWindowOffsetY;
                        
                        // Clamp window to screen bounds so it doesn't get completely lost
                        if (win.x < -win.w + 20.0f) win.x = -win.w + 20.0f;
                        if (win.x > 320.0f - 20.0f) win.x = 320.0f - 20.0f;
                        if (win.y < 0.0f) win.y = 0.0f;
                        if (win.y > 240.0f - 12.0f) win.y = 240.0f - 12.0f;
                        break;
                    }
                }
            } else if (isDraggingCharList) {
                float dy = touch.py - dragStartY;
                charScrollY = dragStartScroll - dy;
                float itemH = 8.5f;
                float boxH = 54.0f;
                float maxScroll = (characterList.size() * itemH) - boxH;
                if (maxScroll < 0.0f) maxScroll = 0.0f;
                if (charScrollY < 0.0f) charScrollY = 0.0f;
                if (charScrollY > maxScroll) charScrollY = maxScroll;
            } else if (isDraggingAnimList) {
                float dy = touch.py - dragStartY;
                animScrollY = dragStartScroll - dy;
                float itemH = 8.5f;
                float boxH = 54.0f;
                float maxScroll = (animList.size() * itemH) - boxH;
                if (maxScroll < 0.0f) maxScroll = 0.0f;
                if (animScrollY < 0.0f) animScrollY = 0.0f;
                if (animScrollY > maxScroll) animScrollY = maxScroll;
            } else {
                // Continuous Slider Dragging
                bool touchingAnyWindow = false;
                if (uiExpanded) {
                    auto layout = getPCLayout(windows);
                    for (const auto& elem : layout) {
                        if (elem.id == 7) { // Ghost Alpha Slider
                            float sliderX = elem.x + 4.0f;
                            float sliderW = elem.w - 8.0f;
                            if (touch.px >= elem.x && touch.px <= elem.x + elem.w && touch.py >= elem.y && touch.py <= elem.y + elem.h) {
                                touchingAnyWindow = true;
                                float pct = (touch.px - sliderX) / sliderW;
                                if (pct < 0.0f) pct = 0.0f;
                                if (pct > 1.0f) pct = 1.0f;
                                ghostAlpha = pct;
                                ghostObj->alpha = ghostAlpha;
                            }
                        } else {
                            if (touch.px >= elem.x && touch.px <= elem.x + elem.w && touch.py >= elem.y && touch.py <= elem.y + elem.h) {
                                touchingAnyWindow = true;
                            }
                        }
                    }
                    for (const auto& win : windows) {
                        float winH = win.expanded ? win.h : 12.0f;
                        if (touch.px >= win.x && touch.px <= win.x + win.w && touch.py >= win.y && touch.py <= win.y + winH) {
                            touchingAnyWindow = true;
                        }
                    }
                }

                if (!touchingAnyWindow) {
                    if (lastTouchX != -1) {
                        float dx = touch.px - lastTouchX;
                        float dy = touch.py - lastTouchY;
                        
                        if (hidKeysHeld() & KEY_Y) {
                            camX -= dx / camZoom;
                            camY -= dy / camZoom;
                        } else if (charObj && !animList.empty()) {
                            std::string curAnimName = animList[curAnimIndex];
                            float scaleFactor = charObj->charScale * (240.0f / 720.0f) * camZoom;
                            if (scaleFactor > 0.01f) {
                                float dOffX = dx / scaleFactor;
                                float dOffY = dy / scaleFactor;
                                
                                bool shouldFlip = (charObj->isPlayer != charObj->flipX);
                                if (shouldFlip) {
                                    charObj->animations[curAnimName].offsetX += dOffX;
                                } else {
                                    charObj->animations[curAnimName].offsetX -= dOffX;
                                }
                                charObj->animations[curAnimName].offsetY -= dOffY;
                                
                                if (curAnimIndex == curGhostAnimIndex) {
                                    ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                                    ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                                }
                                playCurAnim();
                            }
                        }
                    }
                    lastTouchX = touch.px;
                    lastTouchY = touch.py;
                } else {
                    lastTouchX = -1;
                    lastTouchY = -1;
                }
            }
        } 
        else if (uiExpanded || !pcMode) {
            lastTouchX = -1;
            lastTouchY = -1;

            float boxX = 10.0f;
            float boxY = 32.0f;
            float boxW = 300.0f;
            float boxH = 80.0f;
            float itemH = 16.0f;

            if (touchDown) {
                // 1. Tab Headers (Normal Mode Only)
                if (touch.py >= 0 && touch.py <= 24) {
                    float tabW = 80.0f;
                    int newTab = (int)(touch.px / tabW);
                    if (newTab >= 0 && newTab <= 3) {
                        currentTab = newTab;
                        curSelected = 0;
                        isDraggingCharList = false;
                        isDraggingAnimList = false;
                        AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                    }
                }
                // 2. Drag Start for Lists (Normal Mode Only)
                else if (currentTab == 0 && touch.px >= boxX && touch.px <= boxX + boxW && touch.py >= boxY && touch.py <= boxY + boxH) {
                    dragStartY = touch.py;
                    dragStartScroll = charScrollY;
                    isDraggingCharList = true;
                    curSelected = 0;
                }
                else if (currentTab == 3 && touch.px >= boxX && touch.px <= boxX + boxW && touch.py >= boxY && touch.py <= boxY + boxH) {
                    dragStartY = touch.py;
                    dragStartScroll = animScrollY;
                    isDraggingAnimList = true;
                    curSelected = 0;
                }
                
                touchStartPx = touch.px;
                touchStartPy = touch.py;
            }

            if (currentTab == 1) {
                float sliderX = 20.0f;
                float sliderW = 280.0f;
                float sliderY = 120.0f;
                if (touch.py >= sliderY - 10.0f && touch.py <= sliderY + 24.0f) {
                    curSelected = 3;
                    float pct = (touch.px - sliderX) / sliderW;
                    if (pct < 0.0f) pct = 0.0f;
                    if (pct > 1.0f) pct = 1.0f;
                    ghostAlpha = pct;
                    ghostObj->alpha = ghostAlpha;
                }
            }

            if (isDraggingCharList) {
                float dy = touch.py - dragStartY;
                charScrollY = dragStartScroll - dy;
                float maxScroll = (characterList.size() * itemH) - boxH;
                if (maxScroll < 0.0f) maxScroll = 0.0f;
                if (charScrollY < 0.0f) charScrollY = 0.0f;
                if (charScrollY > maxScroll) charScrollY = maxScroll;
            }
            else if (isDraggingAnimList) {
                float dy = touch.py - dragStartY;
                animScrollY = dragStartScroll - dy;
                float maxScroll = (animList.size() * itemH) - boxH;
                if (maxScroll < 0.0f) maxScroll = 0.0f;
                if (animScrollY < 0.0f) animScrollY = 0.0f;
                if (animScrollY > maxScroll) animScrollY = maxScroll;
            }
        }
    } else {
        lastTouchX = -1;
        lastTouchY = -1;
    }

    // Touch Down Processing for PC Mode (Focus and Drag start)
    if (touchDown && pcMode && uiExpanded) {
        for (int i = (int)windows.size() - 1; i >= 0; i--) {
            auto& win = windows[i];
            float winH = win.expanded ? win.h : 12.0f;
            if (touch.px >= win.x && touch.px <= win.x + win.w && touch.py >= win.y && touch.py <= win.y + winH) {
                // Focus: Bring window to front
                if (i < (int)windows.size() - 1) {
                    UIWindow temp = win;
                    windows.erase(windows.begin() + i);
                    windows.push_back(temp);
                }
                auto& focusedWin = windows.back();

                if (touch.py <= focusedWin.y + 12.0f) {
                    // Clicked title bar
                    // Check collapse button (right 12px)
                    if (touch.px >= focusedWin.x + focusedWin.w - 12.0f) {
                        focusedWin.expanded = !focusedWin.expanded;
                        AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                    } else {
                        // Start dragging window
                        draggedWindowId = focusedWin.id;
                        dragWindowOffsetX = touch.px - focusedWin.x;
                        dragWindowOffsetY = touch.py - focusedWin.y;
                    }
                } else {
                    // Clicked inside body - check listboxes start drag
                    auto layout = getPCLayout(windows);
                    for (const auto& elem : layout) {
                        if (elem.id == 0 && touch.px >= elem.x && touch.px <= elem.x + elem.w && touch.py >= elem.y && touch.py <= elem.y + elem.h) {
                            dragStartY = touch.py;
                            dragStartScroll = charScrollY;
                            isDraggingCharList = true;
                            auto it = std::find(visibleOptionIds.begin(), visibleOptionIds.end(), elem.id);
                            if (it != visibleOptionIds.end()) curSelected = std::distance(visibleOptionIds.begin(), it);
                        } else if (elem.id == 16 && touch.px >= elem.x && touch.px <= elem.x + elem.w && touch.py >= elem.y && touch.py <= elem.y + elem.h) {
                            dragStartY = touch.py;
                            dragStartScroll = animScrollY;
                            isDraggingAnimList = true;
                            auto it = std::find(visibleOptionIds.begin(), visibleOptionIds.end(), elem.id);
                            if (it != visibleOptionIds.end()) curSelected = std::distance(visibleOptionIds.begin(), it);
                        }
                    }
                }
                
                touchStartPx = touch.px;
                touchStartPy = touch.py;
                break;
            }
        }
    }

    // Touch Up: Gesture tap detection
    if (touchUp) {
        float boxX = 10.0f;
        float boxY = 32.0f;
        float boxW = 300.0f;
        float boxH = 80.0f;
        float itemH = 16.0f;

        draggedWindowId = -1;
        isDraggingCharList = false;
        isDraggingAnimList = false;

        float dx = touchStartPx - touch.px;
        float dy = touchStartPy - touch.py;
        
        // Tap gesture confirmed if stylus moved less than 5 pixels
        if (dx * dx + dy * dy < 25.0f && touchStartPy >= 0.0f && touchStartPy < 240.0f && (!pcMode || uiExpanded)) {
            if (pcMode) {
                auto layout = getPCLayout(windows);
                for (const auto& elem : layout) {
                    if (touchStartPx >= elem.x && touchStartPx <= elem.x + elem.w && touchStartPy >= elem.y && touchStartPy <= elem.y + elem.h) {
                        if (elem.id != -1) {
                            // Tapped a control element
                            // Set curSelected index to sync selection
                            auto it = std::find(visibleOptionIds.begin(), visibleOptionIds.end(), elem.id);
                            if (it != visibleOptionIds.end()) {
                                curSelected = std::distance(visibleOptionIds.begin(), it);
                            }
                            
                            // Process action for this control
                            if (elem.id == 0) { // Character listbox
                                int tapped = (int)((touchStartPy - elem.y + charScrollY) / 8.5f);
                                if (tapped >= 0 && tapped < (int)characterList.size()) {
                                    curCharIndex = tapped;
                                    loadCharacter(characterList[curCharIndex]);
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                }
                            } else if (elem.id == 1) { // Playable Checkbox
                                charObj->isPlayer = !charObj->isPlayer;
                                ghostObj->isPlayer = charObj->isPlayer;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (elem.id == 2) { // Reload Button
                                loadCharacter(currentCharacter);
                                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                            } else if (elem.id == 3) { // Save Button
                                saveCharacter();
                                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                            } else if (elem.id == 4) { // Make Ghost Button
                                ghostObj->isPlayer = charObj->isPlayer;
                                ghostObj->flipX = charObj->flipX;
                                ghostObj->charScale = charObj->charScale;
                                ghostObj->charScaleX = charObj->charScale;
                                ghostObj->charScaleY = charObj->charScale;
                                if (ghostObj->isSpritemap) {
                                    ghostObj->spritemapAnim.scaleX = charObj->charScale;
                                    ghostObj->spritemapAnim.scaleY = charObj->charScale;
                                }
                                ghostObj->animations = charObj->animations;
                                ghostObj->playAnim(charObj->curAnim, true);
                                ghostObj->curFrame = charObj->curFrame;
                                ghostObj->frameTimer = charObj->frameTimer;
                                ghostObj->animFinished = charObj->animFinished;
                                ghostObj->spritemapAnim.smLogicalFrame = charObj->spritemapAnim.smLogicalFrame;
                                ghostObj->spritemapAnim.animFinished = charObj->spritemapAnim.animFinished;
                                AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                            } else if (elem.id == 5) { // Show Ghost Checkbox
                                showGhost = !showGhost;
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (elem.id == 6) { // Highlight Ghost Checkbox
                                ghostHighlight = !ghostHighlight;
                                ghostObj->isHighlighted = ghostHighlight;
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (elem.id == 10) { // Flip X Checkbox
                                charObj->flipX = !charObj->flipX;
                                ghostObj->flipX = charObj->flipX;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (elem.id == 11) { // Antialiasing Checkbox
                                charObj->noAntialiasing = !charObj->noAntialiasing;
                                ghostObj->noAntialiasing = charObj->noAntialiasing;
                                charObj->setAntialiasing(!charObj->noAntialiasing);
                                ghostObj->setAntialiasing(!ghostObj->noAntialiasing);
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (elem.id == 16) { // Animations listbox
                                int tapped = (int)((touchStartPy - elem.y + animScrollY) / 8.5f);
                                if (tapped >= 0 && tapped < (int)animList.size()) {
                                    animSliderIndex = tapped;
                                    curAnimIndex = animSliderIndex;
                                    playCurAnim();
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                }
                            } else if (elem.id == 18) { // Loop Checkbox
                                if (!animList.empty()) {
                                    std::string curAnimName = animList[animSliderIndex];
                                    charObj->setAnimLoop(curAnimName, !charObj->animations[curAnimName].loop);
                                    ghostObj->setAnimLoop(curAnimName, charObj->animations[curAnimName].loop);
                                    playCurAnim();
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                }
                            } else {
                                // Arrows adjusted elements
                                float arrowL = elem.x + elem.w - 32.0f;
                                float arrowR = elem.x + elem.w - 12.0f;
                                bool isLeft = (touchStartPx >= arrowL && touchStartPx <= arrowL + 12.0f);
                                bool isRight = (touchStartPx >= arrowR && touchStartPx <= arrowR + 12.0f);
                                if (isLeft || isRight) {
                                    float sign = isLeft ? -1.0f : 1.0f;
                                    if (elem.id == 8) { // Sing Length
                                        charObj->singDuration += sign * 0.5f;
                                        if (charObj->singDuration < 0.1f) charObj->singDuration = 0.1f;
                                        ghostObj->singDuration = charObj->singDuration;
                                    } else if (elem.id == 9) { // Scale
                                        charObj->charScale += sign * change * 0.1f;
                                        if (charObj->charScale < 0.1f) charObj->charScale = 0.1f;
                                        charObj->charScaleX = charObj->charScale;
                                        charObj->charScaleY = charObj->charScale;
                                        if (charObj->isSpritemap) {
                                            charObj->spritemapAnim.scaleX = charObj->charScale;
                                            charObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        ghostObj->charScale = charObj->charScale;
                                        ghostObj->charScaleX = charObj->charScale;
                                        ghostObj->charScaleY = charObj->charScale;
                                        if (ghostObj->isSpritemap) {
                                            ghostObj->spritemapAnim.scaleX = charObj->charScale;
                                            ghostObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        playCurAnim();
                                    } else if (elem.id == 12) { // Pos X
                                        charObj->x += sign * change;
                                        charObj->baseX = charObj->x;
                                        ghostObj->x = charObj->x;
                                        ghostObj->baseX = charObj->x;
                                    } else if (elem.id == 13) { // Pos Y
                                        charObj->y += sign * change;
                                        charObj->baseY = charObj->y;
                                        ghostObj->y = charObj->y;
                                        ghostObj->baseY = charObj->y;
                                    } else if (elem.id == 14) { // Cam Off X
                                        charObj->camOffsetX += sign * change * 5.0f;
                                        ghostObj->camOffsetX = charObj->camOffsetX;
                                    } else if (elem.id == 15) { // Cam Off Y
                                        charObj->camOffsetY += sign * change * 5.0f;
                                        ghostObj->camOffsetY = charObj->camOffsetY;
                                    } else if (elem.id == 17 && !animList.empty()) { // FPS
                                        std::string curAnimName = animList[animSliderIndex];
                                        charObj->animations[curAnimName].fps += (int)sign;
                                        if (charObj->animations[curAnimName].fps < 1) charObj->animations[curAnimName].fps = 1;
                                        ghostObj->animations[curAnimName].fps = charObj->animations[curAnimName].fps;
                                        playCurAnim();
                                    } else if (elem.id == 19 && !animList.empty()) { // Offset X
                                        std::string curAnimName = animList[animSliderIndex];
                                        charObj->animations[curAnimName].offsetX += sign * change;
                                        ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                                        playCurAnim();
                                    } else if (elem.id == 20 && !animList.empty()) { // Offset Y
                                        std::string curAnimName = animList[animSliderIndex];
                                        charObj->animations[curAnimName].offsetY += sign * change;
                                        ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                                        playCurAnim();
                                    }
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                }
                            }
                        }
                        break;
                    }
                }
            } else {
                // Original 3DS Tab tap detection logic (Normal Mode) - Improved for touch sensitivity
                if (currentTab == 0) { // Settings
                    // Tapped Character List Box
                    if (touchStartPx >= boxX && touchStartPx <= boxX + boxW && touchStartPy >= boxY && touchStartPy <= boxY + boxH) {
                        int tapped = (int)((touchStartPy - boxY + charScrollY) / itemH);
                        if (tapped >= 0 && tapped < (int)characterList.size()) {
                            curCharIndex = tapped;
                            loadCharacter(characterList[curCharIndex]);
                            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                        }
                    }
                    // Playable Checkbox (forgiving row height tap)
                    float checkY = 120.0f;
                    if (touchStartPy >= checkY - 5.0f && touchStartPy <= checkY + 20.0f) {
                        curSelected = 1;
                        charObj->isPlayer = !charObj->isPlayer;
                        ghostObj->isPlayer = charObj->isPlayer;
                        playCurAnim();
                        AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                    }
                    // Reload Button
                    float btnY = 145.0f;
                    float btnH = 20.0f;
                    if (touchStartPy >= btnY - 2.0f && touchStartPy <= btnY + btnH + 2.0f && touchStartPx >= 10 && touchStartPx <= 310) {
                        curSelected = 2;
                        loadCharacter(currentCharacter);
                        AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                    }
                    // Save Button
                    float saveY = 175.0f;
                    float saveH = 20.0f;
                    if (touchStartPy >= saveY - 2.0f && touchStartPy <= saveY + saveH + 2.0f && touchStartPx >= 10 && touchStartPx <= 310) {
                        curSelected = 3;
                        saveCharacter();
                        AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                    }
                }
                else if (currentTab == 1) { // Ghost
                    // Make Ghost Button
                    float btnY = 32.0f;
                    float btnH = 20.0f;
                    if (touchStartPy >= btnY - 2.0f && touchStartPy <= btnY + btnH + 2.0f && touchStartPx >= 10 && touchStartPx <= 310) {
                        curSelected = 0;
                        ghostObj->isPlayer = charObj->isPlayer;
                        ghostObj->flipX = charObj->flipX;
                        ghostObj->charScale = charObj->charScale;
                        ghostObj->charScaleX = charObj->charScale;
                        ghostObj->charScaleY = charObj->charScale;
                        if (ghostObj->isSpritemap) {
                            ghostObj->spritemapAnim.scaleX = charObj->charScale;
                            ghostObj->spritemapAnim.scaleY = charObj->charScale;
                        }
                        ghostObj->animations = charObj->animations;
                        ghostObj->playAnim(charObj->curAnim, true);
                        ghostObj->curFrame = charObj->curFrame;
                        ghostObj->frameTimer = charObj->frameTimer;
                        ghostObj->animFinished = charObj->animFinished;
                        ghostObj->spritemapAnim.smLogicalFrame = charObj->spritemapAnim.smLogicalFrame;
                        ghostObj->spritemapAnim.animFinished = charObj->spritemapAnim.animFinished;
                        AudioEngine::playSound("romfs:/preload/sounds/confirmMenu.ogg", 0.5f);
                    }
                    // Show Ghost Checkbox (forgiving row height)
                    float checkShowY = 62.0f;
                    if (touchStartPy >= checkShowY - 5.0f && touchStartPy <= checkShowY + 18.0f) {
                        curSelected = 1;
                        showGhost = !showGhost;
                        AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                    }
                    // Highlight Ghost Checkbox (forgiving row height)
                    float checkHighY = 82.0f;
                    if (touchStartPy >= checkHighY - 5.0f && touchStartPy <= checkHighY + 18.0f) {
                        curSelected = 2;
                        ghostHighlight = !ghostHighlight;
                        ghostObj->isHighlighted = ghostHighlight;
                        AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                    }
                }
                else if (currentTab == 2) { // Character
                    for (int i = 0; i < 8; i++) {
                        float itemY = 32.0f + i * 20.0f;
                        float itemH = 16.0f;
                        if (touchStartPy >= itemY - 2.0f && touchStartPy <= itemY + itemH + 2.0f) {
                            curSelected = i;
                            if (i == 2) { // Flip X
                                charObj->flipX = !charObj->flipX;
                                ghostObj->flipX = charObj->flipX;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (i == 3) { // Antialiasing
                                charObj->noAntialiasing = !charObj->noAntialiasing;
                                ghostObj->noAntialiasing = charObj->noAntialiasing;
                                charObj->setAntialiasing(!charObj->noAntialiasing);
                                ghostObj->setAntialiasing(!ghostObj->noAntialiasing);
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else {
                                float arrowL = 200.0f;
                                float arrowR = 280.0f;
                                if (touchStartPx >= arrowL - 10.0f && touchStartPx <= arrowL + 25.0f) { // Left arrow
                                    if (i == 0) {
                                        charObj->singDuration -= 0.5f;
                                        if (charObj->singDuration < 0.1f) charObj->singDuration = 0.1f;
                                        ghostObj->singDuration = charObj->singDuration;
                                    } else if (i == 1) {
                                        charObj->charScale -= 0.1f;
                                        if (charObj->charScale < 0.1f) charObj->charScale = 0.1f;
                                        charObj->charScaleX = charObj->charScale;
                                        charObj->charScaleY = charObj->charScale;
                                        if (charObj->isSpritemap) {
                                            charObj->spritemapAnim.scaleX = charObj->charScale;
                                            charObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        ghostObj->charScale = charObj->charScale;
                                        ghostObj->charScaleX = charObj->charScale;
                                        ghostObj->charScaleY = charObj->charScale;
                                        if (ghostObj->isSpritemap) {
                                            ghostObj->spritemapAnim.scaleX = charObj->charScale;
                                            ghostObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        playCurAnim();
                                    } else if (i == 4) {
                                        charObj->x -= change;
                                        charObj->baseX = charObj->x;
                                        ghostObj->x = charObj->x;
                                        ghostObj->baseX = charObj->x;
                                    } else if (i == 5) {
                                        charObj->y -= change;
                                        charObj->baseY = charObj->y;
                                        ghostObj->y = charObj->y;
                                        ghostObj->baseY = charObj->y;
                                    } else if (i == 6) {
                                        charObj->camOffsetX -= change * 5.0f;
                                        ghostObj->camOffsetX = charObj->camOffsetX;
                                    } else if (i == 7) {
                                        charObj->camOffsetY -= change * 5.0f;
                                        ghostObj->camOffsetY = charObj->camOffsetY;
                                    }
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                } else if (touchStartPx >= arrowR - 10.0f && touchStartPx <= arrowR + 25.0f) { // Right arrow
                                    if (i == 0) {
                                        charObj->singDuration += 0.5f;
                                        ghostObj->singDuration = charObj->singDuration;
                                    } else if (i == 1) {
                                        charObj->charScale += 0.1f;
                                        charObj->charScaleX = charObj->charScale;
                                        charObj->charScaleY = charObj->charScale;
                                        if (charObj->isSpritemap) {
                                            charObj->spritemapAnim.scaleX = charObj->charScale;
                                            charObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        ghostObj->charScale = charObj->charScale;
                                        ghostObj->charScaleX = charObj->charScale;
                                        ghostObj->charScaleY = charObj->charScale;
                                        if (ghostObj->isSpritemap) {
                                            ghostObj->spritemapAnim.scaleX = charObj->charScale;
                                            ghostObj->spritemapAnim.scaleY = charObj->charScale;
                                        }
                                        playCurAnim();
                                    } else if (i == 4) {
                                        charObj->x += change;
                                        charObj->baseX = charObj->x;
                                        ghostObj->x = charObj->x;
                                        ghostObj->baseX = charObj->x;
                                    } else if (i == 5) {
                                        charObj->y += change;
                                        charObj->baseY = charObj->y;
                                        ghostObj->y = charObj->y;
                                        ghostObj->baseY = charObj->y;
                                    } else if (i == 6) {
                                        charObj->camOffsetX += change * 5.0f;
                                        ghostObj->camOffsetX = charObj->camOffsetX;
                                    } else if (i == 7) {
                                        charObj->camOffsetY += change * 5.0f;
                                        ghostObj->camOffsetY = charObj->camOffsetY;
                                    }
                                    AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                                }
                            }
                            break;
                        }
                    }
                }
                else if (currentTab == 3) { // Animations
                    // Tapped Animations List Box
                    if (touchStartPx >= boxX && touchStartPx <= boxX + boxW && touchStartPy >= boxY && touchStartPy <= boxY + boxH) {
                        int tapped = (int)((touchStartPy - boxY + animScrollY) / itemH);
                        if (tapped >= 0 && tapped < (int)animList.size()) {
                            animSliderIndex = tapped;
                            curAnimIndex = animSliderIndex;
                            playCurAnim();
                            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                        }
                    }
                    
                    if (!animList.empty()) {
                        std::string curAnimName = animList[animSliderIndex];
                        float arrowL = 200.0f;
                        float arrowR = 280.0f;

                        // FPS
                        float fpsY = 138.0f;
                        if (touchStartPy >= fpsY - 5.0f && touchStartPy <= fpsY + 18.0f) {
                            curSelected = 1;
                            if (touchStartPx >= arrowL - 10.0f && touchStartPx <= arrowL + 25.0f) {
                                charObj->animations[curAnimName].fps--;
                                if (charObj->animations[curAnimName].fps < 1) charObj->animations[curAnimName].fps = 1;
                                ghostObj->animations[curAnimName].fps = charObj->animations[curAnimName].fps;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (touchStartPx >= arrowR - 10.0f && touchStartPx <= arrowR + 25.0f) {
                                charObj->animations[curAnimName].fps++;
                                ghostObj->animations[curAnimName].fps = charObj->animations[curAnimName].fps;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            }
                        }
                        // Loop (forgiving row height)
                        else if (touchStartPy >= 150.0f && touchStartPy <= 170.0f) {
                            curSelected = 2;
                            charObj->setAnimLoop(curAnimName, !charObj->animations[curAnimName].loop);
                            ghostObj->setAnimLoop(curAnimName, charObj->animations[curAnimName].loop);
                            playCurAnim();
                            AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                        }
                        // Offset X
                        float offXY = 174.0f;
                        if (touchStartPy >= offXY - 5.0f && touchStartPy <= offXY + 18.0f) {
                            curSelected = 3;
                            if (touchStartPx >= arrowL - 10.0f && touchStartPx <= arrowL + 25.0f) {
                                charObj->animations[curAnimName].offsetX -= change;
                                ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (touchStartPx >= arrowR - 10.0f && touchStartPx <= arrowR + 25.0f) {
                                charObj->animations[curAnimName].offsetX += change;
                                ghostObj->animations[curAnimName].offsetX = charObj->animations[curAnimName].offsetX;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            }
                        }
                        // Offset Y
                        float offYY = 192.0f;
                        if (touchStartPy >= offYY - 5.0f && touchStartPy <= offYY + 18.0f) {
                            curSelected = 4;
                            if (touchStartPx >= arrowL - 10.0f && touchStartPx <= arrowL + 25.0f) {
                                charObj->animations[curAnimName].offsetY -= change;
                                ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            } else if (touchStartPx >= arrowR - 10.0f && touchStartPx <= arrowR + 25.0f) {
                                charObj->animations[curAnimName].offsetY += change;
                                ghostObj->animations[curAnimName].offsetY = charObj->animations[curAnimName].offsetY;
                                playCurAnim();
                                AudioEngine::playSound("romfs:/preload/sounds/scrollMenu.ogg", 0.4f);
                            }
                        }
                }
            }
        }
    }
    }

    touchHeldLastFrame = touchHeld;
}

void CharacterEditorState::saveCharacter() {
    if (charObj && !currentCharacter.empty()) {
        std::string path = "sdmc:/SnakeEngine/characters/" + currentCharacter + ".json";
        charObj->saveToPsychJson(path);
        saveMessageTimer = 3.0f;
    }
}

CharacterEditorState::~CharacterEditorState() {
    if (charObj) delete charObj;
    if (ghostObj) delete ghostObj;
    if (textBuf) C2D_TextBufDelete(textBuf);
}

void CharacterEditorState::init() {
    MusicBeatState::init();
    textBuf = C2D_TextBufNew(4096);
    loadCharacter(currentCharacter);
}

void CharacterEditorState::draw(C3D_RenderTarget* top, C3D_RenderTarget* bottom) {
    C2D_TextBufClear(textBuf);

    // 1. TOP SCREEN: Draw Character, static crosshair, and camera target scope
    C2D_SceneBegin(top);
    C2D_TargetClear(top, C2D_Color32(50, 50, 55, 255));

    // Screen Center crosshair
    C2D_DrawLine(200, 0, C2D_Color32(255, 0, 0, 80), 200, 240, C2D_Color32(255, 0, 0, 80), 1.0f, 0.0f);
    C2D_DrawLine(0, 120, C2D_Color32(255, 0, 0, 80), 400, 120, C2D_Color32(255, 0, 0, 80), 1.0f, 0.0f);

    float cx = 200 - camX;
    float cy = 120 - camY;

    if (showGhost && ghostObj) {
        ghostObj->draw(cx, cy, 0.5f, camZoom, 0, 0);
    }
    if (charObj) {
        charObj->draw(cx, cy, 0.6f, camZoom, 0, 0);
    }

    float screenScale = 240.0f / 720.0f;
    
    // Draw Camera Follow Target Scope (green circular target)
    if (charObj) {
        float followBaseX = (200.0f - camX + charObj->x + 150.0f + charObj->camOffsetX);
        float followBaseY = (120.0f - camY + charObj->y + 150.0f + charObj->camOffsetY);
        float screenFollowX = (followBaseX * screenScale * camZoom) + 200.0f;
        float screenFollowY = (followBaseY * screenScale * camZoom) + 120.0f;
        
        C2D_DrawCircleSolid(screenFollowX, screenFollowY, 0.9f, 2.0f, C2D_Color32(0, 255, 0, 255));
        C2D_DrawLine(screenFollowX - 8, screenFollowY, C2D_Color32(0, 255, 0, 200), screenFollowX + 8, screenFollowY, C2D_Color32(0, 255, 0, 200), 1.0f, 0.9f);
        C2D_DrawLine(screenFollowX, screenFollowY - 8, C2D_Color32(0, 255, 0, 200), screenFollowX, screenFollowY + 8, C2D_Color32(0, 255, 0, 200), 1.0f, 0.9f);
    }

    // TOP HUD: No offsets text!
    char topHUD[128];
    snprintf(topHUD, sizeof(topHUD), "Zoom: %.2fx | Cam: (%d, %d)", camZoom, (int)camX, (int)camY);

    C2D_Text topInfo;
    C2D_TextParse(&topInfo, textBuf, topHUD);
    C2D_TextOptimize(&topInfo);
    C2D_DrawText(&topInfo, C2D_WithColor, 6.0f, 6.0f, 0.95f, 0.45f, 0.45f, C2D_Color32(230, 230, 235, 200));


    // 2. BOTTOM SCREEN: Tab UI or PC Mode Split
    C2D_SceneBegin(bottom);
    C2D_TargetClear(bottom, C2D_Color32(18, 18, 22, 255));

    float panelW = pcMode ? (uiExpanded ? 110.0f : 0.0f) : 320.0f;

    if (pcMode) {
        // Draw Character Preview fullscreen behind windows
        float previewX = 0.0f;
        float previewW = 320.0f;
        float previewCenterX = 160.0f;

        C2D_DrawRectSolid(previewX, 0.0f, 0.1f, previewW, 240.0f, C2D_Color32(50, 50, 55, 255));

        // Subtle grid lines in preview area (behind character)
        for (float gx = 0.0f; gx <= 320.0f; gx += 20.0f) {
            C2D_DrawLine(gx, 14.0f, C2D_Color32(70, 70, 82, 45), gx, 226.0f, C2D_Color32(70, 70, 82, 45), 1.0f, 0.15f);
        }
        for (float gy = 34.0f; gy <= 226.0f; gy += 20.0f) {
            C2D_DrawLine(0.0f, gy, C2D_Color32(70, 70, 82, 45), 320.0f, gy, C2D_Color32(70, 70, 82, 45), 1.0f, 0.15f);
        }

        // Crosshair (subtler in PC mode, clipped to content area)
        C2D_DrawLine(previewCenterX, 14.0f, C2D_Color32(255, 0, 0, 55), previewCenterX, 226.0f, C2D_Color32(255, 0, 0, 55), 1.0f, 0.2f);
        C2D_DrawLine(0.0f, 120.0f, C2D_Color32(255, 0, 0, 55), 320.0f, 120.0f, C2D_Color32(255, 0, 0, 55), 1.0f, 0.2f);

        float pcCx = previewCenterX - camX;
        float pcCy = 120.0f - camY;

        if (showGhost && ghostObj) {
            ghostObj->draw(pcCx, pcCy, 0.3f, camZoom, 0, 0);
        }
        if (charObj) {
            charObj->draw(pcCx, pcCy, 0.4f, camZoom, 0, 0);
        }

        // Draw camera follow scope in PC preview window
        if (charObj) {
            float pcFollowX = ((previewCenterX - camX + charObj->x + 150.0f + charObj->camOffsetX) * screenScale * camZoom) + previewCenterX;
            float pcFollowY = ((120.0f - camY + charObj->y + 150.0f + charObj->camOffsetY) * screenScale * camZoom) + 120.0f;
            C2D_DrawCircleSolid(pcFollowX, pcFollowY, 0.9f, 2.0f, C2D_Color32(0, 255, 0, 255));
            C2D_DrawLine(pcFollowX - 6, pcFollowY, C2D_Color32(0, 255, 0, 200), pcFollowX + 6, pcFollowY, C2D_Color32(0, 255, 0, 200), 1.0f, 0.9f);
            C2D_DrawLine(pcFollowX, pcFollowY - 6, C2D_Color32(0, 255, 0, 200), pcFollowX, pcFollowY + 6, C2D_Color32(0, 255, 0, 200), 1.0f, 0.9f);
        }

        // Animation offset overlay (shown over character, like Psych Engine PC)
        if (charObj && !animList.empty()) {
            float ovX = 4.0f;
            float ovY = 17.0f;
            float ovScale = 0.22f;
            float ovLineH = 9.5f;
            int maxVisible = (int)((224.0f - ovY) / ovLineH);
            if (maxVisible < 1) maxVisible = 1;
            int count = (int)animList.size();
            int showCount = count < maxVisible ? count : maxVisible;

            C2D_DrawRectSolid(ovX - 2.0f, ovY - 1.0f, 0.44f, 84.0f, showCount * ovLineH + 3.0f, C2D_Color32(0, 0, 0, 110));

            for (int ai = 0; ai < showCount; ai++) {
                const std::string& aName = animList[ai];
                float ox = charObj->animations[aName].offsetX;
                float oy = charObj->animations[aName].offsetY;

                std::string displayName = (aName.length() > 9) ? (aName.substr(0, 8) + "~") : aName;
                char animLine[40];
                snprintf(animLine, sizeof(animLine), "%s: [%d,%d]", displayName.c_str(), (int)ox, (int)oy);

                u32 animLineCol = (ai == curAnimIndex)
                    ? C2D_Color32(0, 220, 220, 255)
                    : C2D_Color32(200, 200, 210, 180);

                C2D_Text animLineT;
                C2D_TextParse(&animLineT, textBuf, animLine);
                C2D_TextOptimize(&animLineT);
                C2D_DrawText(&animLineT, C2D_WithColor, ovX, ovY + ai * ovLineH, 0.45f, ovScale, ovScale, animLineCol);
            }
        }

        // Preview Header bar: character name + active animation
        C2D_DrawRectSolid(0.0f, 0.0f, 0.62f, 320.0f, 14.0f, C2D_Color32(10, 10, 16, 230));
        C2D_DrawRectSolid(0.0f, 13.0f, 0.63f, 320.0f, 1.0f, C2D_Color32(0, 100, 180, 200));
        {
            std::string headerStr = currentCharacter;
            if (!animList.empty()) {
                std::string animPart = animList[curAnimIndex];
                if (animPart.length() > 12) animPart = animPart.substr(0, 11) + "~";
                headerStr += "  |  " + animPart;
            }
            if (headerStr.length() > 28) headerStr = headerStr.substr(0, 26) + "..";
            C2D_Text headerT;
            C2D_TextParse(&headerT, textBuf, headerStr.c_str());
            C2D_TextOptimize(&headerT);
            float tw = 0, th = 0;
            C2D_TextGetDimensions(&headerT, 0.24f, 0.24f, &tw, &th);
            C2D_DrawText(&headerT, C2D_WithColor, (320.0f - tw) * 0.5f, (14.0f - th) * 0.5f, 0.65f, 0.24f, 0.24f, C2D_Color32(220, 225, 235, 255));
        }

        // Collapse/Expand toggle button at the top-left of the preview header
        C2D_Text toggleT;
        C2D_TextParse(&toggleT, textBuf, uiExpanded ? "<-" : "->");
        C2D_TextOptimize(&toggleT);
        float ttw = 0, tth = 0;
        C2D_TextGetDimensions(&toggleT, 0.24f, 0.24f, &ttw, &tth);
        C2D_DrawRectSolid(3.0f, 1.0f, 0.64f, 16.0f, 12.0f, C2D_Color32(30, 30, 45, 255));
        C2D_DrawText(&toggleT, C2D_WithColor, 3.0f + (16.0f - ttw) * 0.5f, 1.0f + (12.0f - tth) * 0.5f, 0.65f, 0.24f, 0.24f, C2D_Color32(0, 210, 255, 255));

        // Preview Footer bar: frame count + zoom
        C2D_DrawRectSolid(0.0f, 226.0f, 0.62f, 320.0f, 14.0f, C2D_Color32(10, 10, 16, 230));
        C2D_DrawRectSolid(0.0f, 226.0f, 0.63f, 320.0f, 1.0f, C2D_Color32(0, 100, 180, 200));
        {
            char footerStr[48];
            int curFr = charObj ? charObj->curFrame : 0;
            int totalFr = 0;
            if (charObj && !animList.empty()) {
                totalFr = (int)charObj->animations[animList[curAnimIndex]].indices.size();
            }
            if (totalFr > 0) {
                if (curFr < 0) curFr = 0;
                if (curFr >= totalFr) curFr = totalFr - 1;
                snprintf(footerStr, sizeof(footerStr), "Fr:%d/%d  Z:%.2fx", curFr + 1, totalFr, camZoom);
            } else {
                snprintf(footerStr, sizeof(footerStr), "Z:%.2fx  Cam:(%d,%d)", camZoom, (int)camX, (int)camY);
            }
            C2D_Text footerT;
            C2D_TextParse(&footerT, textBuf, footerStr);
            C2D_TextOptimize(&footerT);
            float tw = 0, th = 0;
            C2D_TextGetDimensions(&footerT, 0.24f, 0.24f, &tw, &th);
            C2D_DrawText(&footerT, C2D_WithColor, (320.0f - tw) * 0.5f, 226.0f + (14.0f - th) * 0.5f, 0.65f, 0.24f, 0.24f, C2D_Color32(190, 200, 215, 220));
        }
    }

    // Draw Tab Headers (only in Normal Mode)
    if (!pcMode) {
        float tabW = 80.0f;
        std::string tabLabelsNormal[] = { "Settings", "Ghost", "Character", "Animations" };

        for (int i = 0; i < 4; i++) {
            bool isActive = (i == currentTab);
            u32 tabBg = isActive ? C2D_Color32(0, 110, 220, 255) : C2D_Color32(28, 28, 34, 255);

            C2D_DrawRectSolid(i * tabW, 0.0f, 0.4f, tabW, 24.0f, tabBg);
            C2D_DrawLine(i * tabW, 24.0f, C2D_Color32(50, 50, 65, 255), (i + 1) * tabW, 24.0f, C2D_Color32(50, 50, 65, 255), 1.0f, 0.45f);

            if (isActive) {
                // Accent underline on active tab
                C2D_DrawRectSolid(i * tabW + 1, 21.0f, 0.45f, tabW - 2, 2.5f, C2D_Color32(0, 210, 255, 255));
            }

            C2D_Text labelT;
            C2D_TextParse(&labelT, textBuf, tabLabelsNormal[i].c_str());
            C2D_TextOptimize(&labelT);

            float tw = 0, th = 0;
            C2D_TextGetDimensions(&labelT, 0.35f, 0.35f, &tw, &th);
            C2D_DrawText(&labelT, C2D_WithColor, i * tabW + (tabW - tw) * 0.5f, (24.0f - th) * 0.5f, 0.5f, 0.35f, 0.35f, C2D_Color32(255, 255, 255, 255));
        }
    }

    // UI Helpers (Closure lambdas)
    auto drawCheckbox = [&](float x, float y, bool checked, bool selected) {
        float size = pcMode ? 7.0f : 14.0f;
        float padding = pcMode ? 0.5f : 1.0f;
        u32 borderCol = selected ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(65, 65, 80, 255);
        C2D_DrawRectSolid(x, y, 0.5f, size, size, borderCol);
        C2D_DrawRectSolid(x + padding, y + padding, 0.55f, size - padding * 2, size - padding * 2, C2D_Color32(20, 20, 26, 255));
        if (checked) {
            C2D_DrawRectSolid(x + padding + (pcMode ? 0.5f : 1.0f), y + padding + (pcMode ? 0.5f : 1.0f), 0.6f, size - padding * 2 - (pcMode ? 1.0f : 2.0f), size - padding * 2 - (pcMode ? 1.0f : 2.0f), C2D_Color32(0, 190, 230, 255));
        }
    };

    auto drawSlider = [&](float x, float y, float w, float valPct, bool selected) {
        float lineH = pcMode ? 1.0f : 2.0f;
        float handleW = pcMode ? 4.0f : 8.0f;
        float handleH = pcMode ? 7.0f : 14.0f;
        C2D_DrawRectSolid(x, y + handleH * 0.5f - lineH * 0.5f, 0.5f, w, lineH, C2D_Color32(60, 62, 78, 255));
        C2D_DrawRectSolid(x, y + handleH * 0.5f - lineH * 0.5f, 0.51f, valPct * w, lineH, C2D_Color32(0, 190, 230, 255));
        
        float handleX = x + valPct * (w - handleW);
        u32 handleCol = selected ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(0, 150, 190, 255);
        C2D_DrawRectSolid(handleX, y, 0.55f, handleW, handleH, handleCol);
    };

    auto drawButton = [&](float x, float y, float w, float h, const std::string& label, bool selected) {
        u32 topCol = selected ? C2D_Color32(0, 130, 255, 255) : C2D_Color32(48, 50, 64, 255);
        u32 botCol = selected ? C2D_Color32(0, 90, 200, 255) : C2D_Color32(32, 34, 46, 255);
        C2D_DrawRectangle(x, y, 0.5f, w, h, topCol, topCol, botCol, botCol);
        
        u32 borderCol = selected ? C2D_Color32(0, 180, 255, 255) : C2D_Color32(60, 62, 78, 255);
        C2D_DrawLine(x, y, borderCol, x + w, y, borderCol, 1.0f, 0.52f);
        C2D_DrawLine(x, y, borderCol, x, y + h, borderCol, 1.0f, 0.52f);
        C2D_DrawLine(x + w, y, borderCol, x + w, y + h, borderCol, 1.0f, 0.52f);
        C2D_DrawLine(x, y + h, borderCol, x + w, y + h, borderCol, 1.0f, 0.52f);
        
        C2D_Text btnT;
        C2D_TextParse(&btnT, textBuf, label.c_str());
        C2D_TextOptimize(&btnT);
        float tw = 0, th = 0;
        float textScale = pcMode ? 0.18f : 0.35f;
        C2D_TextGetDimensions(&btnT, textScale, textScale, &tw, &th);
        C2D_DrawText(&btnT, C2D_WithColor, x + (w - tw) * 0.5f, y + (h - th) * 0.5f, 0.6f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
    };

    auto drawArrowsVal = [&](float itemX, float itemY, float itemW, const std::string& label, const std::string& valStr, bool selected) {
        float arrowL = pcMode ? (itemX + itemW - 32.0f) : 200.0f;
        float arrowR = pcMode ? (itemX + itemW - 12.0f) : 280.0f;
        float textScale = pcMode ? 0.18f : 0.35f;

        if (selected) {
            C2D_DrawRectSolid(itemX, itemY, 0.48f, 2.0f, pcMode ? 10.0f : 12.0f, C2D_Color32(255, 170, 0, 255));
        }

        C2D_Text labelT;
        C2D_TextParse(&labelT, textBuf, label.c_str());
        C2D_TextOptimize(&labelT);
        C2D_DrawText(&labelT, C2D_WithColor, itemX + 4.0f, itemY + (pcMode ? 0.5f : 1.0f), 0.5f, textScale, textScale, C2D_Color32(230, 235, 245, 255));
        
        C2D_Text arrL;
        C2D_TextParse(&arrL, textBuf, "<");
        C2D_TextOptimize(&arrL);
        C2D_DrawText(&arrL, C2D_WithColor, arrowL, itemY + (pcMode ? 0.5f : 1.0f), 0.5f, textScale, textScale, selected ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(110, 112, 130, 255));
        
        C2D_Text valT;
        C2D_TextParse(&valT, textBuf, valStr.c_str());
        C2D_TextOptimize(&valT);
        float vw = 0, vh = 0;
        C2D_TextGetDimensions(&valT, textScale, textScale, &vw, &vh);
        float valX = arrowL + (pcMode ? 8.0f : 15.0f) + (arrowR - arrowL - (pcMode ? 8.0f : 15.0f) - vw) * 0.5f;
        C2D_DrawText(&valT, C2D_WithColor, valX, itemY + (pcMode ? 0.5f : 1.0f), 0.5f, textScale, textScale, selected ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(0, 190, 230, 255));
        
        C2D_Text arrR;
        C2D_TextParse(&arrR, textBuf, ">");
        C2D_TextOptimize(&arrR);
        C2D_DrawText(&arrR, C2D_WithColor, arrowR, itemY + (pcMode ? 0.5f : 1.0f), 0.5f, textScale, textScale, selected ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(110, 112, 130, 255));
    };

    auto drawListBox = [&](float bx, float by, float bw, float bh, 
                           const std::vector<std::string>& items, int selectedIdx, 
                           float scrollY, bool activeSelected) {
        C2D_DrawRectSolid(bx, by, 0.45f, bw, bh, C2D_Color32(20, 20, 26, 255));
        u32 borderCol = activeSelected ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(65, 65, 80, 255);
        C2D_DrawRectSolid(bx, by, 0.46f, bw, 1.0f, borderCol);
        C2D_DrawRectSolid(bx, by + bh - 1.0f, 0.46f, bw, 1.0f, borderCol);
        C2D_DrawRectSolid(bx, by, 0.46f, 1.0f, bh, borderCol);
        C2D_DrawRectSolid(bx + bw - 1.0f, by, 0.46f, 1.0f, bh, borderCol);
        
        float ih = pcMode ? 8.5f : 16.0f;
        float ts = pcMode ? 0.18f : 0.38f;
        
        for (size_t i = 0; i < items.size(); i++) {
            float itemY = by + i * ih - scrollY;
            if (itemY + ih <= by || itemY >= by + bh) {
                continue;
            }
            
            bool isSel = (i == (size_t)selectedIdx);
            u32 textCol = isSel ? C2D_Color32(0, 190, 230, 255) : C2D_Color32(220, 225, 235, 255);
            if (isSel) {
                C2D_DrawRectSolid(bx + (pcMode ? 1 : 2), itemY, 0.47f, bw - (pcMode ? 2 : 4), ih, C2D_Color32(0, 120, 220, 70));
            }
            
            C2D_Text itemT;
            C2D_TextParse(&itemT, textBuf, items[i].c_str());
            C2D_TextOptimize(&itemT);
            
            float th = 0;
            C2D_TextGetDimensions(&itemT, ts, ts, nullptr, &th);
            C2D_DrawText(&itemT, C2D_WithColor, bx + (pcMode ? 3.0f : 6.0f), itemY + (ih - th) * 0.5f, 0.48f, ts, ts, textCol);
        }
        
        float totalH = items.size() * ih;
        if (totalH > bh) {
            float barH = (bh / totalH) * bh;
            float barY = by + (scrollY / totalH) * bh;
            C2D_DrawRectSolid(bx + bw - (pcMode ? 2.5f : 4.0f), barY, 0.49f, (pcMode ? 1.0f : 2.0f), barH, C2D_Color32(0, 190, 230, 160));
        }
    };

    // Helper: 1px horizontal separator between UI sections (PC mode only)
    auto drawSectionSep = [&](float y) {
        if (!pcMode) return;
        C2D_DrawRectSolid(0.0f, y, 0.48f, 320.0f, 1.0f, C2D_Color32(45, 47, 60, 255));
    };

    // Helper: 2px left vertical accent bar on selected non-arrow rows
    auto drawLeftAccent = [&](float itemX, float itemY, float h) {
        if (pcMode) {
            C2D_DrawRectSolid(itemX, itemY, 0.48f, 2.0f, h, C2D_Color32(255, 170, 0, 255));
        }
    };

    // Wrap tab/accordion drawing logic
    if (pcMode) {
        if (uiExpanded) {
            // Draw floating windows in back-to-front order (focus order)
            for (const auto& win : windows) {
                float winH = win.expanded ? win.h : 12.0f;
                bool isFocused = (windows.back().id == win.id);

                // 1. Soft drop shadow (offset down and right)
                C2D_DrawRectSolid(win.x + 1.5f, win.y + 1.5f, 0.38f, win.w, winH, C2D_Color32(0, 0, 0, 75));

                // 2. Frosted dark window body
                C2D_DrawRectSolid(win.x, win.y, 0.40f, win.w, winH, C2D_Color32(22, 23, 30, 225));
                
                // 3. Title bar with Vertical Metallic Gradient
                u32 titleTopCol = isFocused ? C2D_Color32(44, 46, 62, 255) : C2D_Color32(32, 33, 40, 255);
                u32 titleBotCol = isFocused ? C2D_Color32(30, 32, 44, 255) : C2D_Color32(22, 23, 28, 255);
                C2D_DrawRectangle(win.x, win.y, 0.45f, win.w, 12.0f, titleTopCol, titleTopCol, titleBotCol, titleBotCol);

                // 4. Accent outline / border
                u32 borderCol = isFocused ? C2D_Color32(255, 170, 0, 255) : C2D_Color32(65, 65, 80, 255);
                C2D_DrawLine(win.x, win.y + 11.0f, borderCol, win.x + win.w, win.y + 11.0f, borderCol, 1.0f, 0.47f);
                C2D_DrawLine(win.x, win.y, borderCol, win.x + win.w, win.y, borderCol, 1.0f, 0.48f);
                C2D_DrawLine(win.x, win.y, borderCol, win.x, win.y + winH, borderCol, 1.0f, 0.48f);
                C2D_DrawLine(win.x + win.w, win.y, borderCol, win.x + win.w, win.y + winH, borderCol, 1.0f, 0.48f);
                C2D_DrawLine(win.x, win.y + winH, borderCol, win.x + win.w, win.y + winH, borderCol, 1.0f, 0.48f);
                
                // 5. Title text offset
                C2D_Text titleT;
                C2D_TextParse(&titleT, textBuf, win.title.c_str());
                C2D_TextOptimize(&titleT);
                float th = 0;
                C2D_TextGetDimensions(&titleT, 0.20f, 0.20f, nullptr, &th);
                C2D_DrawText(&titleT, C2D_WithColor, win.x + 6.0f, win.y + (12.0f - th) * 0.5f, 0.5f, 0.20f, 0.20f, isFocused ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(180, 180, 195, 255));
                
                // 7. Collapse button [-]/[+] on the right side
                C2D_Text btnCollapseT;
                C2D_TextParse(&btnCollapseT, textBuf, win.expanded ? "[-]" : "[+]");
                C2D_TextOptimize(&btnCollapseT);
                float ctw = 0, cth = 0;
                C2D_TextGetDimensions(&btnCollapseT, 0.20f, 0.20f, &ctw, &cth);
                C2D_DrawText(&btnCollapseT, C2D_WithColor, win.x + win.w - 14.0f, win.y + (12.0f - cth) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(0, 190, 230, 255));

                if (win.id == 3 && win.expanded && charObj) {
                    std::string curAnimName = animList.empty() ? "" : animList[animSliderIndex];
                    if (!curAnimName.empty()) {
                        float symY = win.y + 12.0f + 62.0f;
                        float indY = win.y + 12.0f + 128.0f;
                        float textScale = 0.18f;
                        
                        std::string prefix = charObj->animations[curAnimName].prefix;
                        if (prefix.length() > 14) prefix = prefix.substr(0, 12) + "...";
                        C2D_Text symT;
                        std::string symHUD = "Symbol: " + prefix;
                        C2D_TextParse(&symT, textBuf, symHUD.c_str());
                        C2D_TextOptimize(&symT);
                        C2D_DrawText(&symT, C2D_WithColor, win.x + 8.0f, symY, 0.5f, textScale, textScale, C2D_Color32(180, 180, 200, 255));
                        
                        C2D_Text indT;
                        std::string indHUD = "Indices: " + getIndicesString(charObj->animations[curAnimName].indices, pcMode);
                        C2D_TextParse(&indT, textBuf, indHUD.c_str());
                        C2D_TextOptimize(&indT);
                        C2D_DrawText(&indT, C2D_WithColor, win.x + 8.0f, indY, 0.5f, 0.18f, 0.18f, C2D_Color32(180, 180, 200, 255));
                    }
                }
            }

            auto layout = getPCLayout(windows);
            std::vector<int> visibleOptionIds;
            for (const auto& elem : layout) {
                if (elem.id != -1) {
                    visibleOptionIds.push_back(elem.id);
                }
            }

            for (const auto& elem : layout) {
                if (elem.id != -1) {
                    bool isSelected = (!visibleOptionIds.empty() && visibleOptionIds[curSelected] == elem.id);

                    if (elem.id == 0) {
                        drawListBox(elem.x, elem.y, elem.w, elem.h, characterList, curCharIndex, charScrollY, isSelected);
                    } else if (elem.id == 1) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text playT;
                        C2D_TextParse(&playT, textBuf, "Playable");
                        C2D_TextOptimize(&playT);
                        float th = 0;
                        C2D_TextGetDimensions(&playT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&playT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, charObj->isPlayer, isSelected);
                    } else if (elem.id == 2) {
                        drawButton(elem.x, elem.y, elem.w, elem.h, "RELOAD", isSelected);
                    } else if (elem.id == 3) {
                        drawButton(elem.x, elem.y, elem.w, elem.h, "SAVE", isSelected);
                    } else if (elem.id == 4) {
                        drawButton(elem.x, elem.y, elem.w, elem.h, "MAKE GHOST", isSelected);
                    } else if (elem.id == 5) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text showT;
                        C2D_TextParse(&showT, textBuf, "Show Ghost");
                        C2D_TextOptimize(&showT);
                        float th = 0;
                        C2D_TextGetDimensions(&showT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&showT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, showGhost, isSelected);
                    } else if (elem.id == 6) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text highlightT;
                        C2D_TextParse(&highlightT, textBuf, "Highlight");
                        C2D_TextOptimize(&highlightT);
                        float th = 0;
                        C2D_TextGetDimensions(&highlightT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&highlightT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, ghostHighlight, isSelected);
                    } else if (elem.id == 7) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text alphaT;
                        std::string alphaHUD = "Alpha: " + std::to_string(ghostAlpha).substr(0, 4);
                        C2D_TextParse(&alphaT, textBuf, alphaHUD.c_str());
                        C2D_TextOptimize(&alphaT);
                        C2D_DrawText(&alphaT, C2D_WithColor, elem.x + 4.0f, elem.y, 0.5f, 0.18f, 0.18f, C2D_Color32(255, 255, 255, 255));
                        drawSlider(elem.x + 4.0f, elem.y + 9.0f, elem.w - 8.0f, ghostAlpha, isSelected);
                    } else if (elem.id == 10) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text flipT;
                        C2D_TextParse(&flipT, textBuf, "Flip X");
                        C2D_TextOptimize(&flipT);
                        float th = 0;
                        C2D_TextGetDimensions(&flipT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&flipT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, charObj->flipX, isSelected);
                    } else if (elem.id == 11) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text aaT;
                        C2D_TextParse(&aaT, textBuf, "Antialiasing");
                        C2D_TextOptimize(&aaT);
                        float th = 0;
                        C2D_TextGetDimensions(&aaT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&aaT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, !charObj->noAntialiasing, isSelected);
                    } else if (elem.id == 16) {
                        if (animList.empty()) {
                            C2D_Text noneT;
                            C2D_TextParse(&noneT, textBuf, "No Anims Loaded");
                            C2D_TextOptimize(&noneT);
                            C2D_DrawText(&noneT, C2D_WithColor, elem.x + 4.0f, elem.y + 12.0f, 0.5f, 0.18f, 0.18f, C2D_Color32(255, 100, 100, 255));
                        } else {
                            drawListBox(elem.x, elem.y, elem.w, elem.h, animList, animSliderIndex, animScrollY, isSelected);
                        }
                    } else if (elem.id == 18) {
                        if (isSelected) drawLeftAccent(elem.x, elem.y, elem.h);
                        C2D_Text loopT;
                        C2D_TextParse(&loopT, textBuf, "Loop");
                        C2D_TextOptimize(&loopT);
                        float th = 0;
                        C2D_TextGetDimensions(&loopT, 0.20f, 0.20f, nullptr, &th);
                        C2D_DrawText(&loopT, C2D_WithColor, elem.x + 4.0f, elem.y + (elem.h - th) * 0.5f, 0.5f, 0.20f, 0.20f, C2D_Color32(255, 255, 255, 255));
                        drawCheckbox(elem.x + elem.w - 12.0f, elem.y + (elem.h - 7.0f) * 0.5f, animList.empty() ? false : charObj->animations[animList[animSliderIndex]].loop, isSelected);
                    } else {
                        std::string valStr = "";
                        if (elem.id == 8) valStr = std::to_string(charObj->singDuration).substr(0, 4);
                        else if (elem.id == 9) valStr = std::to_string(charObj->charScale).substr(0, 4);
                        else if (elem.id == 12) valStr = std::to_string((int)charObj->x);
                        else if (elem.id == 13) valStr = std::to_string((int)charObj->y);
                        else if (elem.id == 14) valStr = std::to_string((int)charObj->camOffsetX);
                        else if (elem.id == 15) valStr = std::to_string((int)charObj->camOffsetY);
                        else if (elem.id == 17) valStr = animList.empty() ? "0" : std::to_string(charObj->animations[animList[animSliderIndex]].fps);
                        else if (elem.id == 19) valStr = animList.empty() ? "0" : std::to_string((int)charObj->animations[animList[animSliderIndex]].offsetX);
                        else if (elem.id == 20) valStr = animList.empty() ? "0" : std::to_string((int)charObj->animations[animList[animSliderIndex]].offsetY);

                        drawArrowsVal(elem.x, elem.y, elem.w, elem.label, valStr, isSelected);
                    }
                }
            }
        }
    } else {
        // Draw Tab Contents (Normal Mode)
        if (currentTab == 0) {
            float boxX = 10.0f;
            float boxY = 32.0f;
            float boxW = 300.0f;
            float boxH = 80.0f;
            drawListBox(boxX, boxY, boxW, boxH, characterList, curCharIndex, charScrollY, curSelected == 0);
            drawSectionSep(120.0f);
            float playY = 120.0f;
            float textScale = 0.38f;
            C2D_Text playT;
            C2D_TextParse(&playT, textBuf, "Playable");
            C2D_TextOptimize(&playT);
            C2D_DrawText(&playT, C2D_WithColor, 8.0f, playY, 0.5f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
            drawCheckbox(320.0f - 25.0f, playY, charObj->isPlayer, curSelected == 1);
            drawSectionSep(140.0f);
            float btnY1 = 145.0f;
            float btnY2 = 175.0f;
            float btnH = 20.0f;
            drawButton(10.0f, btnY1, 300.0f, btnH, "RELOAD CHARACTER", curSelected == 2);
            drawButton(10.0f, btnY2, 300.0f, btnH, "SAVE CHARACTER", curSelected == 3);
        }
        else if (currentTab == 1) {
            float btnY = 32.0f;
            float btnH = 20.0f;
            drawButton(10.0f, btnY, 300.0f, btnH, "MAKE GHOST FROM CURRENT", curSelected == 0);
            drawSectionSep(46.0f);
            float checkShowY = 62.0f;
            float checkHighY = 82.0f;
            float textScale = 0.38f;
            C2D_Text showT;
            C2D_TextParse(&showT, textBuf, "Show Ghost");
            C2D_TextOptimize(&showT);
            C2D_DrawText(&showT, C2D_WithColor, 8.0f, checkShowY, 0.5f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
            drawCheckbox(320.0f - 25.0f, checkShowY, showGhost, curSelected == 1);
            C2D_Text highlightT;
            C2D_TextParse(&highlightT, textBuf, "Highlight Ghost");
            C2D_TextOptimize(&highlightT);
            C2D_DrawText(&highlightT, C2D_WithColor, 8.0f, checkHighY, 0.5f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
            drawCheckbox(320.0f - 25.0f, checkHighY, ghostHighlight, curSelected == 2);
            drawSectionSep(84.0f);
            float alphaY = 106.0f;
            float sliderX = 20.0f;
            float sliderY = 120.0f;
            float sliderW = 280.0f;
            C2D_Text alphaT;
            std::string alphaHUD = "Ghost Alpha: " + std::to_string(ghostAlpha).substr(0, 4);
            C2D_TextParse(&alphaT, textBuf, alphaHUD.c_str());
            C2D_TextOptimize(&alphaT);
            C2D_DrawText(&alphaT, C2D_WithColor, 8.0f, alphaY, 0.5f, 0.35f, 0.35f, C2D_Color32(255, 255, 255, 255));
            drawSlider(sliderX, sliderY, sliderW, ghostAlpha, curSelected == 3);
        }
        else if (currentTab == 2) {
            for (int i = 0; i < 8; i++) {
                float itemY = 32.0f + i * 20.0f;
                bool selected = (curSelected == i);
                float textScale = 0.38f;
                if (i == 0) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Sing Length", std::to_string(charObj->singDuration).substr(0, 4), selected);
                } else if (i == 1) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Scale", std::to_string(charObj->charScale).substr(0, 4), selected);
                } else if (i == 2) {
                    C2D_Text flipT;
                    C2D_TextParse(&flipT, textBuf, "Flip X");
                    C2D_TextOptimize(&flipT);
                    C2D_DrawText(&flipT, C2D_WithColor, 8.0f, itemY, 0.5f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
                    drawCheckbox(320.0f - 25.0f, itemY, charObj->flipX, selected);
                } else if (i == 3) {
                    C2D_Text aaT;
                    C2D_TextParse(&aaT, textBuf, "Antialiasing");
                    C2D_TextOptimize(&aaT);
                    C2D_DrawText(&aaT, C2D_WithColor, 8.0f, itemY, 0.5f, textScale, textScale, C2D_Color32(255, 255, 255, 255));
                    drawCheckbox(320.0f - 25.0f, itemY, !charObj->noAntialiasing, selected);
                } else if (i == 4) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Pos X", std::to_string((int)charObj->x), selected);
                } else if (i == 5) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Pos Y", std::to_string((int)charObj->y), selected);
                } else if (i == 6) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Cam Off X", std::to_string((int)charObj->camOffsetX), selected);
                } else if (i == 7) {
                    drawArrowsVal(0.0f, itemY, 320.0f, "Cam Off Y", std::to_string((int)charObj->camOffsetY), selected);
                }
            }
        }
        else if (currentTab == 3) {
            if (animList.empty()) {
                C2D_Text noneT;
                C2D_TextParse(&noneT, textBuf, "No Animations Loaded");
                C2D_TextOptimize(&noneT);
                C2D_DrawText(&noneT, C2D_WithColor, 12.0f, 50.0f, 0.5f, 0.45f, 0.45f, C2D_Color32(255, 100, 100, 255));
            } else {
                float boxX = 10.0f;
                float boxY = 32.0f;
                float boxW = 300.0f;
                float boxH = 80.0f;
                drawListBox(boxX, boxY, boxW, boxH, animList, animSliderIndex, animScrollY, curSelected == 0);
                drawSectionSep(120.0f);
                std::string curAnimName = animList[animSliderIndex];
                float symY = 120.0f;
                float fpsY = 138.0f;
                float loopY = 156.0f;
                float offXY = 174.0f;
                float offYY = 192.0f;
                float indY = 210.0f;
                float textScale = 0.35f;

                std::string prefix = charObj->animations[curAnimName].prefix;
                if (prefix.length() > 32) prefix = prefix.substr(0, 30) + "...";
                C2D_Text symT;
                std::string symHUD = "Symbol: " + prefix;
                C2D_TextParse(&symT, textBuf, symHUD.c_str());
                C2D_TextOptimize(&symT);
                C2D_DrawText(&symT, C2D_WithColor, 8.0f, symY, 0.5f, textScale, textScale, C2D_Color32(200, 200, 220, 255));

                drawArrowsVal(0.0f, fpsY, 320.0f, "FPS", std::to_string(charObj->animations[curAnimName].fps), curSelected == 1);
                drawSectionSep(152.0f);
                C2D_Text loopT;
                C2D_TextParse(&loopT, textBuf, "Loop");
                C2D_TextOptimize(&loopT);
                C2D_DrawText(&loopT, C2D_WithColor, 8.0f, loopY, 0.5f, 0.38f, 0.38f, C2D_Color32(255, 255, 255, 255));
                drawCheckbox(320.0f - 25.0f, loopY, charObj->animations[curAnimName].loop, curSelected == 2);
                drawSectionSep(168.0f);
                drawArrowsVal(0.0f, offXY, 320.0f, "Offset X", std::to_string((int)charObj->animations[curAnimName].offsetX), curSelected == 3);
                drawArrowsVal(0.0f, offYY, 320.0f, "Offset Y", std::to_string((int)charObj->animations[curAnimName].offsetY), curSelected == 4);

                C2D_Text indT;
                std::string indHUD = "Indices: " + getIndicesString(charObj->animations[curAnimName].indices, pcMode);
                C2D_TextParse(&indT, textBuf, indHUD.c_str());
                C2D_TextOptimize(&indT);
                C2D_DrawText(&indT, C2D_WithColor, 8.0f, indY, 0.5f, 0.32f, 0.32f, C2D_Color32(180, 180, 200, 255));
            }
        }
    }

    // Save message Banner
    if (saveMessageTimer > 0.0f) {
        float alpha = 1.0f;
        if (saveMessageTimer < 0.5f) {
            alpha = saveMessageTimer / 0.5f;
        }
        u32 bannerColor = C2D_Color32(0, 180, 0, (u8)(200 * alpha));
        u32 textColor = C2D_Color32(255, 255, 255, (u8)(255 * alpha));

        float bannerW = (panelW > 0.0f) ? (panelW - 20.0f) : 120.0f;
        float bannerX = (panelW > 0.0f) ? 10.0f : 100.0f;
        C2D_DrawRectSolid(bannerX, 100.0f, 0.95f, bannerW, 40.0f, bannerColor);
        
        C2D_Text msgT;
        C2D_TextParse(&msgT, textBuf, "CHARACTER SAVED!");
        C2D_TextOptimize(&msgT);
        float tw = 0, th = 0;
        C2D_TextGetDimensions(&msgT, 0.55f, 0.55f, &tw, &th);
        C2D_DrawText(&msgT, C2D_WithColor, bannerX + (bannerW - tw) * 0.5f, 100.0f + (40.0f - th) * 0.5f, 0.95f, 0.55f, 0.55f, textColor);
    }
}

void CharacterEditorState::exitState() {
    ModHandler::get().currentModFolder = "";
    switchState(new DebugMenuState());
}
