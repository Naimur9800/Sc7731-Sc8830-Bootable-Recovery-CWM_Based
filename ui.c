/*
 * Copyright (C) 2007 The Android Open Source Project
 * Copyright (C) 2014 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>

#include "ui.h"
#include "extendedcommands.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "cutils/android_reboot.h"
#include "cutils/properties.h"
#include "libcrecovery/common.h"
#include "minuictr/minui.h"
#include "recovery_ui.h"

extern int __system(const char *command);

#ifdef BOARD_HAS_NO_SELECT_BUTTON
static int gShowBackButton = 1;
#else
static int gShowBackButton = 0;
#endif

UIParameters ui_parameters = {
    6,       // indeterminate progress bar frames
    20,      // fps
    6,       // installation icon frames (0 == static image)
    10, 100, // installation icon overlay offset
};

static pthread_mutex_t gUpdateMutex = PTHREAD_MUTEX_INITIALIZER;
static gr_surface gBackgroundIcon[NUM_BACKGROUND_ICONS];
static gr_surface *gInstallationOverlay;
static gr_surface *gProgressBarIndeterminate;
static gr_surface gProgressBarEmpty;
static gr_surface gProgressBarFill;
static gr_surface gVirtualKeys;
static int ui_has_initialized = 0;
static int ui_log_stdout = 1;

static int boardEnableKeyRepeat = 0;
static int boardRepeatableKeys[64];
static int boardNumRepeatableKeys = 0;

static const struct { gr_surface* surface; const char *name; } BITMAPS[] = {
	{ &gBackgroundIcon[BACKGROUND_ICON_INSTALLING],          "icon_installing" },
	{ &gBackgroundIcon[BACKGROUND_ICON_ERROR],               "icon_error" },
	{ &gBackgroundIcon[BACKGROUND_ICON_CLOCKWORK],           "icon_clockwork" },
	{ &gProgressBarEmpty,                                    "progress_empty" },
	{ &gProgressBarFill,                                     "progress_fill" },
	{ &gVirtualKeys,                                         "virtual_keys" },
	{ NULL,                                                  NULL },
};

static int gCurrentIcon = 0;
static int gInstallingFrame = 0;

static enum ProgressBarType {
    PROGRESSBAR_TYPE_NONE,
    PROGRESSBAR_TYPE_INDETERMINATE,
    PROGRESSBAR_TYPE_NORMAL,
} gProgressBarType = PROGRESSBAR_TYPE_NONE;

// Progress bar scope of current operation
static float gProgressScopeStart = 0.0;
static float gProgressScopeSize = 0.0;
static float gProgress = 0.0;
static double gProgressScopeTime;
static double gProgressScopeDuration;

// Set to 1 when both graphics pages are the same (except for the progress bar)
static int gPagesIdentical = 0;

// Log text overlay, displayed when a magic key is pressed
static char text[MAX_ROWS][MAX_COLS];
static int text_cols = 0;
static int text_rows = 0;
static int text_col = 0;
static int text_row = 0;
static int text_top = 0;
static int show_text = 0;
static int show_text_ever = 0; // i.e. has show_text ever been 1?

static char menu[MENU_MAX_ROWS][MENU_MAX_COLS];
static int show_menu = 0;
static int menu_top = 0;
static int menu_items = 0;
static int menu_sel = 0;
static int menu_show_start = 0; // line at which menu display starts
static int max_menu_rows;

static unsigned cur_rainbow_color = 0;
static int gRainbowMode = 0;

// Key event input queue
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;
static int key_queue[256], key_queue_len = 0;
static unsigned long key_last_repeat[KEY_MAX + 1];
static unsigned long key_press_time[KEY_MAX + 1];
static volatile char key_pressed[KEY_MAX + 1];

static void update_screen_locked(void);
static int ui_wait_key_with_repeat();

// Current time
static double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// time using gettimeofday()
// to get time in usec, we call timenow_usec() which will link here if clock_gettime fails
static long long gettime_usec() {
    struct timeval now;
    long long useconds;
    gettimeofday(&now, NULL);
    useconds = (long long)(now.tv_sec) * 1000000LL;
    useconds += (long long)now.tv_usec;
    return useconds;
}

// use clock_gettime for elapsed time
// this is nsec precise + less prone to issues for elapsed time
// unsigned integers cannot be negative (overflow): set error return code to 0 (1.1.1970 00:00)
static unsigned long long gettime_nsec() {
    struct timespec ts;
    static int err = 0;

    if (err) return 0;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        LOGI("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        ++err;
        return 0;
    }

    unsigned long long nseconds = (unsigned long long)(ts.tv_sec) * 1000000000ULL;
    nseconds += (unsigned long long)(ts.tv_nsec);
    return nseconds;
}

static long long timenow_msec() {
    // first try using clock_gettime
    unsigned long long nseconds;
    nseconds = gettime_nsec();
    if (nseconds == 0) {
        // LOGI("dropping to gettimeofday()\n");
        return (gettime_usec() / 1000LL);
    }

    return (long long)(nseconds / 1000000ULL);
}

// Draw the given frame over the installation overlay animation.  The
// background is not cleared or draw with the base icon first; we
// assume that the frame already contains some other frame of the
// animation.  Does nothing if no overlay animation is defined.
// Should only be called with gUpdateMutex locked.
static void draw_install_overlay_locked(int frame) {
    if (gInstallationOverlay == NULL) return;
    gr_surface surface = gInstallationOverlay[frame];
    int iconWidth = gr_get_width(surface);
    int iconHeight = gr_get_height(surface);
    gr_blit(surface, 0, 0, iconWidth, iconHeight,
            ui_parameters.install_overlay_offset_x,
            ui_parameters.install_overlay_offset_y);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with gUpdateMutex locked.
static void draw_background_locked(int icon) {
    gPagesIdentical = 0;

    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    if (icon) {
        gr_surface surface = gBackgroundIcon[icon];
        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int iconX = (gr_fb_width() - iconWidth) / 2;
        int iconY = (gr_fb_height() - iconHeight) / 2;
        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
        if (icon == BACKGROUND_ICON_INSTALLING) {
            draw_install_overlay_locked(gInstallingFrame);
        }
    }
}

static void ui_increment_frame() {
    if (!ui_has_initialized) return;
    gInstallingFrame =
        (gInstallingFrame + 1) % ui_parameters.installing_frames;
}

static long long t_last_progress_update = 0;

// Draw the progress bar (if any) on the screen; does not flip pages
// Should only be called with gUpdateMutex locked
static void draw_progress_locked()
{
    if (gCurrentIcon == BACKGROUND_ICON_INSTALLING) {
        // update the installation animation, if active
        if (ui_parameters.installing_frames > 0)
            ui_increment_frame();
        draw_install_overlay_locked(gInstallingFrame);
    }

    if (gProgressBarType != PROGRESSBAR_TYPE_NONE) {
        int iconHeight = gr_get_height(gBackgroundIcon[BACKGROUND_ICON_INSTALLING]);
        int width = gr_get_width(gProgressBarEmpty);
        int height = gr_get_height(gProgressBarEmpty);

        int dx = (gr_fb_width() - width)/2;
        int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

        // Erase behind the progress bar (in case this was a progress-only update)
        gr_color(0, 0, 0, 255);
        gr_fill(dx, dy, width, height);

        if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL) {
            float progress = gProgressScopeStart + gProgress * gProgressScopeSize;
            int pos = (int) (progress * width);

            if (pos > 0) {
                gr_blit(gProgressBarFill, 0, 0, pos, height, dx, dy);
            }
            if (pos < width-1) {
                gr_blit(gProgressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
            }
        }

        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE) {
            static int frame = 0;
            gr_blit(gProgressBarIndeterminate[frame], 0, 0, width, height, dx, dy);
            frame = (frame + 1) % ui_parameters.indeterminate_frames;
        }
    }

    t_last_progress_update = timenow_msec();
}

/*******************************/
/* Part of touch handling code */
/*   Original port by PhilZ    */
/*******************************/

