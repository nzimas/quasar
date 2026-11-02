/* SPDX-License-Identifier: GPL-3.0-only */
static void splash_draw(const char *build)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 70, 240, text_w(&FONT_L,"QUASAR") <= 232 ? &FONT_L : &FONT_M, "QUASAR", C_WHITE, 1);
    draw_text_box(0, 150, 240, &FONT_S, build, C_GREY, 1);
}
