#pragma once

#include <Arduino.h>
#include <lvgl.h>

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
protected:
    lv_obj_t* screen = nullptr;  // Main screen object
    bool initialized = false;     // Whether show() has been called
    
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
     * Load this screen as the active screen.
     * Call this at the end of show() implementation.
     */
    void loadScreen() {
        if (screen != nullptr) {
            lv_scr_load(screen);
        }
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