static int touch_sel = 0;
static int first_touched_menu = -1;
static int last_touched_menu = -1;
static int now_scrolling = 0;
static long scroll_speed = 0;
int ignore_key_action = 0;
static int allow_long_press_move = 0;
static int virtual_keys_h = 0;

static long long  t_last_touch = 0;
static long long t_last_menu_touch = 0;
static long long t_old_last_touch = 0;
static long long t_first_touch = 0;
static long long t_last_scroll_y = 0;

static int in_touch = 0; //1 == in a touch, 0 == finger lifted
static int touch_x = TOUCH_RESET_POS; // actual touch position
static int touch_y = TOUCH_RESET_POS; // actual touch position
static int first_x = TOUCH_RESET_POS; // x coordinate of first touch after finger was lifted
static int first_y = TOUCH_RESET_POS; // y coordinate of first touch after finger was lifted
static int last_scroll_y = TOUCH_RESET_POS; // last y that triggered an up/down scroll

// we reset gestures whenever finger is lifted
static void reset_gestures() {
    first_x = TOUCH_RESET_POS;
    first_y = TOUCH_RESET_POS;
    touch_x = TOUCH_RESET_POS;
    touch_y = TOUCH_RESET_POS;
    last_scroll_y = TOUCH_RESET_POS;
}

static int vbutton_pressed = -1;
static int vk_pressed = -1;

static void draw_virtualkeys_locked() {
        gr_surface surface = gVirtualKeys;
        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int iconX = (gr_fb_width() - iconWidth) / 2;
        int iconY = (gr_fb_height() - iconHeight);
        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);

        // draw highlight key line
        gr_color(34, 167, 240, 255);
        gr_fill(iconX, iconY-2,
                iconX+iconWidth, iconY);
}

/*******************************/

static int get_batt_stats(void) {
    int level = -1;
    char value[4];
    FILE * fd;
#ifndef CUSTOM_BATTERY_FILE
#define CUSTOM_BATTERY_FILE "/sys/class/power_supply/battery/capacity"
#endif    
    if ((fd = fopen(CUSTOM_BATTERY_FILE, "rt")) == NULL){
		LOGI("Error! opening batt file");
		level = -1;
		return level;
	}
    fgets(value, 4, fd);
    fclose(fd);
    level = atoi(value);

    if (level > 100)
        level = 100;
    if (level < 0)
        level = 0;
    return level;
}

static int get_batt_charging(void) {
    int charging = 0;
    char stat[16] = "";
    FILE * fs;
#ifndef CUSTOM_BATTERY_STATS_PATH
#define CUSTOM_BATTERY_STATS_PATH "/sys/class/power_supply/battery/status"
#endif
	if ((fs = fopen(CUSTOM_BATTERY_STATS_PATH, "rt")) == NULL){
		LOGI("Error! opening batt status file");
		return charging;
	}
	fscanf(fs,"%s",stat);
	if (strncmp(stat,"Charging", 3) == 0) {
		charging = 1;
	}
    fclose(fs);
    return charging;
}

static void draw_head_line(int row, const char* t, int align) {
    int col = 0;
    if (t[0] != '\0') {
        int length = strnlen(t, MENU_MAX_COLS) * CHAR_WIDTH;
        switch(align) {
            case LEFT_ALIGN:
                col = 1;
                break;
            case CENTER_ALIGN:
                col = ((gr_fb_width() - length) / 2);
                break;
            case RIGHT_ALIGN:
                col = gr_fb_width() - length - 1;
                break;
        }
        gr_text(col, (row + 1) * CHAR_HEIGHT - 1, t, 0);
    }
}

static void draw_text_line(int row, const char* t, int height, int align) {
    int col = 0;
    if (t[0] != '\0') {
		if (ui_get_rainbow_mode) ui_rainbow_mode();
        int length = strnlen(t, MENU_MAX_COLS) * CHAR_WIDTH;
        switch(align) {
            case LEFT_ALIGN:
                col = 1;
                break;
            case CENTER_ALIGN:
                col = ((gr_fb_width() - length) / 2);
                break;
            case RIGHT_ALIGN:
                col = gr_fb_width() - length - 1;
                break;
        }
        gr_text(col, ((row + 1) * height) - ((height - CHAR_HEIGHT) / 2) - 1, t, 0);
    }
}

static int show_battery = 0;
static void draw_battery() {
	int batt_level = 0;
	int batt_stats = 0;
	char batt_text[40] = "";	
    batt_stats = get_batt_charging();
    batt_level = get_batt_stats();
    if (batt_stats == 1) {
		sprintf(batt_text, "[+%d%%]", batt_level);
	} else {
		sprintf(batt_text, "[%d%%]", batt_level);
	}
	if (batt_level < 21)
        gr_color(242, 38, 19, 255);
    else gr_color(38, 194, 129, 255);
	draw_head_line(0, batt_text, RIGHT_ALIGN);
}

/*******************************/
/* Part of touch handling code */
/*   Original port by PhilZ    */
/*******************************/

void fast_ui_init(void) {
    pthread_mutex_lock(&gUpdateMutex);
	
	gr_surface surface = gVirtualKeys;
	virtual_keys_h = gr_get_height(surface);
	text_rows = (gr_fb_height() - virtual_keys_h) / CHAR_HEIGHT;

    max_menu_rows = ((text_rows - MIN_LOG_ROWS) * CHAR_HEIGHT) / MENU_TOTAL_HEIGHT;
	
    while (((gr_fb_height() - (max_menu_rows * MENU_TOTAL_HEIGHT) - virtual_keys_h) / CHAR_HEIGHT) < MIN_LOG_ROWS)
    {
        --max_menu_rows;
    }

    if (max_menu_rows > MENU_MAX_ROWS)
        max_menu_rows = MENU_MAX_ROWS;
    if (text_rows > MAX_ROWS) text_rows = MAX_ROWS;
    
    pthread_mutex_unlock(&gUpdateMutex);
}

