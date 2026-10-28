#pragma once
#include "ui_screen.h"
#include "app_state.h"
const char *ui_icon_name(unsigned icon);
void ui_draw_icon(u8g2_t *display, int x, int y, unsigned icon);
void ui_draw_product_logo(u8g2_t *display, int x, int y);
