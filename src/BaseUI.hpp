#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <RemoteLogger.hpp>

// Forward declaration
class BaseUI;

/**
 * Screen Manager - singleton for centralized screen transitions.
 * Handles switching between screens with optional animations.
 * Automatically calls hide() on old screen and show() on new screen.
 */
class ScreenManager {
private:
    BaseUI* currentScreen = nullptr;
    
    ScreenManager() = default;
    
public:
    static ScreenManager& instance() {
        static ScreenManager inst;
        return inst;
    }
    
    // Delete copy/move constructors
    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;
    
    /**
     * Switch to a new screen with optional animation.
     * @param newScreen The screen to switch to
     * @param anim Animation type (default: fade)
     * @param duration Animation duration in ms (default: 200)
     * @param autoDelete Whether LVGL should auto-delete old screen (default: false - we manage it)
     */
    void switchTo(BaseUI* newScreen, 
                  lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_ON, 
                  uint32_t duration = 200);
    
    /**
     * Get the currently active screen.
     */
    BaseUI* current() const { return currentScreen; }
};

/**
 * Base class for all UI screens.
 * 
 * Lifecycle:
 * - show()   - Creates UI elements programmatically (lazy initialization)
 * - update() - Updates UI with current data
 * - hide()   - Destroys UI elements, frees memory
 * 
 * Each screen owns its LVGL objects and manages their lifecycle.
 */
class BaseUI {
    friend class ScreenManager;  // Allow ScreenManager to access skipLoad
    
protected:
    lv_obj_t* screen = nullptr;  // Main screen object
    bool initialized = false;     // Whether show() has been called
    bool skipLoad = false;        // If true, loadScreen() does nothing (for animated transitions)
    
    /**
     * Create the screen object with common settings.
     * Call this at the beginning of show() implementation.
     */
    void createScreen() {
        if (screen != nullptr) {
            lv_obj_del(screen);
        }
        screen = lv_obj_create(NULL);
        initialized = true;
    }
    
    /**
     * Load this screen as the active screen (no animation).
     * Call this at the end of show() implementation.
     * If skipLoad is true (set by ScreenManager), this does nothing.
     */
    void loadScreen() {
        if (skipLoad) {
            skipLoad = false;  // Reset for next time
            return;
        }
        if (screen != nullptr) {
            lv_scr_load(screen);
        }
    }
    
    /**
     * Load this screen with animation.
     * @param anim Animation type
     * @param duration Duration in ms
     * @param autoDelete Whether to auto-delete old screen (true when managed by ScreenManager)
     */
    void loadScreenAnim(lv_scr_load_anim_t anim, uint32_t duration, bool autoDelete = true) {
        if (screen != nullptr) {
            lv_scr_load_anim(screen, anim, duration, 0, autoDelete);
        }
    }
    
    /**
     * Mark screen as deleted by LVGL (without calling lv_obj_del).
     * Used when LVGL auto-deletes the screen after animation.
     */
    void markScreenDeleted() {
        screen = nullptr;
        initialized = false;
    }

public:
    virtual ~BaseUI() {
        hide();
    }
    
    /**
     * Initialize and display the UI screen.
     * Creates all UI elements programmatically.
     * Must be implemented by derived classes.
     */
    virtual void show() = 0;
    
    /**
     * Update the UI with current data.
     * Default implementation does nothing.
     * Override in derived classes as needed.
     */
    virtual void update() {}
    
    /**
     * Hide and destroy the UI screen.
     * Frees all allocated LVGL objects.
     */
    virtual void hide() {
        if (screen != nullptr) {
            lv_obj_del(screen);
            screen = nullptr;
        }
        initialized = false;
    }
    
    /**
     * Check if the screen is currently initialized.
     */
    bool isInitialized() const {
        return initialized;
    }
    
    /**
     * Get the screen object (for external use like lv_scr_load).
     */
    lv_obj_t* getScreen() const {
        return screen;
    }
};

// ScreenManager implementation (must be after BaseUI definition)
inline void ScreenManager::switchTo(BaseUI* newScreen, 
                                    lv_scr_load_anim_t anim, 
                                    uint32_t duration) {
    if (newScreen == nullptr) {
        LOGW("ScreenManager::switchTo called with nullptr");
        return;
    }
    
    if (newScreen == currentScreen && newScreen->isInitialized()) {
        LOGD("ScreenManager: Already on requested screen, skipping");
        return;
    }
    
    LOGD("ScreenManager: Switching screen (anim=%d, duration=%dms)", anim, duration);
    
    // Set flag to prevent show() from loading the screen immediately
    newScreen->skipLoad = true;
    
    // Create new screen UI elements (but don't load yet)
    newScreen->show();
    
    // Load with animation - LVGL will auto-delete old screen after animation
    newScreen->loadScreenAnim(anim, duration, true);
    
    // Mark old screen as deleted (LVGL will handle actual deletion)
    if (currentScreen != nullptr) {
        currentScreen->markScreenDeleted();
    }
    
    currentScreen = newScreen;
}