void draw_menu() {
    if (show_text) {
        // don't "disable" the background any more with this...
        gr_color(0, 0, 0, 110);
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());

        int i = 0;
        int j = 0;
        int row = 0;
        
        if (show_menu) {			
            if (menu_sel >= menu_show_start && menu_sel < menu_show_start + max_menu_rows - menu_top) {
                gr_color(MENU_HIGHLIGHT_COLOR);
                gr_fill(0, ((menu_top + menu_sel - menu_show_start) * MENU_TOTAL_HEIGHT) + 1, gr_fb_width(), (menu_top + menu_sel - menu_show_start + 1) * MENU_TOTAL_HEIGHT);
				gr_color(MENU_SELECTED_COLOR);
				gr_fill(0, (menu_top + menu_sel - menu_show_start) * MENU_TOTAL_HEIGHT, gr_fb_width(), ((menu_top + menu_sel - menu_show_start) * MENU_TOTAL_HEIGHT) + 1);
            }
            
            // start header text write
            gr_color(HEADER_TEXT_COLOR);
            for(i = 0; i < menu_top; ++i) {
                draw_head_line(i, menu[i], LEFT_ALIGN);
                row++;
            }
            draw_battery();
            show_battery = 1;            

            if (menu_items - menu_show_start + menu_top >= max_menu_rows)
                j = max_menu_rows - menu_top;
            else
                j = menu_items - menu_show_start;

            for(i = menu_show_start + menu_top; i < (menu_show_start + menu_top + j); ++i) {
                if (i == menu_top + menu_sel && menu_sel >= menu_show_start && menu_sel < menu_show_start + max_menu_rows - menu_top) {
                    gr_color(MENU_SELECTED_COLOR); 
                    draw_text_line(i - menu_show_start , menu[i], MENU_TOTAL_HEIGHT, LEFT_ALIGN);
                } else {
                    gr_color(MENU_BACKGROUND_COLOR);
                    gr_fill(0, ((i - menu_show_start) * MENU_TOTAL_HEIGHT) + 1,
                            gr_fb_width(), (i - menu_show_start + 1) * MENU_TOTAL_HEIGHT);
					gr_color(MENU_SEPARATOR_COLOR);
					gr_fill(0, ((i - menu_show_start) * MENU_TOTAL_HEIGHT),
							gr_fb_width(), ((i - menu_show_start) * MENU_TOTAL_HEIGHT) + 1);
                    gr_color(MENU_TEXT_COLOR);
                    draw_text_line(i - menu_show_start, menu[i], MENU_TOTAL_HEIGHT, LEFT_ALIGN);
                }
                row++;
                if (row >= max_menu_rows)
                    break;
            }

            gr_color(242, 38, 19, 255);
            gr_fill(0, (row * MENU_TOTAL_HEIGHT) + (MENU_TOTAL_HEIGHT / CHAR_HEIGHT) - 1,
                    gr_fb_width(), (row * MENU_TOTAL_HEIGHT) + (MENU_TOTAL_HEIGHT / CHAR_HEIGHT) + 1);
            row++;
        }

        int available_rows;
        int cur_row;
        int start_row;
        cur_row = text_row;
        start_row = ((row * MENU_TOTAL_HEIGHT) / CHAR_HEIGHT);
		available_rows = (gr_fb_height() - (row * MENU_TOTAL_HEIGHT) - virtual_keys_h) / CHAR_HEIGHT;

        if (available_rows < MAX_ROWS)
            cur_row = (cur_row + (MAX_ROWS - available_rows)) % MAX_ROWS;
        else
            start_row = (available_rows + ((row * MENU_TOTAL_HEIGHT) / CHAR_HEIGHT)) - MAX_ROWS;

        int r;
        for(r = 0; r < (available_rows < MAX_ROWS ? available_rows : MAX_ROWS); r++) {
            if ((start_row + r) <= 1) {
                int col_offset = 1;
                if (text_cols - col_offset < 0) col_offset = 0; 
                text[(cur_row + r) % MAX_ROWS][text_cols - col_offset] = '\0';
            }

          gr_color(NORMAL_TEXT_COLOR);
          draw_text_line(start_row + r, text[(cur_row + r) % MAX_ROWS], CHAR_HEIGHT, LEFT_ALIGN);
        }
    }

    draw_virtualkeys_locked();
}

/*******************************/

