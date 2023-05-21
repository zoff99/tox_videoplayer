/**
 * [shell percentage meter]
 * Copyright (C) 2023 Zoff <zoff@zoff.cc>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef C_SHELL_PERCENTAGE_H
#define C_SHELL_PERCENTAGE_H

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"

#define __shell_percentage__CLEAR(x) memset(&(x), 0, sizeof(x))

static char *__shell_percentage__CODE_SAVE_CURSOR = "\033[s";
static char *__shell_percentage__CODE_RESTORE_CURSOR = "\033[u";
static char *__shell_percentage__CODE_CURSOR_IN_SCROLL_AREA = "\033[1A";
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
static char *__shell_percentage__COLOR_FG = "\e[30m";
static char *__shell_percentage__COLOR_BG = "\e[42m";
static char *__shell_percentage__COLOR_BG_BLOCKED = "\e[43m";
static char *__shell_percentage__RESTORE_FG = "\e[39m";
static char *__shell_percentage__RESTORE_BG = "\e[49m";
#pragma clang diagnostic pop

static bool __shell_percentage__PROGRESS_BLOCKED = false;
static int __shell_percentage__CURRENT_NR_LINES = 0;
#define __shell_percentage__XTERM_VERT_LINES 23
#define __shell_percentage__XTERM_HOR_COLUMS 79

// function definitions ---------------------------------------------
static void __shell_percentage__setup_scroll_area(void);
static void __shell_percentage__printf_new(const char c, int count);
// function definitions ---------------------------------------------

static void __shell_percentage__run_cmd_return_output(const char *command, char *output)
{
    FILE *fp = NULL;
    char path[999];
    __shell_percentage__CLEAR(path);
    char *pos = NULL;

    if (!output)
    {
        return;
    }

    /* Open the command for reading. */
    fp = popen(command, "r");

    if (fp == NULL)
    {
        output[0] = '\0';
        return;
    }

    /* Read the output a line at a time - output it. */
    while (fgets(path, sizeof(path) - 1, fp) != NULL)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(output, 998, "%s", (const char *)path);
#pragma GCC diagnostic pop
    }

    if (strlen(output) > 1)
    {
        if ((pos = strchr(output, '\n')) != NULL)
        {
            *pos = '\0';
        }
    }

    /* close */
    pclose(fp);
}

static void __shell_percentage__tput_el(void)
{
    if (system("tput el")) {}
}

static int __shell_percentage__tput_lines(void)
{
    char output_str[1000];
    __shell_percentage__CLEAR(output_str);
    __shell_percentage__run_cmd_return_output("tput lines", output_str);
    if (strlen(output_str) > 0)
    {
        int lines = (int)(strtol(output_str, NULL, 10));
        if (lines < 3)
        {
            lines = 3;
        }
        if (lines > 400)
        {
            lines = 400;
        }
        return lines;
    }
    
    return __shell_percentage__XTERM_VERT_LINES;
}

static int __shell_percentage__tput_cols(void)
{
    char output_str[1000];
    __shell_percentage__CLEAR(output_str);
    __shell_percentage__run_cmd_return_output("tput cols", output_str);
    if (strlen(output_str) > 0)
    {
        int cols = (int)(strtol(output_str, NULL, 10));
        if (cols < 3)
        {
            cols = 3;
        }
        if (cols > 400)
        {
            cols = 400;
        }
        return cols;
    }
    
    return __shell_percentage__XTERM_HOR_COLUMS;
}

static void __shell_percentage__clear_progress_bar(void)
{
    int lines = __shell_percentage__tput_lines();
    printf("%s", __shell_percentage__CODE_SAVE_CURSOR);
    printf("\033[%d;0f", lines);
    __shell_percentage__tput_el();
    printf("%s", __shell_percentage__CODE_RESTORE_CURSOR);
}

static void __shell_percentage__destroy_scroll_area(void)
{
    int lines = __shell_percentage__tput_lines();
    printf("%s", __shell_percentage__CODE_SAVE_CURSOR);
    printf("\033[0;%dr", lines);

    printf("%s", __shell_percentage__CODE_RESTORE_CURSOR);
    printf("%s", __shell_percentage__CODE_CURSOR_IN_SCROLL_AREA);
    
    __shell_percentage__clear_progress_bar();
    printf("\n");
    printf("\n");
}

static void __shell_percentage__print_bar_text(int percentage)
{
    if (percentage < 0)
    {
        percentage = 0;
    }

    if (percentage > 100)
    {
        percentage = 100;
    }

    int cols = __shell_percentage__tput_cols();
    int bar_size = cols - 17;
    if (bar_size < 4)
    {
        bar_size = 4;
    }
    int complete_size = (bar_size * percentage) / 100;
    int remainder_size = bar_size - complete_size;

    printf(" Progress ");
    if (percentage < 10)
    {
        printf(" ");
    }
    if (percentage < 100)
    {
        printf(" ");
    }
    printf("%d%%", percentage);
    printf(" ");

    printf("[");
    if (__shell_percentage__PROGRESS_BLOCKED)
    {
        printf("%s%s", __shell_percentage__COLOR_FG, __shell_percentage__COLOR_BG_BLOCKED);
    }
    else
    {
        printf("%s%s", __shell_percentage__COLOR_FG, __shell_percentage__COLOR_BG);
    }
    __shell_percentage__printf_new('#', complete_size);
    printf("%s%s", __shell_percentage__RESTORE_FG, __shell_percentage__RESTORE_BG);
    __shell_percentage__printf_new('.', remainder_size);
    printf("]");
}

static void __shell_percentage__printf_new(const char c, int count)
{
    if (count < 1)
    {
        return;
    }

    if (count > 400)
    {
        count = 400;
    }

    for (int i = 0; i < count; i ++)
    {
        printf("%c", c);
    }
}

static void __shell_percentage__draw_progress_bar(int percentage, bool blocked)
{
    if (percentage < 0)
    {
        percentage = 0;
    }

    if (percentage > 100)
    {
        percentage = 100;
    }

    int lines = __shell_percentage__tput_lines();
    if (lines != __shell_percentage__CURRENT_NR_LINES)
    {
        __shell_percentage__setup_scroll_area();
    }
    printf("%s", __shell_percentage__CODE_SAVE_CURSOR);
    printf("\033[%d;0f", lines);
    __shell_percentage__tput_el();
    __shell_percentage__PROGRESS_BLOCKED = blocked;
    __shell_percentage__print_bar_text(percentage);
    printf("%s", __shell_percentage__CODE_RESTORE_CURSOR);
}

static void __shell_percentage__setup_scroll_area(void)
{
    int lines = __shell_percentage__tput_lines();
    __shell_percentage__CURRENT_NR_LINES = lines;
    lines--;
    printf("\n");
    printf("%s", __shell_percentage__CODE_SAVE_CURSOR);
    printf("\033[0;%dr", lines);

    printf("%s", __shell_percentage__CODE_RESTORE_CURSOR);
    printf("%s", __shell_percentage__CODE_CURSOR_IN_SCROLL_AREA);

    __shell_percentage__draw_progress_bar(0, false);
}

#pragma clang diagnostic pop

#ifdef __cplusplus
}
#endif

#endif // C_SHELL_PERCENTAGE_H
