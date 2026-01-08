#ifndef TINYBASIC_TERMINAL_H
#define TINYBASIC_TERMINAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "user_datatypes.h"
#include "user.h"
#include "tcp_interface.h"

/* color definitions */
#define TERM_COLOR_BLACK         0
#define TERM_COLOR_RED           1
#define TERM_COLOR_GREEN         2
#define TERM_COLOR_YELLOW        3
#define TERM_COLOR_BLUE          4
#define TERM_COLOR_MAGENTA       5
#define TERM_COLOR_CYAN          6
#define TERM_COLOR_LIGHT_GRAY    7
#define TERM_COLOR_DARK_GRAY     8
#define TERM_COLOR_LIGHT_RED     9
#define TERM_COLOR_LIGHT_GREEN   10
#define TERM_COLOR_LIGHT_YELLOW  11
#define TERM_COLOR_LIGHT_BLUE    12
#define TERM_COLOR_LIGHT_MAGENTA 13
#define TERM_COLOR_LIGHT_CYAN    14
#define TERM_COLOR_WHITE         15

/* lifecycle */
void terminal_init(user_context_t *termid, uint16_t cols, uint16_t rows);
void terminal_deinit(user_context_t *termid);

/* basic output */
int  terminal_putc(user_context_t *termid, char c);
int  terminal_puts(user_context_t *termid, const char *s);
int  terminal_write(user_context_t *termid, const char *buf, size_t len);
int  terminal_printf(user_context_t *termid, const char *fmt, ...) __attribute__((format(printf,2,3)));
int  terminal_vprintf(user_context_t *termid, const char *fmt, va_list ap);

/* input (optional backends may implement) */
int terminal_read(user_context_t *termid, char *buf, size_t len);

/* control */
void terminal_clear(user_context_t *termid);
void terminal_flush(user_context_t *termid);
void terminal_set_cursor(user_context_t *termid, uint16_t x, uint16_t y);
void terminal_get_cursor(user_context_t *termid, uint16_t *x, uint16_t *y);
void terminal_set_colors(user_context_t *termid, uint8_t fg, uint8_t bg);
void terminal_get_size(user_context_t *termid, uint16_t *cols, uint16_t *rows);

#endif /* TINYBASIC_TERMINAL_H */