/// @file SolidRectExample.cpp
/// @brief Demonstration of solid-color rectangle helpers for fades and overlays.
///
/// This example shows how to use the Sprite2D solid-color rectangle helpers
/// for common effects like full-screen fades, letterbox bars, and colored overlays.

#include "Jet.hpp"

namespace SolidRectExample {

// Example 1: Full-screen fade to black effect
// ----------------------------------------------------------------------------
// Typical use: fade-in at game start, fade-out on transitions
class FadeEffect {
private:
    Renderer::Material fadeMat;
    Renderer::Sprite2D fadeSprite;
    Renderer::Scene* scene;
    float fadeAmount = 0.0f;  // 0.0 = transparent, 1.0 = opaque

public:
    FadeEffect(Renderer::Scene* scene, int screenWidth, int screenHeight, uint16_t color = 0x0000)
        : fadeMat(color, 0), scene(scene) {
        // Create a full-screen overlay starting at 0% opacity
        fadeSprite = Renderer::makeFullScreenFade(screenWidth, screenHeight, color, &fadeMat);
        fadeSprite.zOrder = 100;  // Draw on top of everything else
        scene->addSprite(&fadeSprite);
    }

    // Call this each frame with fadeAmount in [0.0, 1.0]
    void update(float amount) {
        fadeAmount = amount;
        if (fadeAmount < 0.0f) fadeAmount = 0.0f;
        if (fadeAmount > 1.0f) fadeAmount = 1.0f;

        // Convert to 8-bit alpha and update the sprite
        // Optimization: 0 → disabled, 255 → fast opaque path
        Renderer::setSolidRectAlpha(fadeSprite, (uint8_t)(fadeAmount * 255));
    }

    void fadeIn(float deltaTime, float speed = 1.0f) {
        update(fadeAmount + deltaTime * speed);
    }

    void fadeOut(float deltaTime, float speed = 1.0f) {
        update(fadeAmount - deltaTime * speed);
    }

    bool isFullyVisible() const { return fadeAmount >= 1.0f; }
    bool isFullyHidden() const { return fadeAmount <= 0.0f; }
};

// Example 2: Cinematic letterbox bars
// ----------------------------------------------------------------------------
// Typical use: cutscenes, dramatic moments
class LetterboxBars {
private:
    Renderer::Material topMat;
    Renderer::Material botMat;
    Renderer::Sprite2D topBar;
    Renderer::Sprite2D botBar;
    Renderer::Scene* scene;
    int screenHeight;
    int barHeight;
    float visibility = 0.0f;  // 0.0 = hidden, 1.0 = full bars

public:
    LetterboxBars(Renderer::Scene* scene, int screenWidth, int screenHeight, int barHeight = 40)
        : topMat(0x0000, 255), botMat(0x0000, 255), scene(scene),
          screenHeight(screenHeight), barHeight(barHeight) {
        // Create both letterbox bars
        topBar = Renderer::makeLetterboxBar(screenWidth, barHeight, 0x0000, true, &topMat);
        botBar = Renderer::makeSolidRect(0, screenHeight - barHeight, screenWidth, barHeight, &botMat);

        topBar.zOrder = 50;
        botBar.zOrder = 50;

        // Start hidden
        topBar.enabled = false;
        botBar.enabled = false;

        scene->addSprite(&topBar);
        scene->addSprite(&botBar);
    }

    void show() {
        topBar.enabled = true;
        botBar.enabled = true;
        visibility = 1.0f;
    }

    void hide() {
        topBar.enabled = false;
        botBar.enabled = false;
        visibility = 0.0f;
    }

    void toggle() {
        if (visibility > 0.5f) hide();
        else show();
    }
};

// Example 3: Colored damage/healing overlay
// ----------------------------------------------------------------------------
// Typical use: visual feedback for taking damage (red flash) or healing (green)
class FlashOverlay {
private:
    Renderer::Material flashMat;
    Renderer::Sprite2D flashSprite;
    Renderer::Scene* scene;
    float intensity = 0.0f;
    float decayRate = 2.0f;  // How fast the flash fades

public:
    FlashOverlay(Renderer::Scene* scene, int screenWidth, int screenHeight)
        : flashMat(0xF800, 0), scene(scene) {  // Red flash, start transparent
        flashSprite = Renderer::makeSolidRect(0, 0, screenWidth, screenHeight, &flashMat);
        flashSprite.zOrder = 90;  // Below fades but above gameplay
        scene->addSprite(&flashSprite);
    }

    // Trigger a flash with the given color and intensity
    void flash(uint16_t color, float maxIntensity = 0.5f) {
        flashMat.color = color;
        intensity = maxIntensity;
        Renderer::setSolidRectAlpha(flashSprite, (uint8_t)(intensity * 255));
    }

    // Flash red for damage
    void flashDamage(float amount = 0.5f) { flash(0xF800, amount); }  // Red

    // Flash green for healing
    void flashHeal(float amount = 0.3f) { flash(0x07E0, amount); }    // Green

    // Flash white for strong impact
    void flashImpact(float amount = 0.7f) { flash(0xFFFF, amount); }  // White

    // Call every frame to decay the flash
    void update(float deltaTime) {
        if (intensity > 0.0f) {
            intensity -= decayRate * deltaTime;
            if (intensity < 0.0f) intensity = 0.0f;
            Renderer::setSolidRectAlpha(flashSprite, (uint8_t)(intensity * 255));
        }
    }
};

// Example usage in a game loop
// ----------------------------------------------------------------------------
void demonstrationLoop(Renderer::Scene* scene, int screenWidth, int screenHeight) {
    // Set up effects
    FadeEffect fadeToBlack(scene, screenWidth, screenHeight, 0x0000);
    LetterboxBars cinematicBars(scene, screenWidth, screenHeight, 40);
    FlashOverlay damageFlash(scene, screenWidth, screenHeight);

    float time = 0.0f;
    float deltaTime = 1.0f / 60.0f;  // 60 FPS

    // Simulation of game loop
    for (int frame = 0; frame < 600; ++frame) {
        time += deltaTime;

        // Example sequence:
        // 0-2s: Fade in from black
        if (time < 2.0f) {
            fadeToBlack.fadeOut(deltaTime, 0.5f);
        }

        // 3s: Show letterbox bars for dramatic moment
        if (time >= 3.0f && time < 3.1f) {
            cinematicBars.show();
        }

        // 5s: Hide letterbox bars
        if (time >= 5.0f && time < 5.1f) {
            cinematicBars.hide();
        }

        // 7s: Flash red for damage
        if (time >= 7.0f && time < 7.1f) {
            damageFlash.flashDamage(0.6f);
        }

        // 9s: Fade out to black
        if (time >= 9.0f) {
            fadeToBlack.fadeIn(deltaTime, 0.5f);
        }

        // Update effects
        damageFlash.update(deltaTime);

        // Render the scene (would normally be part of your game loop)
        // scene->render();
    }
}

} // namespace SolidRectExample