static int ui_menu_header_offset() {
    int offset = 1;
    if (show_battery) offset += 2;
    if (text_cols - offset < 0) offset = 1; 

    return offset;
}

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_screen_locked(void) {
    if (!ui_has_initialized) return;
    draw_background_locked(gCurrentIcon);
    draw_progress_locked();
    draw_menu();
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with gUpdateMutex locked.
static void update_screen_locked(void) {
    if (!ui_has_initialized) return;
    draw_screen_locked();
    gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with gUpdateMutex locked.
static void update_progress_locked(void) {
    if (!ui_has_initialized) return;

    // minimum of UI_UPDATE_PROGRESS_INTERVAL msec delay between progress updates if we have a text overlay
    // exception: gProgressScopeDuration != 0: to keep zip installer refresh behaviour
    if (show_text && t_last_progress_update > 0 && gProgressScopeDuration == 0 && timenow_msec() - t_last_progress_update < UI_UPDATE_PROGRESS_INTERVAL)
        return;

    if (show_text || !gPagesIdentical) {
        draw_screen_locked();    // Must redraw the whole screen
        gPagesIdentical = 1;
    } else {
        draw_progress_locked();  // Draw only the progress bar and overlays
    }
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
static void *progress_thread(void *cookie) {
    double interval = 1.0 / ui_parameters.update_fps;
    for (;;) {
        double start = now();
        pthread_mutex_lock(&gUpdateMutex);

        int redraw = 0;

        // update the progress bar animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE) {
            redraw = 1;
        } else if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && gProgressScopeDuration > 0) {
            // move the progress bar forward on timed intervals, if configured
            double elapsed = now() - gProgressScopeTime;
            float progress = 1.0 * elapsed / gProgressScopeDuration;
            if (progress > 1.0) progress = 1.0;
            if (progress > gProgress) {
                gProgress = progress;
                redraw = 1;
            }
        }

        if (gCurrentIcon == BACKGROUND_ICON_INSTALLING) {
            redraw = 1;
        }

        if (redraw) update_progress_locked();

        pthread_mutex_unlock(&gUpdateMutex);
        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
    return NULL;
}

/*******************************/
/* Part of touch handling code */
/*   Original port by PhilZ    */
/*******************************/

static int ui_valid_menu_touch(int pos_y) {
    int ret = -1;
    if (show_menu <= 0) {
        ret = -2;
    } else {
        int i = 0;
        int j = 0;
        if (menu_items - menu_show_start + menu_top >= max_menu_rows) {
            // we have many menu items, they need to scroll
            j = max_menu_rows - menu_top;
        }
         else {
            j = menu_items - menu_show_start;
        }
        for(i = menu_show_start; i < (menu_show_start + j); ++i) {
            if (pos_y > ((i - menu_show_start + menu_top) * MENU_TOTAL_HEIGHT) &&
                    pos_y < ((i - menu_show_start + menu_top + 1) * MENU_TOTAL_HEIGHT))
                ret = i; // yes, we touched a selectable menu
        }
    }

    return ret;
}

int ui_menu_touch_select() {
    pthread_mutex_lock(&gUpdateMutex);

    if (show_menu > 0) {
        if (first_touched_menu >= 0 && first_touched_menu != menu_sel) {
            menu_sel = first_touched_menu;
            update_screen_locked();
        }
    }

    touch_sel = 0;
    pthread_mutex_unlock(&gUpdateMutex);
    return menu_sel;
}

static void scroll_touch_menu() {
    pthread_mutex_lock(&gUpdateMutex);

    int menu_jump;
    if (in_touch) {
        menu_jump = 0;
    } else {
        menu_jump = scroll_speed / (gr_fb_height() * 2);
        if (menu_jump > 5)
            menu_jump = 5;
    }

    int old_menu_show_start = menu_show_start;
    menu_show_start += now_scrolling + (now_scrolling > 0 ? menu_jump : -menu_jump);

    if (menu_items - menu_show_start + menu_top < max_menu_rows) {
        menu_show_start = menu_items - max_menu_rows + menu_top;
        now_scrolling = 0;
    }

    if (menu_show_start < 0) {
        menu_show_start = 0;
        now_scrolling = 0;
    }
    
    if (menu_show_start != old_menu_show_start)
        update_screen_locked();

    pthread_mutex_unlock(&gUpdateMutex);
}

static int ui_check_key() {
    pthread_mutex_lock(&key_queue_mutex);
    int key = -1;
    if (key_queue_len > 0) {
        key = key_queue[0];
        memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
    }
    pthread_mutex_unlock(&key_queue_mutex);
    return key;
}

int touch_handle_key(int key_code, int visible) {
    if (visible) {
        switch (key_code) {
            case KEY_PAGEUP:
            case BTN_TOUCH:
                if (touch_sel)
                    return HIGHLIGHT_ON_TOUCH;
                break;

            case KEY_ESC: 
                break;
                
			case BTN_TOOL_FINGER: 
                break;

            case KEY_PAGEDOWN:
                if (now_scrolling != 0)
                    scroll_touch_menu();
                break;

            case KEY_LEFTBRACE:
                break;

            case KEY_F12:
            case KEY_ENTER:
            case BTN_MOUSE: 
                return SELECT_ITEM;

            case KEY_END:
            case KEY_BACKSPACE:            
            case KEY_BACK:
                if (!ui_root_menu || vbutton_pressed != -1) {
                    return GO_BACK;
                }
                break;

            default:
                return device_handle_key(key_code, visible);
        }
    }

    return NO_ACTION;
}

/*******************************/
/*  Start touch handling code  */
/*   Original port by PhilZ    */
/*******************************/

static int device_has_vk = 0; 
static int touch_is_init = 0;
static int vk_count = 0;

struct virtualkey {
    int scancode;
    int centerx, centery;
    int width, height;
};

struct virtualkey *vks;

static int board_min_x = 0;
static int board_max_x = 0;
static int board_min_y = 0;
static int board_max_y = 0;

static char *vk_strtok_r(char *str, const char *delim, char **save_str) {
    if(!str) {
        if(!*save_str)
            return NULL;
        str = (*save_str) + 1;
    }
    *save_str = strpbrk(str, delim);

    if (*save_str)
        **save_str = '\0';

    return str;
}

struct abs_devices {
    int fd;
    char deviceName[64];
    int ignored;
};

static struct abs_devices abs_device[MAX_DEVICES + MAX_MISC_FDS];
static unsigned abs_count = 0;

static int vk_init(struct abs_devices e) {
    char vk_str[2048];
    char *ts = NULL;
    ssize_t len;
    int vk_fd;
    char vk_path[PATH_MAX] = "/sys/board_properties/virtualkeys.";
    strcat(vk_path, e.deviceName);
#ifdef RECOVERY_TOUCH_DEBUG
    LOGI("\n>> Checking board vk: %s\n", vk_path);
#endif
    vk_fd = open(vk_path, O_RDONLY);
    if (vk_fd < 0)
        return -1;

    len = read(vk_fd, vk_str, sizeof(vk_str)-1);
    close(vk_fd);
    if (len <= 0) {
        LOGI("error reading vk path\n");
        return -1;
    }

    vk_str[len] = '\0';

    for(ts = vk_str, vk_count = 1; *ts; ++ts) {
        if (*ts == ':')
            ++vk_count;
    }

    if (vk_count % 6) {
        LOGW("minui: %s is %d %% 6\n", vk_path, vk_count % 6);
    }

    vk_count /= 6;
    if (vk_count <= 0) {
        LOGI("non valid format for %s\n", vk_path);
        return -1;
    }

    vks = malloc(sizeof(*vks) * vk_count);
    int i;
    for(i = 0; i < vk_count; ++i) {
        char *token[6];
        int j;

        for(j = 0; j < 6; ++j) {
            token[j] = vk_strtok_r((i||j)?NULL:vk_str, ":", &ts);
        }

        if (strcmp(token[0], "0x01") != 0) {
            LOGW("minui: %s: ignoring unknown virtual key type %s\n", vk_path, token[0]);
            continue;
        }

        vks[i].scancode = strtol(token[1], NULL, 0);
        vks[i].centerx = strtol(token[2], NULL, 0);
        vks[i].centery = strtol(token[3], NULL, 0);
        vks[i].width = strtol(token[4], NULL, 0);
        vks[i].height = strtol(token[5], NULL, 0);
    }

    return 0;
}

static int abs_mt_pos_horizontal = ABS_MT_POSITION_X;
static int abs_mt_pos_vertical = ABS_MT_POSITION_Y;
static int touch_device_init(int fd) {
    unsigned int i;
    for(i=0; i <= abs_count; i++) {
        if (fd == abs_device[i].fd && abs_device[i].ignored == 1)
            return -1;
    }

    abs_device[abs_count].fd = fd;

    ssize_t len;
    len = ioctl(fd,EVIOCGNAME(sizeof(abs_device[abs_count].deviceName)),abs_device[abs_count].deviceName);
    if (len <= 0) {
        LOGE("Unable to query event object.\n");
        abs_device[abs_count].ignored = 1;
        abs_count++;
        return -1;
    }
    
#ifndef TOUCH_INPUT_BLACKLIST
    if (strcmp(abs_device[abs_count].deviceName, "bma250") == 0 || strcmp(abs_device[abs_count].deviceName, "bma150") == 0) {
        abs_device[abs_count].ignored = 1;
        abs_count++;
        return -1;
    }
#else
    char* bl = strdup(EXPAND(TOUCH_INPUT_BLACKLIST));
    char* blacklist = strtok(bl, "\n");

    while (blacklist != NULL) {
        if (strcmp(abs_device[abs_count].deviceName, blacklist) == 0) {
			printf("blacklisting %s input device\n", abs_device[abs_count].deviceName);
	        abs_device[abs_count].ignored = 1;
	        abs_count++;
	        return -1;
	    }
        blacklist = strtok(NULL, "\n");
    }
    free(bl);
#endif

    struct input_absinfo absinfo_x;
    struct input_absinfo absinfo_y;

    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo_x);
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo_y);
    if (absinfo_x.maximum <= 0 || absinfo_y.maximum <= 0) {
        abs_device[abs_count].ignored = 1;
        abs_count++;
        return -1;
    }

    touch_is_init = 1;

    board_min_x = absinfo_x.minimum;
    board_max_x = absinfo_x.maximum;
    board_min_y = absinfo_y.minimum;
    board_max_y = absinfo_y.maximum;

#ifdef RECOVERY_TOUCHSCREEN_SWAP_XY
        int old_val;
        old_val = board_min_x;
        board_min_x = board_min_y;
        board_min_y = old_val;
        old_val = board_max_x;
        board_max_x = board_max_y;
        board_max_y = old_val;

        abs_mt_pos_horizontal = ABS_MT_POSITION_Y;
        abs_mt_pos_vertical = ABS_MT_POSITION_X;
#endif

    if (vk_init(abs_device[abs_count]) == 0)
        device_has_vk = 1;

    return 0;
}

static void toggle_key_pressed(int key_code, int pressed) {
    pthread_mutex_lock(&key_queue_mutex);
    key_pressed[key_code] = pressed;
    pthread_mutex_unlock(&key_queue_mutex);
}

static int input_buttons() {
    pthread_mutex_lock(&gUpdateMutex);

    int final_code = -1;
    int start_draw = 0;
    int end_draw = 0;

    gr_surface surface = gVirtualKeys;
    int fbh = gr_fb_height();
    int fbw = gr_fb_width();
    int vk_width = gr_get_width(surface);
    int keyhight = gr_get_height(surface);
    int keywidth = vk_width / 4;
    int keyoffset = (fbw - vk_width) / 2; 

    if (touch_x < (keywidth + keyoffset + 1)) {
        // down button
        final_code = KEY_DOWN; // 108
        start_draw = keyoffset;
        end_draw = keywidth + keyoffset;
    } else if (touch_x < ((keywidth * 2) + keyoffset + 1)) {
        // up button
        final_code = KEY_UP; // 103
        start_draw = keyoffset + keywidth + 1;
        end_draw = (keywidth * 2) + keyoffset;
    } else if (touch_x < ((keywidth * 3) + keyoffset + 1)) {
        // back button
        final_code = KEY_BACK; // 158
        start_draw = keyoffset + (keywidth * 2) + 1;
        end_draw = (keywidth * 3) + keyoffset;
    } else if (touch_x < ((keywidth * 4) + keyoffset + 1)) {
        // enter key
        final_code = KEY_ENTER; // 28
        start_draw = keyoffset + (keywidth * 3) + 1;
        end_draw = (keywidth * 4) + keyoffset;
    } else {
        return final_code;
    }

    gr_color(50, 50, 50, 180); // grey
    gr_fill(0, fbh-keyhight, 
            vk_width+keyoffset, fbh-keyhight+4);

    gr_color(242, 38, 19, 255);
    gr_fill(start_draw, fbh-keyhight,
            end_draw, fbh-keyhight+4);
    gr_flip(); // makes visible the draw buffer we did above, without redrawing whole screen
    pthread_mutex_unlock(&gUpdateMutex);
    
    return final_code;
}

