/*                   GNU AFFERO GENERAL PUBLIC LICENSE
                       Version 3, 19 November 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed. */
#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"
#include "user.h"

void init_filesys(void);
int close_filesys(void);
FRESULT display_directory(user_context_t *user, char *dirpath, bool as_root);
bool user_create_home_directory(user_context_t *user);
FRESULT user_create_directory(user_context_t *user, char *dirname);
FRESULT user_remove_directory(user_context_t *user, char *dirname);
FRESULT user_rename_user_file(user_context_t *user, char *sourcefile, char *destfile);
FRESULT Copy_file(user_context_t *user, const char* source_file, const char* dest_file);
char *basic_language_config(user_context_t *user);


#endif // FILESYSTEM_H