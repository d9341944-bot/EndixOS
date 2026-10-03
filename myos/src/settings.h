#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>

struct os_settings {
    uint32_t bg, surface, surface2, surface3;
    uint32_t primary, primary_dim;
    uint32_t on_surface, on_variant, outline;

    int theme_id;
    int wallpaper_id;
    int brightness;
};

extern struct os_settings g_settings;

void settings_init(void);
void settings_save(void);
void settings_apply_theme(int id);

void settings_draw_wallpaper(void);
int  settings_wallpaper_is_animated(void);
const char* settings_get_name(int id);

void settings_open(void);
void settings_handle_key(int c);

#endif
