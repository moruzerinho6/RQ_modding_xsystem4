#ifndef RQ_MODLOADER_PRIV_H
#define RQ_MODLOADER_PRIV_H

#include <stdio.h>

#define RQ_PATH_MAX 1024

extern char g_rq_game_dir[RQ_PATH_MAX];
extern char g_rq_mods_dir[RQ_PATH_MAX];
extern char g_rq_exe_dir[RQ_PATH_MAX];
extern FILE *g_rq_log;
extern int g_rq_verbose;
extern int g_rq_skip_sdl;

void rq_log(const char *fmt, ...);

#endif
