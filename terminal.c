// filepath: /home/brian/Pico_2_w/tinybasic/terminal.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include "terminal.h"

void terminal_init(user_context_t *termid, uint16_t cols, uint16_t rows)
{
    terminal_t *t = &termid->term;

    if (!t) return;
    if(cols) t->cols = cols;
    if(rows) t->rows = rows;
    t->cursor_x = 0;
    t->cursor_y = 0;
    t->fg_color = 7;        // default white-ish
    t->bg_color = 0;        // default black
}

void terminal_deinit(user_context_t *termid)
{
    terminal_t *t = &termid->term;

    if (!t) return;
    /* If backend was a FILE* we don't close it implicitly; caller owns it.
       Just clear pointer and reset state. */
    t->cols = t->rows = t->cursor_x = t->cursor_y = 0;
    t->fg_color = t->bg_color = 0;
}

int terminal_putc(user_context_t *termid, char c)
{
    terminal_t *t = &termid->term;
    if (!t) return -1;

    /* Basic control handling */
    if (c == '\r') {
        t->cursor_x = 0;
        if (termid) putUserChar(termid,'\r');
        return 1;
    } else if (c == '\n') {
        t->cursor_x = 0;
        if (t->cursor_y + 1 < t->rows) t->cursor_y++;
        if (termid) putUserChar(termid,'\n');
        return 1;
    } else if (c == '\b') {
        if (t->cursor_x > 0) t->cursor_x--;
        if (termid) {
            putUserChar(termid,'\b');
            putUserChar(termid,' ');
            putUserChar(termid,'\b');
        }
        return 1;
    } else if (c == '\t') {
        int spaces = 4 - (t->cursor_x % 4);
        for (int i = 0; i < spaces; ++i) {
            if (termid) putUserChar(termid,' ');
            t->cursor_x++;
            if (t->cursor_x >= t->cols) {
                t->cursor_x = 0;
                if (t->cursor_y + 1 < t->rows) t->cursor_y++;
            }
        }
        return spaces;
    } else {
        if (termid) putUserChar(termid,c);
        t->cursor_x++;
        if (t->cursor_x >= t->cols) {
            t->cursor_x = 0;
            if (t->cursor_y + 1 < t->rows) t->cursor_y++;
        }
        return 1;
    }
}

int terminal_puts(user_context_t *termid, const char *s)
{
    if (!termid || !s) return -1;
    return (int)terminal_write(termid, s, strlen(s));
}

int terminal_write(user_context_t *termid, const char *buf, size_t len)
{
    terminal_t *t = &termid->term;
    if (!t || !buf) return 0;
    int written = 0;
    for (int i = 0; i < len; ++i) {
        char c = buf[i];
        /* Use putc to update cursor and handle control chars */
        int r = terminal_putc(termid, c);
        if (r < 0) break;
        written += (int)r;
    }

    if (termid) user_flush(termid);
    return written;
}

