#include <linux/input.h>

#include "common.h"
#include "extendedcommands.h"
#include "recovery_ui.h"

int device_handle_key(int key_code, int visible) {
    if (visible) {
        switch (key_code) {
			case KEY_RIGHTSHIFT:
			case KEY_DOWN:
			case KEY_VOLUMEDOWN:
			case KEY_MENU:
                return HIGHLIGHT_DOWN;

			case KEY_LEFTSHIFT:
			case KEY_UP:
			case KEY_VOLUMEUP:
			case KEY_SEARCH:
                return HIGHLIGHT_UP;

			case KEY_ENTER:
			case KEY_POWER:
			case BTN_MOUSE:
			case KEY_HOME:
			case KEY_HOMEPAGE:
			case KEY_SEND:
                return SELECT_ITEM;

            case KEY_END:
            case KEY_BACKSPACE:
            case KEY_BACK:
                if (!ui_root_menu) {
                    return GO_BACK;
                }
        }
    }

    return NO_ACTION;
}
