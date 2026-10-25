/* In-engine Lua mods (meson -Dmodloader=true). No RVA inject. */

#ifndef RQ_MODLOADER_H
#define RQ_MODLOADER_H

#include <stdbool.h>
#include <stdarg.h>

struct ain;

#ifdef RQ_MODLOADER

void rq_mods_early_init(void);
void rq_mods_init(struct ain *ain);
void rq_mods_on_vm_call(int fno, int struct_page);
bool rq_mods_swallow_msgbox(const char *title, const char *msg);
void rq_mods_agent_start(void);
void rq_log(const char *fmt, ...);
int rq_loot_install(struct ain *ain);
int rq_loot_syscall(int code);

#else

static inline void rq_mods_early_init(void) {}
static inline void rq_mods_init(struct ain *ain) { (void)ain; }
static inline void rq_mods_on_vm_call(int fno, int struct_page)
{
	(void)fno;
	(void)struct_page;
}
static inline bool rq_mods_swallow_msgbox(const char *title, const char *msg)
{
	(void)title;
	(void)msg;
	return false;
}
static inline void rq_mods_agent_start(void) {}
static inline void rq_log(const char *fmt, ...) { (void)fmt; }
static inline int rq_loot_install(struct ain *ain) { (void)ain; return 0; }
static inline int rq_loot_syscall(int code) { (void)code; return 0; }

#endif

void sys_show_simple_message_box(unsigned flags, const char *title, const char *msg);

#endif /* RQ_MODLOADER_H */
