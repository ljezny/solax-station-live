SET(SOURCES screens/ui_Splash.c
    screens/ui_Dashboard.c
    screens/ui_WifiSetup.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_logo_png.c
    images/ui_img_1516017106.c
    images/ui_img_1279321064.c
    images/ui_img_564643105.c
    images/ui_img_960241876.c
    images/ui_img_1061873486.c
    images/ui_img_performance_png.c
    fonts/ui_font_OpenSansExtraSmall.c
    fonts/ui_font_OpenSansLarge.c
    fonts/ui_font_OpenSansMedium.c
    fonts/ui_font_OpenSansSmall.c)

add_library(ui ${SOURCES})
