SET(SOURCES ui_Splash.c
    ui_Dashboard.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    ui_img_1516017106.c
    ui_img_1666978921.c
    ui_img_2000019334.c
    ui_img_1930319305.c
    ui_img_leaf_png.c
    ui_img_1061873486.c
    fonts/ui_font_RobotoMono12.c
    fonts/ui_font_RobotoMono16.c
    fonts/ui_font_RobotoMonoSmall.c
    fonts/ui_font_RobotoMonoLarge.c
    fonts/ui_font_RobotoMonoMedium.c)

add_library(ui ${SOURCES})
