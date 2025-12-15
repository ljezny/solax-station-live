// Intelligence Setup Screen
// LVGL version: 8.3.11
// Project name: SolaxStationLive_Project

#ifndef UI_INTELLIGENCESETUP_H
#define UI_INTELLIGENCESETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// SCREEN: ui_IntelligenceSetup
extern void ui_IntelligenceSetup_screen_init(void);
extern void ui_IntelligenceSetup_screen_destroy(void);

extern lv_obj_t *ui_IntelligenceSetup;
extern lv_obj_t *ui_intelligenceMainContainer;

// Left column - Enable & Battery
extern lv_obj_t *ui_intelligenceLeftColumn;
extern lv_obj_t *ui_intelligenceEnableSwitch;
extern lv_obj_t *ui_intelligenceEnableLabel;
extern lv_obj_t *ui_intelligenceBatteryCostLabel;
extern lv_obj_t *ui_intelligenceBatteryCostInput;
extern lv_obj_t *ui_intelligenceMinSocLabel;
extern lv_obj_t *ui_intelligenceMinSocInput;
extern lv_obj_t *ui_intelligenceMaxSocLabel;
extern lv_obj_t *ui_intelligenceMaxSocInput;
extern lv_obj_t *ui_intelligenceBatteryCapLabel;
extern lv_obj_t *ui_intelligenceBatteryCapInput;
extern lv_obj_t *ui_intelligenceMaxChargeLabel;
extern lv_obj_t *ui_intelligenceMaxChargeInput;
extern lv_obj_t *ui_intelligenceMaxDischargeLabel;
extern lv_obj_t *ui_intelligenceMaxDischargeInput;

// Middle column - Buy coefficients
extern lv_obj_t *ui_intelligenceMiddleColumn;
extern lv_obj_t *ui_intelligenceBuyLabel;
extern lv_obj_t *ui_intelligenceBuyKLabel;
extern lv_obj_t *ui_intelligenceBuyKInput;
extern lv_obj_t *ui_intelligenceBuyQLabel;
extern lv_obj_t *ui_intelligenceBuyQInput;

// Right column - Sell coefficients
extern lv_obj_t *ui_intelligenceRightColumn;
extern lv_obj_t *ui_intelligenceSellLabel;
extern lv_obj_t *ui_intelligenceSellKLabel;
extern lv_obj_t *ui_intelligenceSellKInput;
extern lv_obj_t *ui_intelligenceSellQLabel;
extern lv_obj_t *ui_intelligenceSellQInput;

// Info labels for localization
extern lv_obj_t *ui_intelligenceBuyInfoLabel;
extern lv_obj_t *ui_intelligenceSellInfoLabel;
extern lv_obj_t *ui_intelligenceBuyFormulaLabel;
extern lv_obj_t *ui_intelligenceSellFormulaLabel;

// Bottom - Buttons
extern lv_obj_t *ui_intelligenceButtonContainer;
extern lv_obj_t *ui_intelligenceResetButton;
extern lv_obj_t *ui_intelligenceResetLabel;
extern lv_obj_t *ui_intelligenceSaveButton;
extern lv_obj_t *ui_intelligenceSaveLabel;
extern lv_obj_t *ui_intelligenceCancelButton;
extern lv_obj_t *ui_intelligenceCancelLabel;

// Keyboard
extern lv_obj_t *ui_intelligenceKeyboard;

// Beta warning
extern lv_obj_t *ui_intelligenceBetaWarning;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