int terminal_vprintf(user_context_t *termid, const char *fmt, va_list ap)
{
    terminal_t *t = &termid->term;
    if (!t || !fmt) return -1;
    /* Try on-stack, fall back to heap if needed */
    char smallbuf[512];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(smallbuf, sizeof(smallbuf), fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;
    if ((size_t)n < sizeof(smallbuf)) {
        return (int)terminal_write(termid, smallbuf, (size_t)n);
    }
    /* need larger buffer */
    int sz = n + 1;
    char *buf = (char *)malloc(sz);
    if (!buf) return -1;
    va_copy(ap2, ap);
    n = vsnprintf(buf, sz, fmt, ap2);
    va_end(ap2);
    if (n < 0) { free(buf); return -1; }
    int wrote = (int)terminal_write(termid, buf, (size_t)n);
    free(buf);
    return wrote;
}

int terminal_printf( user_context_t *termid, const char *fmt, ...)
{
    terminal_t *t = &termid->term;
    if (!t || !fmt) return -1;
    va_list ap;
    va_start(ap, fmt);
    int r = terminal_vprintf(termid, fmt, ap);
    va_end(ap);
    return r;
}

int terminal_read(user_context_t *termid, char *buf, size_t len)
{
    terminal_t *t = &termid->term;
    if (!t) return -1;

/*    if (!buf || len == 0) return -1;
    user_context_t *f = term_get_file(t);
    if (!f) return -1;
    // Use fgets-like behavior but return bytes read 
    if (f == stdin || f == stdout) {
        // read from stdin 
        if (fgets(buf, (int)len, stdin) == NULL) return -1;
        size_t l = strnlen(buf, len);
        // update cursor position 
        for (size_t i = 0; i < l; ++i) {
            if (buf[i] == '\n') {
                t->cursor_x = 0;
                if (t->cursor_y + 1 < t->rows) t->cursor_y++;
            } else {
                t->cursor_x++;
                if (t->cursor_x >= t->cols) {
                    t->cursor_x = 0;
                    if (t->cursor_y + 1 < t->rows) t->cursor_y++;
                }
            }
        }
        return (int)l;
    } else {
        // generic FILE* read 
        int r = fread(buf, 1, len, f);
        if (r == 0 && ferror(f)) return -1;
        // naive cursor update on bytes read 
        for (int i = 0; i < r; ++i) {
            char c = buf[i];
            if (c == '\n') {
                t->cursor_x = 0;
                if (t->cursor_y + 1 < t->rows) t->cursor_y++;
            } else {
                t->cursor_x++;
                if (t->cursor_x >= t->cols) {
                    t->cursor_x = 0;
                    if (t->cursor_y + 1 < t->rows) t->cursor_y++;
                }
            }
        }
        return (int)r;
    }
*/
}

void terminal_clear(user_context_t *termid)
{
    terminal_t *t = &termid->term;
    if (!t) return;
    /* ANSI clear screen + move home */
    const char *esc = "\x1b[2J\x1b[H";
    terminal_write(termid, esc, strlen(esc));
    t->cursor_x = 0;
    t->cursor_y = 0;
}

void terminal_flush(user_context_t *termid)
{
    if (termid) user_flush(termid);
}

void terminal_set_cursor(user_context_t *termid, uint16_t x, uint16_t y)
{
    terminal_t *t = &termid->term;
    if (!t) return;
    if (x >= t->cols) x = t->cols ? t->cols - 1 : 0;
    if (y >= t->rows) y = t->rows ? t->rows - 1 : 0;
    t->cursor_x = x;
    t->cursor_y = y;
    /* move terminal cursor via ANSI if outputting to a terminal */
    char esc[32];
    int n = snprintf(esc, sizeof(esc), "\x1b[%u;%uH", (unsigned)(y + 1), (unsigned)(x + 1));
    if (n > 0) terminal_write(termid, esc, (size_t)n);
}

void terminal_get_cursor(user_context_t *termid, uint16_t *x, uint16_t *y)
{
    terminal_t *t = &termid->term;
    if (!t) return;
    if (x) *x = t->cursor_x;
    if (y) *y = t->cursor_y;
}

void terminal_set_colors(user_context_t *termid, uint8_t fg, uint8_t bg)
{
    terminal_t *t = &termid->term;
    if (!t) return;
    t->fg_color = fg;
    t->bg_color = bg;
    /* Try to set ANSI colors roughly (0-7 mapping) */
    char esc[32];
    int n = snprintf(esc, sizeof(esc), "\x1b[3%u;4%um", (unsigned)(fg & 7), (unsigned)(bg & 7));
    if (n > 0) terminal_write(termid, esc, (size_t)n);
}

void terminal_get_size(user_context_t *termid, uint16_t *cols, uint16_t *rows)
{
    terminal_t *t = &termid->term;
    if (!t) return;
    if (cols) *cols = t->cols;
    if (rows) *rows = t->rows;
}