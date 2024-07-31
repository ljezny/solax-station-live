SET(SOURCES ui_Splash.c
    ui_Dashboard.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    ui_img_80640877.c
    ui_img_battery_png.c
    ui_img_880406665.c
    ui_img_2001872125.c
    fonts/ui_font_RobotoMono32.c
    fonts/ui_font_RobotoMono16.c
    fonts/ui_font_RobotoMono12.c)

add_library(ui ${SOURCES})
