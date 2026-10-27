// What the UWP app shell (App.cpp) and the C glue in this folder share.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Paths the core reads through cfile.h, set before the game starts
extern const char *Document_path;
extern const char *Cache_path;
extern const char *Data_path;

// Physical pixels per view pixel (the UWP equivalent of Android dp)
extern float Display_scale;

int descent_main(int argc, char **argv);

void init_buttons(int w, int h);
void draw_buttons(void);
// Touch events in physical pixels; returns 1 if the touch controls took it
int touch_handler(int action, int pointer_id, float x, float y, float prev_x, float prev_y);

// Whether the on-screen controls are drawn and take touches. Always on the
// phone; on a PC only after the screen is touched.
extern int Touch_controls_shown;

// 1 while the player flies, as opposed to menus, briefings and the automap
int in_game_view(void);

// The core defines bool as int, hence int for these flags
void key_handler(unsigned char scancode, int down);
void mouse_handler(short x, short y, int down);
void mouse_button_handler(int button, int down);
void mouse_set_pos(short x, short y);

// On a PC the ship turns with the mouse's own movement, not with drags:
// set Relative_mouse and mouse_get_delta takes the movement from
// take_mouse_delta (App.cpp) instead
extern int Relative_mouse;
void take_mouse_delta(int *dx, int *dy);

// Touch pointer ids passed to touch_handler are slots below this
#define MAX_TOUCHES 10

#define TOUCH_DOWN 0
#define TOUCH_UP 1
#define TOUCH_MOVE 2

#ifdef __cplusplus
}
#endif
