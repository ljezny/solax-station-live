SET(SOURCES ui_Splash.c
    ui_Dashboard.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    ui_img_logo_png.c
    ui_img_1516017106.c
    ui_img_1279321064.c
    ui_img_564643105.c
    ui_img_960241876.c
    ui_img_leaf_png.c
    ui_img_1061873486.c
    ui_img_performance_png.c
    fonts/ui_font_RobotoMonoExtraSmall.c
    fonts/ui_font_RobotoMonoLarge.c
    fonts/ui_font_RobotoMonoMedium.c
    fonts/ui_font_RobotoMonoSmall.c)

add_library(ui ${SOURCES})
