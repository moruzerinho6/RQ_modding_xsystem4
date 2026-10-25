/* In-engine Lua mods (meson -Dmodloader=true). No RVA inject. */

#ifndef RQ_MODLOADER_H
#define RQ_MODLOADER_H

#include <stdbool.h>
#include <stdarg.h>

struct ain;
struct lua_State;

#ifdef RQ_MODLOADER

void rq_mods_early_init(void);
void rq_mods_init(struct ain *ain);
void rq_mods_on_vm_call(int fno, int struct_page);
bool rq_mods_swallow_msgbox(const char *title, const char *msg);
void rq_mods_agent_start(void);
void rq_log(const char *fmt, ...);
int rq_loot_install(struct ain *ain);
int rq_loot_syscall(int code);
int rq_loot_list(struct lua_State *L);
int rq_hud_syscall(int code);
int rq_hud_install(struct ain *ain);
int rq_hud_tick(void);
void rq_hud_draw(void);
int rq_hud_block_mouse(void);
int rq_dungeon_active(void);
void rq_hud_bind_lua(struct lua_State *L);
struct lua_State *rq_lua(void);

int rq_lua_on_frame(struct lua_State *L);
int rq_lua_dungeon_active(struct lua_State *L);
int rq_lua_input_mouse(struct lua_State *L);
int rq_lua_input_key(struct lua_State *L);
int rq_lua_hud_rect(struct lua_State *L);
int rq_lua_hud_text(struct lua_State *L);
int rq_lua_hud_cg(struct lua_State *L);
int rq_lua_hud_hit(struct lua_State *L);
int rq_lua_hud_clicked(struct lua_State *L);
int rq_lua_hud_block(struct lua_State *L);
int rq_lua_hud_screen(struct lua_State *L);
int rq_lua_loot_list(struct lua_State *L);

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
static inline int rq_hud_syscall(int code) { (void)code; return 0; }
static inline int rq_hud_tick(void) { return 0; }
static inline void rq_hud_draw(void) {}
static inline int rq_hud_block_mouse(void) { return 0; }

#endif

void sys_show_simple_message_box(unsigned flags, const char *title, const char *msg);

#endif /* RQ_MODLOADER_H */
