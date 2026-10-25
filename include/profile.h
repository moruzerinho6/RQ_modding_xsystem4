/* Lightweight frame profiler for Rance Quest hub-lag captures.
 * Compiled when meson -Dprofile=true (FORCE on) or -Dmodloader=true
 * (runtime-gated via config.json; all flags default off).
 */

#ifndef RQ_PROFILE_H
#define RQ_PROFILE_H

#include <stddef.h>

enum rq_prof_bucket {
	RQ_PROF_INPUT = 0,   /* SDL event pump */
	RQ_PROF_PLUGIN,      /* sprite plugin update (Tapir + dungeon + …) */
	RQ_PROF_SCENE,       /* scene_render compose */
	RQ_PROF_SPRITE,      /* 2D sprite / UI blit */
	RQ_PROF_DUNGEON,     /* DrawDungeon / 3D dungeon */
	RQ_PROF_TAPIR,       /* Reign/Tapir RE_render */
	RQ_PROF_SWAP,        /* SDL_GL_SwapWindow (vsync/wait) */
	RQ_PROF_NR
};

struct rq_prof_runtime {
	int csv;
	int leftover_ain;
	int hll;
	int text_cache;
	int update_split;
	int vm_ops;
	int ain_fn;
};

#ifdef RQ_PROFILE

extern struct rq_prof_runtime g_rq_prof;

void rq_prof_set_runtime(const struct rq_prof_runtime *rt);
void rq_prof_init(void);
void rq_prof_fini(void);
void rq_prof_toggle_pause(void);
void vm_prof_ain_stack(char *buf, size_t n);
void vm_prof_ain_fname(int fno, char *buf, size_t n);

void rq_prof_begin_impl(enum rq_prof_bucket b);
void rq_prof_end_impl(enum rq_prof_bucket b);
void rq_prof_count_impl(enum rq_prof_bucket b);
void rq_prof_frame_impl(void);
void rq_prof_note_sleep_impl(int requested_ms, double elapsed_ms, const char *ain_stack);
void rq_prof_hll_begin_impl(void);
void rq_prof_hll_end_impl(const char *lib, const char *fn);
void rq_prof_hll_add_impl(const char *lib, const char *fn, double elapsed_ms);
void rq_prof_note_update_split_impl(double events_ms, double plugin_ms, double scene_ms,
				   double other_ms, int dirty, int nspr);
void rq_prof_note_vmop_impl(unsigned opcode);
void rq_prof_ain_enter_impl(int fno);
void rq_prof_ain_leave_impl(void);
void rq_prof_ain_unwind_impl(void);

static inline int rq_prof_any(void)
{
	return g_rq_prof.csv || g_rq_prof.leftover_ain || g_rq_prof.hll
		|| g_rq_prof.text_cache || g_rq_prof.update_split
		|| g_rq_prof.vm_ops || g_rq_prof.ain_fn;
}

static inline void rq_prof_begin(enum rq_prof_bucket b)
{
	if (!rq_prof_any())
		return;
	rq_prof_begin_impl(b);
}

static inline void rq_prof_end(enum rq_prof_bucket b)
{
	if (!rq_prof_any())
		return;
	rq_prof_end_impl(b);
}

static inline void rq_prof_count(enum rq_prof_bucket b)
{
	if (!rq_prof_any())
		return;
	rq_prof_count_impl(b);
}

static inline void rq_prof_frame(void)
{
	if (!rq_prof_any())
		return;
	rq_prof_frame_impl();
}

static inline void rq_prof_note_sleep(int requested_ms, double elapsed_ms, const char *ain_stack)
{
	if (!g_rq_prof.csv && !g_rq_prof.leftover_ain)
		return;
	rq_prof_note_sleep_impl(requested_ms, elapsed_ms, ain_stack);
}

static inline void rq_prof_hll_begin(void)
{
	if (!g_rq_prof.hll && !g_rq_prof.csv && !g_rq_prof.leftover_ain)
		return;
	rq_prof_hll_begin_impl();
}

static inline void rq_prof_hll_end(const char *lib, const char *fn)
{
	if (!g_rq_prof.hll && !g_rq_prof.csv && !g_rq_prof.leftover_ain)
		return;
	rq_prof_hll_end_impl(lib, fn);
}

static inline void rq_prof_hll_add(const char *lib, const char *fn, double elapsed_ms)
{
	if (!g_rq_prof.hll && !g_rq_prof.csv && !g_rq_prof.leftover_ain && !g_rq_prof.update_split)
		return;
	rq_prof_hll_add_impl(lib, fn, elapsed_ms);
}

static inline void rq_prof_note_update_split(double events_ms, double plugin_ms,
					    double scene_ms, double other_ms,
					    int dirty, int nspr)
{
	if (!g_rq_prof.update_split)
		return;
	rq_prof_note_update_split_impl(events_ms, plugin_ms, scene_ms, other_ms, dirty, nspr);
}

static inline void rq_prof_note_vmop(unsigned opcode)
{
	if (!g_rq_prof.vm_ops)
		return;
	rq_prof_note_vmop_impl(opcode);
}

static inline void rq_prof_ain_enter(int fno)
{
	if (!g_rq_prof.ain_fn)
		return;
	rq_prof_ain_enter_impl(fno);
}

static inline void rq_prof_ain_leave(void)
{
	if (!g_rq_prof.ain_fn)
		return;
	rq_prof_ain_leave_impl();
}

static inline void rq_prof_ain_unwind(void)
{
	if (!g_rq_prof.ain_fn)
		return;
	rq_prof_ain_unwind_impl();
}

#else

static inline void rq_prof_set_runtime(const struct rq_prof_runtime *rt) { (void)rt; }
static inline int rq_prof_any(void) { return 0; }
static inline void rq_prof_init(void) {}
static inline void rq_prof_fini(void) {}
static inline void rq_prof_begin(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_end(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_count(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_frame(void) {}
static inline void rq_prof_toggle_pause(void) {}
static inline void rq_prof_note_sleep(int requested_ms, double elapsed_ms, const char *ain_stack)
{
	(void)requested_ms;
	(void)elapsed_ms;
	(void)ain_stack;
}
static inline void rq_prof_hll_begin(void) {}
static inline void rq_prof_hll_end(const char *lib, const char *fn)
{
	(void)lib;
	(void)fn;
}
static inline void rq_prof_hll_add(const char *lib, const char *fn, double elapsed_ms)
{
	(void)lib;
	(void)fn;
	(void)elapsed_ms;
}
static inline void rq_prof_note_update_split(double events_ms, double plugin_ms,
					    double scene_ms, double other_ms,
					    int dirty, int nspr)
{
	(void)events_ms;
	(void)plugin_ms;
	(void)scene_ms;
	(void)other_ms;
	(void)dirty;
	(void)nspr;
}
static inline void rq_prof_note_vmop(unsigned opcode)
{
	(void)opcode;
}
static inline void rq_prof_ain_enter(int fno)
{
	(void)fno;
}
static inline void rq_prof_ain_leave(void) {}
static inline void rq_prof_ain_unwind(void) {}
static inline void vm_prof_ain_stack(char *buf, size_t n)
{
	if (buf && n)
		buf[0] = '\0';
}
static inline void vm_prof_ain_fname(int fno, char *buf, size_t n)
{
	(void)fno;
	if (buf && n)
		buf[0] = '\0';
}

#endif

#endif /* RQ_PROFILE_H */