static int input_vk() {
    int i = 0;
    while (i < vk_count) {
        if (touch_x > (vks[i].centerx - (vks[i].width / 2)) && 
                touch_x < (vks[i].centerx + (vks[i].width / 2))) {
            return vks[i].scancode;
        }
        i++;
    }
    return -1;
}

static int current_slot = 0;
static int lastWasSynReport = 0;
static int touchReleaseOnNextSynReport = 0;
static int use_tracking_id_negative_as_touch_release = 0;

static int touch_track(int fd, struct input_event ev) {
    if (fd != abs_device[abs_count].fd)
        return -1;

#ifdef BOARD_USE_PROTOCOL_TYPE_B
    if (!use_tracking_id_negative_as_touch_release)
        use_tracking_id_negative_as_touch_release = 1;
#endif

    int finger_up = 0;
    if (ev.type == EV_ABS) {

        if (ev.code == ABS_MT_SLOT) { //47
            current_slot = ev.value;
            return 1;
        }
        if (current_slot != 0)
            return 1;

        switch (ev.code)
        {
            case ABS_X: //00
                break;

            case ABS_Y: //01
                break;

            case ABS_MT_POSITION: //2a
                if (ev.value == (1 << 31))
                    lastWasSynReport = 1;
                else
                    lastWasSynReport = 0;
                break;

            case ABS_MT_TOUCH_MAJOR: //30
                if (ev.value == 0) {
                    // We're in a touch release, although some devices will still send positions as well
                    touchReleaseOnNextSynReport = 1;
                }
                break;

            case ABS_MT_PRESSURE: //3a
                if (ev.value == 0) {
                    // We're in a touch release, although some devices will still send positions as well
                    touchReleaseOnNextSynReport = 1;
                }
                break;

            case ABS_MT_POSITION_X: //35
                break;

            case ABS_MT_POSITION_Y: //36
                break;

            case ABS_MT_TOUCH_MINOR: //31
                break;

            case ABS_MT_WIDTH_MAJOR: //32
                break;

            case ABS_MT_WIDTH_MINOR: //33
                break;

            case ABS_MT_TRACKING_ID: //39
                if (ev.value < 0) {
                    touchReleaseOnNextSynReport = 2;
                    if (!use_tracking_id_negative_as_touch_release)
                        use_tracking_id_negative_as_touch_release = 1;
                }
                break;

            default:
                return 1;
        }

        if (ev.code != ABS_MT_POSITION) {
            lastWasSynReport = 0;
            return 1;
        }
    }

    if (ev.code != ABS_MT_POSITION && (ev.type != EV_SYN || (ev.code != SYN_REPORT && ev.code != SYN_MT_REPORT))) {
        lastWasSynReport = 0;
        return 1;
    }

    if (ev.code == SYN_MT_REPORT)
        return 1;

    if (!use_tracking_id_negative_as_touch_release) {
        if (lastWasSynReport == 1 || touchReleaseOnNextSynReport == 1)
            finger_up = 1;
    } else if (touchReleaseOnNextSynReport == 2)
        finger_up = 1;

    if (finger_up) {
        touchReleaseOnNextSynReport = 0;
         return 0;
    }

    lastWasSynReport = 1;
    return 1;
}

static int key_handle_input (struct input_event ev) {
    if (ev.type != EV_KEY || ev.code > KEY_MAX)
        return 0;

    pthread_mutex_lock(&key_queue_mutex);

    key_pressed[ev.code] = ev.value;

    const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
    if (ev.value > 0 && key_queue_len < queue_max) {
        key_queue[key_queue_len] = ev.code;
        ++key_queue_len;

        if (boardEnableKeyRepeat) {
            struct timeval now;
            gettimeofday(&now, NULL);

            key_press_time[ev.code] = (now.tv_sec * 1000) + (now.tv_usec / 1000);
            key_last_repeat[ev.code] = 0;
        }

        pthread_cond_signal(&key_queue_cond);
    }
    pthread_mutex_unlock(&key_queue_mutex);

    if (ev.value > 0 && device_reboot_now(key_pressed, ev.code)) {
        reboot_main_system(ANDROID_RB_RESTART, 0, 0);
    }

    return 0;
}

static int touch_accuracy;
static int get_touch_accuracy() {
	char defined_touch[PROPERTY_VALUE_MAX];
	char tmp[4];
	
	property_get("ro.ctr.touch_accuracy", defined_touch, "error");
	if (strcmp(defined_touch, "error") == 0) {
		touch_accuracy = 7;
		return 0;		
	} else {
		snprintf(tmp, 4, "%s", defined_touch);
		touch_accuracy = atoi(tmp);
		return 0;
	}
}

static int touch_handle_input(int fd, struct input_event ev) {
    int ret;
    int fbh = gr_fb_height();
    int fbw = gr_fb_width();
    get_touch_accuracy();

    if (ev.type == EV_KEY) {
        if (ignore_key_action) {
            ev.code = KEY_ESC;
            if (ev.value == 0)
                ignore_key_action = 0;
        }
        key_handle_input(ev);
        return 1;
    }

    if (ev.type != EV_ABS && ev.type != EV_SYN)
        return 0;

    if (!touch_is_init) {
        if (ev.type != EV_ABS) {
            return 0;
        }
        if (touch_device_init(fd) != 0) {
            return 1;
        }
    }

    ret = touch_track(fd, ev);
    if (ret == -1) {
        return 1;
    } else if (ret == 0) {
        in_touch = 0;
        t_old_last_touch = t_last_touch;
        t_last_touch = timenow_msec();

        if (ignore_key_action) {
            ignore_key_action = 0;
            ev.type = EV_KEY;
            ev.code = KEY_ESC;
            ev.value = 1;
        } else if (vbutton_pressed != -1) {
            toggle_key_pressed(vbutton_pressed, 0);
            vbutton_pressed = -1;
            now_scrolling = 0;
        } else if (vk_pressed != -1) {
            vk_pressed = -1;
            now_scrolling = 0;
        } else if (now_scrolling != 0) {
            if (scroll_speed == 0 || (t_last_touch - t_last_scroll_y) > 300) {
                scroll_speed = 0;
                now_scrolling = 0;
            } else {
                ev.type = EV_KEY;
                ev.code = KEY_PAGEDOWN;
                ev.value = 1;
            }
        } else if (touch_y < (fbh - virtual_keys_h) && touch_y != TOUCH_RESET_POS) {
            int old_last_touched_menu = last_touched_menu;
            last_touched_menu = ui_valid_menu_touch(touch_y);
            if (abs(touch_x - first_x) <= touch_accuracy && abs(touch_y - first_y) <= touch_accuracy) {
                if (last_touched_menu >= 0 && last_touched_menu == first_touched_menu && last_touched_menu == ui_get_selected_item()) {
                        ev.type = EV_KEY;
                        ev.code = KEY_ENTER; 
                        ev.value = 1;
                }
	        }
		}
        allow_long_press_move = 0;
        reset_gestures();
    } else if (ev.type == EV_ABS && current_slot == 0) {
        if (in_touch == 0) {
            in_touch = 1;
            allow_long_press_move = 1;
            scroll_speed = 0; 
            t_first_touch = timenow_msec();

            if (!ignore_key_action && now_scrolling != 0) {
                pthread_mutex_lock(&key_queue_mutex);
                if (key_queue_len == 0 && (t_first_touch - t_last_touch) > 650)
                    now_scrolling = 0;

                key_queue_len = 0;
                pthread_mutex_unlock(&key_queue_mutex);
            }

            if (vibration_enabled) vibrate(VIBRATOR_TIME_MS);
        }

        if (ev.code == abs_mt_pos_horizontal) {
            float touch_x_rel = (float)(ev.value - board_min_x) / (float)(board_max_x - board_min_x + 1);       
            touch_x = touch_x_rel * fbw;

#ifdef RECOVERY_TOUCHSCREEN_FLIP_X
                touch_x = fbw - touch_x;
#endif

            if (first_x == TOUCH_RESET_POS) {
                first_x = touch_x;
            }

            if (abs(touch_x - first_x) > (3 * touch_accuracy))
                allow_long_press_move = 0;

            if (touch_y > (fbh - virtual_keys_h) && touch_y < fbh) {

                if (!ignore_key_action && vbutton_pressed == -1 && (ret = input_buttons()) != -1) {
                    vbutton_pressed = ret;
                    now_scrolling = 0;
                    ev.type = EV_KEY;
                    ev.code = vbutton_pressed;
                    ev.value = 1;
                }
            } else if (!ignore_key_action && device_has_vk && touch_y > fbh && vk_pressed == -1 && (ret = input_vk()) != -1) {
                vk_pressed = ret;
                now_scrolling = 0;
                ev.type = EV_KEY;
                ev.code = vk_pressed;
                ev.value = 1;
            } else if (vbutton_pressed != -1) {
                toggle_key_pressed(vbutton_pressed, 0);
                vbutton_pressed = -1;
            }
        } else if (ev.code == abs_mt_pos_vertical) {
            float touch_y_rel = (float)(ev.value - board_min_y) / (float)(board_max_y - board_min_y + 1);       
            touch_y = touch_y_rel * fbh;

#ifdef RECOVERY_TOUCHSCREEN_FLIP_Y
                touch_y = fbh - touch_y;
#endif

            if (first_y == TOUCH_RESET_POS) {
                first_y = touch_y;
                last_scroll_y = touch_y;
                t_last_scroll_y = timenow_msec();
                first_touched_menu = ui_valid_menu_touch(touch_y);
                if (touch_y < (fbh - virtual_keys_h) && first_touched_menu >= 0 && now_scrolling == 0) {
                    touch_sel = 1;
                    ev.type = EV_KEY;
                    ev.code = KEY_PAGEUP;
                    ev.value = 1;
                }
            }

            if (abs(touch_y - first_y) > (3 * touch_accuracy))
                allow_long_press_move = 0;

            int val = touch_y - last_scroll_y;
            if (!ignore_key_action && abs(val) > SCROLL_SENSITIVITY && ui_valid_menu_touch(touch_y) >= 0) {
                long long t_now = timenow_msec();
                scroll_speed = (long)(1000 * ((double)(abs(val)) / (double)(t_now - t_last_scroll_y)));
                last_scroll_y = touch_y;
                t_last_scroll_y = t_now;

                if (val > 0) {
                    now_scrolling = -1;
                    ev.type = EV_KEY;
                    ev.code = KEY_PAGEDOWN;
                    ev.value = 1;
                } else {
                    now_scrolling = 1;
                    ev.type = EV_KEY;
                    ev.code = KEY_PAGEDOWN;
                    ev.value = 1;
                }
            }

            if (touch_y < (fbh - virtual_keys_h)) {
                if (vbutton_pressed != -1) {
                    toggle_key_pressed(vbutton_pressed, 0);
                    vbutton_pressed = -1;
                } else if (!ignore_key_action && allow_long_press_move &&
                            (timenow_msec() - t_first_touch) > 2000 && ui_valid_menu_touch(touch_y) == -1) {
                    ev.type = EV_KEY;
                    ev.code = KEY_LEFTBRACE;
                    ev.value = 1;
                    if (vibration_enabled) vibrate(VIBRATOR_TIME_MS);
                    allow_long_press_move = 0;
                }
            } else if (touch_x != TOUCH_RESET_POS && touch_y > (fbh - virtual_keys_h) && touch_y < fbh) {
                if (!ignore_key_action && vbutton_pressed == -1 && (ret = input_buttons()) != -1) {
                    vbutton_pressed = ret;
                    now_scrolling = 0;
                    ev.type = EV_KEY;
                    ev.code = vbutton_pressed;
                    ev.value = 1;
                }
            } else if (!ignore_key_action && device_has_vk && vk_pressed == -1 &&
                       touch_x != TOUCH_RESET_POS && (ret = input_vk()) != -1) {
                now_scrolling = 0;
                vk_pressed = ret;
                ev.type = EV_KEY;
                ev.code = vk_pressed;
                ev.value = 1;
            }
        }
    }

    if (ev.type != EV_KEY || ev.code > KEY_MAX)
        return 1;

    pthread_mutex_lock(&key_queue_mutex);
    if (vbutton_pressed != -1) {
        key_pressed[vbutton_pressed] = 1;
    }

    const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
    if (ev.value > 0 && key_queue_len < queue_max) {
        if (now_scrolling != 0) {
            if (in_touch) {
                key_queue_len = 0;
                key_queue[key_queue_len] = ev.code;
                ++key_queue_len;
            } else {
                int scroll_persist = 4 * (scroll_speed / gr_fb_height());
                if (scroll_persist != 0 && scroll_persist < 4)
                    scroll_persist = 4;
                if (scroll_persist > queue_max)
                    scroll_persist = queue_max - 1;

                key_queue_len = 0;
                while (key_queue_len < scroll_persist) {
                    key_queue[key_queue_len] = ev.code;
                    ++key_queue_len;
                }
            }
        } else {
            key_queue[key_queue_len] = ev.code;
            ++key_queue_len;
        }

        if (boardEnableKeyRepeat) {
            struct timeval now;
            gettimeofday(&now, NULL);

            key_press_time[ev.code] = (now.tv_sec * 500) + (now.tv_usec / 1000);
            key_last_repeat[ev.code] = 0;
        }

        pthread_cond_signal(&key_queue_cond);
    }
    pthread_mutex_unlock(&key_queue_mutex);

    return 1;
}

static void touch_init() {
    boardEnableKeyRepeat = 1;
    boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_UP;
    boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_DOWN;
    boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_VOLUMEUP;
    boardRepeatableKeys[boardNumRepeatableKeys++] = KEY_VOLUMEDOWN;
}


/*******************************/
/*   End touch handling code   */
/*   Original port by PhilZ    */
/*******************************/


static int rel_sum = 0;
static int input_callback(int fd, uint32_t epevents, void* data) {
    struct input_event ev;
    int ret;
    int fake_key = 0;

    ret = ev_get_input(fd, epevents, &ev);
    if (ret)
        return -1;

    if (touch_handle_input(fd, ev))
        return 0;

    if (ev.type == EV_SYN) {
        return 0;
    } else if (ev.type == EV_REL) {
        if (ev.code == REL_Y) {
            // accumulate the up or down motion reported by
            // the trackball.  When it exceeds a threshold
            // (positive or negative), fake an up/down
            // key event.
            rel_sum += ev.value;
            if (rel_sum > 3) {
                fake_key = 1;
                ev.type = EV_KEY;
                ev.code = KEY_DOWN;
                ev.value = 1;
                rel_sum = 0;
            } else if (rel_sum < -3) {
                fake_key = 1;
                ev.type = EV_KEY;
                ev.code = KEY_UP;
                ev.value = 1;
                rel_sum = 0;
            }
        }
    } else {
        rel_sum = 0;
    }

    if (ev.type != EV_KEY || ev.code > KEY_MAX)
        return 0;

    if (ev.value == 2) {
        boardEnableKeyRepeat = 0;
    }

    pthread_mutex_lock(&key_queue_mutex);
    if (!fake_key) {
        // our "fake" keys only report a key-down event (no
        // key-up), so don't record them in the key_pressed
        // table.
        key_pressed[ev.code] = ev.value;
    }
    const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
    if (ev.value > 0 && key_queue_len < queue_max) {
        key_queue[key_queue_len++] = ev.code;

        if (boardEnableKeyRepeat) {
            struct timeval now;
            gettimeofday(&now, NULL);

            key_press_time[ev.code] = (now.tv_sec * 1000) + (now.tv_usec / 1000);
            key_last_repeat[ev.code] = 0;
        }

        pthread_cond_signal(&key_queue_cond);
    }
    pthread_mutex_unlock(&key_queue_mutex);

    if (ev.value > 0 && device_reboot_now(key_pressed, ev.code)) {
        reboot_main_system(ANDROID_RB_RESTART, 0, 0);
    }

    return 0;
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void *input_thread(void *cookie) {
    for (;;) {
        if (!ev_wait(-1))
            ev_dispatch();
    }
    return NULL;
}

void ui_init(void) {
    ui_has_initialized = 1;
    gr_init();
    ev_init(input_callback, NULL);
    touch_init();

    text_col = text_row = 0;
    text_rows = gr_fb_height() / CHAR_HEIGHT;
    max_menu_rows = text_rows - MIN_LOG_ROWS;

    if (max_menu_rows > MENU_MAX_ROWS)
        max_menu_rows = MENU_MAX_ROWS;
    if (text_rows > MAX_ROWS) text_rows = MAX_ROWS;
    text_top = 1;

    text_cols = gr_fb_width() / CHAR_WIDTH;
    if (text_cols > MAX_COLS - 1) text_cols = MAX_COLS - 1;

    int i;
    for (i = 0; BITMAPS[i].name != NULL; ++i) {
        int result = res_create_display_surface(BITMAPS[i].name, BITMAPS[i].surface);
        if (result < 0) {
            LOGE("Missing bitmap %s\n(Code %d)\n", BITMAPS[i].name, result);
        }
    }

    gProgressBarIndeterminate = malloc(ui_parameters.indeterminate_frames *
                                       sizeof(gr_surface));
    for (i = 0; i < ui_parameters.indeterminate_frames; ++i) {
        char filename[40];
        // "indeterminate01.png", "indeterminate02.png", ...
        sprintf(filename, "indeterminate%02d", i+1);
        int result = res_create_display_surface(filename, gProgressBarIndeterminate+i);
        if (result < 0) {
            LOGE("Missing bitmap %s\n(Code %d)\n", filename, result);
        }
    }

    if (ui_parameters.installing_frames > 0) {
        gInstallationOverlay = malloc(ui_parameters.installing_frames *
                                      sizeof(gr_surface));
        for (i = 0; i < ui_parameters.installing_frames; ++i) {
            char filename[40];
            // "icon_installing_overlay01.png",
            // "icon_installing_overlay02.png", ...
            sprintf(filename, "icon_installing_overlay%02d", i+1);
            int result = res_create_display_surface(filename, gInstallationOverlay+i);
            if (result < 0) {
                LOGE("Missing bitmap %s\n(Code %d)\n", filename, result);
            }
        }

        // Adjust the offset to account for the positioning of the
        // base image on the screen.
        if (gBackgroundIcon[BACKGROUND_ICON_INSTALLING] != NULL) {
            gr_surface bg = gBackgroundIcon[BACKGROUND_ICON_INSTALLING];
            ui_parameters.install_overlay_offset_x +=
                (gr_fb_width() - gr_get_width(bg)) / 2;
            ui_parameters.install_overlay_offset_y +=
                (gr_fb_height() - gr_get_height(bg)) / 2;
        }
    } else {
        gInstallationOverlay = NULL;
    }

    pthread_t t;
    pthread_create(&t, NULL, progress_thread, NULL);
    pthread_create(&t, NULL, input_thread, NULL);
}

void ui_set_background(int icon) {
    pthread_mutex_lock(&gUpdateMutex);
    gCurrentIcon = icon;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_indeterminate_progress() {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    if (gProgressBarType != PROGRESSBAR_TYPE_INDETERMINATE) {
        gProgressBarType = PROGRESSBAR_TYPE_INDETERMINATE;
        update_progress_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_progress(float portion, int seconds) {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NORMAL;
    gProgressScopeStart += gProgressScopeSize;
    gProgressScopeSize = portion;
    gProgressScopeTime = now();
    gProgressScopeDuration = seconds;
    gProgress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_set_progress(float fraction) {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && fraction > gProgress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(gProgressBarIndeterminate[0]);
        float scale = width * gProgressScopeSize;
        if ((int) (gProgress * scale) != (int) (fraction * scale)) {
            gProgress = fraction;
            update_progress_locked();
        }
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_reset_progress() {
    if (!ui_has_initialized)
        return;

    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NONE;
    gProgressScopeStart = 0;
    gProgressScopeSize = 0;
    gProgressScopeTime = 0;
    gProgressScopeDuration = 0;
    gProgress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

int ui_get_text_cols() {
    return text_cols;
}

char* word_wrap (char* buffer, const unsigned int buffer_sz, const char* string, const unsigned int string_len, const unsigned int max_line_width) {
assert(buffer != NULL && buffer_sz > 0 && string != NULL && string_len > 0 && max_line_width > 0);

    unsigned int i = 0, cur_line_width = 0;
    int last_space = -1;
    for (i = 0; i < string_len && i < buffer_sz - 1; i++)
    {
        buffer[i] = string[i]; 
        cur_line_width++;   
        if (isspace(buffer[i])) 
            last_space = i;
        if (cur_line_width > max_line_width && last_space >= 0)
        {
            buffer[last_space]  = '\n'; 
            cur_line_width      = i - last_space; 
        }
    }
    buffer[i] = '\0';
    return buffer;
}

void ui_print(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 512, fmt, ap);
    va_end(ap);
    text_cols = gr_fb_width() / CHAR_WIDTH;
    snprintf(buf, 512, "%s", word_wrap(buf, 512, buf, strlen(buf), text_cols));

    if (ui_log_stdout)
        fputs(buf, stdout);

    if (!ui_has_initialized)
        return;

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_printlogtail(int nb_lines) {
    char * log_data;
    char tmp[PATH_MAX];
    FILE * f;
    int line=0;
    //don't log output to recovery.log
    ui_log_stdout=0;
    sprintf(tmp, "tail -n %d /tmp/recovery.log > /tmp/tail.log", nb_lines);
    __system(tmp);
    f = fopen("/tmp/tail.log", "rb");
    if (f != NULL) {
        while (line < nb_lines) {
            log_data = fgets(tmp, PATH_MAX, f);
            if (log_data == NULL) break;
            ui_print("%s", tmp);
            line++;
        }
        fclose(f);
    }
    ui_print("Return to menu with any key.\n");
    ui_log_stdout=1;
}

int ui_start_menu(const char** headers, char** items, int initial_selection) {
    int i;
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        for (i = 0; i < text_rows; ++i) {
            if (headers[i] == NULL) break;
            int offset = 1;
            if (i == 0) offset = ui_menu_header_offset();
            strncpy(menu[i], headers[i], text_cols - offset);
            menu[i][text_cols - offset] = '\0';
        }
        menu_top = i;
        for (; i < MENU_MAX_ROWS; ++i) {
            if (items[i-menu_top] == NULL) break;
            strcpy(menu[i], MENU_ITEM_HEADER);
            strncpy(menu[i] + MENU_ITEM_HEADER_LENGTH, items[i-menu_top], MENU_MAX_COLS - 1 - MENU_ITEM_HEADER_LENGTH);
            menu[i][MENU_MAX_COLS-1] = '\0';
        }

        if (gShowBackButton && !ui_root_menu) {
            strcpy(menu[i], " - <<<<  Go Back  <<<<");
            ++i;
        }

        menu_items = i - menu_top;
        show_menu = 1;
        menu_sel = menu_show_start = initial_selection;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    if (gShowBackButton && !ui_root_menu) {
        return menu_items - 1;
    }
    return menu_items;
}

int ui_menu_select(int sel) {
    int old_sel;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0) {
        old_sel = menu_sel;
        menu_sel = sel;

        if (menu_sel < 0) menu_sel = menu_items + menu_sel;
        if (menu_sel >= menu_items) menu_sel = menu_sel - menu_items;


        if (menu_sel < menu_show_start && menu_show_start > 0) {
            menu_show_start = menu_sel;
        }

        if (menu_sel - menu_show_start + menu_top >= max_menu_rows) {
            menu_show_start = menu_sel + menu_top - max_menu_rows + 1;
        }

        sel = menu_sel;

        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return sel;
}

void ui_end_menu() {
    int i;
    pthread_mutex_lock(&gUpdateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

int ui_text_visible() {
    pthread_mutex_lock(&gUpdateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&gUpdateMutex);
    return visible;
}

int ui_text_ever_visible() {
    pthread_mutex_lock(&gUpdateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&gUpdateMutex);
    return ever_visible;
}

void ui_show_text(int visible) {
    pthread_mutex_lock(&gUpdateMutex);
    show_text = visible;
    if (show_text) show_text_ever = 1;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_clear_text()
{
    memset(text, 0, sizeof(text));
    text_col = text_row = 0;
}

static int usb_connected() {
    int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
    if (fd < 0) {
        printf("failed to open /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
        return 0;
    }

    char buf;
    /* USB is connected if android_usb state is CONNECTED or CONFIGURED */
    int connected = (read(fd, &buf, 1) == 1) && (buf == 'C');
    if (close(fd) < 0) {
        printf("failed to close /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
    }
    return connected;
}

void ui_cancel_wait_key() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue[key_queue_len] = -2;
    key_queue_len++;
    pthread_cond_signal(&key_queue_cond);
    pthread_mutex_unlock(&key_queue_mutex);
}

int ui_wait_key() {
    if (boardEnableKeyRepeat) return ui_wait_key_with_repeat();

    pthread_mutex_lock(&key_queue_mutex);
    int timeouts = UI_WAIT_KEY_TIMEOUT_SEC;

    // Time out after REFRESH_TIME_USB_INTERVAL seconds to catch volume changes, and loop for
    // UI_WAIT_KEY_TIMEOUT_SEC to restart a device not connected to USB
    do {
        struct timeval now;
        struct timespec timeout;
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec;
        timeout.tv_nsec = now.tv_usec * 1000;
        timeout.tv_sec += REFRESH_TIME_USB_INTERVAL;

        int rc = 0;
        while (key_queue_len == 0 && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                        &timeout);
        }
        timeouts -= REFRESH_TIME_USB_INTERVAL;
    } while ((timeouts > 0 || usb_connected()) && key_queue_len == 0);

    int key = -1;
    if (key_queue_len > 0) {
        key = key_queue[0];
        memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
    }
    pthread_mutex_unlock(&key_queue_mutex);
    return key;
}

static int key_can_repeat(int key) {
    int k = 0;
    for (;k < boardNumRepeatableKeys; ++k) {
        if (boardRepeatableKeys[k] == key) {
            break;
        }
    }
    if (k < boardNumRepeatableKeys) return 1;
    return 0;
}

static int ui_wait_key_with_repeat() {
    int key = -1;

    // Loop to wait for more keys
    do {
        int timeouts = UI_WAIT_KEY_TIMEOUT_SEC;
        int rc = 0;
        struct timeval now;
        struct timespec timeout;
        pthread_mutex_lock(&key_queue_mutex);
        while (key_queue_len == 0 && timeouts > 0) {
            gettimeofday(&now, NULL);
            timeout.tv_sec = now.tv_sec;
            timeout.tv_nsec = now.tv_usec * 1000;
            timeout.tv_sec += REFRESH_TIME_USB_INTERVAL;

            rc = 0;
            while (key_queue_len == 0 && rc != ETIMEDOUT) {
                rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                            &timeout);
            }
            timeouts -= REFRESH_TIME_USB_INTERVAL;
        }
        pthread_mutex_unlock(&key_queue_mutex);

        if (rc == ETIMEDOUT && !usb_connected()) {
            return -1;
        }

        // Loop to wait wait for more keys, or repeated keys to be ready.
        while (1) {
            unsigned long now_msec;

            gettimeofday(&now, NULL);
            now_msec = (now.tv_sec * 1000) + (now.tv_usec / 1000);

            pthread_mutex_lock(&key_queue_mutex);

            // Replacement for the while conditional, so we don't have to lock the entire
            // loop, because that prevents the input system from touching the variables while
            // the loop is running which causes problems.
            if (key_queue_len == 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                break;
            }

            key = key_queue[0];
            memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);

            // sanity check the returned key.
            if (key < 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                return key;
            }

            // Check for already released keys and drop them if they've repeated.
            if (!key_pressed[key] && key_last_repeat[key] > 0) {
                pthread_mutex_unlock(&key_queue_mutex);
                continue;
            }

            if (key_can_repeat(key)) {
                // Re-add the key if a repeat is expected, since we just popped it. The
                // if below will determine when the key is actually repeated (returned)
                // in the mean time, the key will be passed through the queue over and
                // over and re-evaluated each time.
                if (key_pressed[key]) {
                    key_queue[key_queue_len] = key;
                    key_queue_len++;
                }
                if ((now_msec > key_press_time[key] + UI_KEY_WAIT_REPEAT && now_msec > key_last_repeat[key] + UI_KEY_REPEAT_INTERVAL) ||
                        key_last_repeat[key] == 0) {
                    key_last_repeat[key] = now_msec;
                } else {
                    // Not ready
                    pthread_mutex_unlock(&key_queue_mutex);
                    continue;
                }
            }
            pthread_mutex_unlock(&key_queue_mutex);
            return key;
        }
    } while (1);

    return key;
}

int ui_key_pressed(int key) {
    // This is a volatile static array, don't bother locking
    return key_pressed[key];
}

void ui_clear_key_queue() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

void ui_set_log_stdout(int enabled) {
    ui_log_stdout = enabled;
}

int ui_should_log_stdout() {
    return ui_log_stdout;
}

void ui_set_show_text(int value) {
    show_text = value;
}

void ui_set_showing_back_button(int showBackButton) {
    gShowBackButton = showBackButton;
}

int ui_get_showing_back_button() {
    return 1;
}

int ui_is_showing_back_button() {
    return gShowBackButton && !ui_root_menu;
}

int ui_get_selected_item() {
  return menu_sel;
}

int ui_handle_key(int key, int visible) {
    return touch_handle_key(key, visible);
}

int key_press_event() {
    int key = ui_check_key();
    int action = ui_handle_key(key, 1);
    return action;
}

int is_ui_initialized() {
    return ui_has_initialized;
}

void ui_delete_line() {
    pthread_mutex_lock(&gUpdateMutex);
    text[text_row][0] = '\0';
    text_row = (text_row - 1 + text_rows) % text_rows;
    text_col = 0;
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_rainbow_mode() {
    static int colors[] = { 255, 0, 0,        // red
                            255, 127, 0,      // orange
                            255, 255, 0,      // yellow
                            0, 255, 0,        // green
                            60, 80, 255,      // blue
                            143, 0, 255 };    // violet

    gr_color(colors[cur_rainbow_color], colors[cur_rainbow_color+1], colors[cur_rainbow_color+2], 255);
    cur_rainbow_color += 3;
    if (cur_rainbow_color >= (sizeof(colors) / sizeof(colors[0]))) cur_rainbow_color = 0;
}
