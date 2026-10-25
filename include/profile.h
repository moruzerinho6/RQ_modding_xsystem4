/* Lightweight frame profiler for Rance Quest hub-lag captures.
 * Compiled in only when meson -Dprofile=true (see notes/rq-profiling.md).
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

#ifdef RQ_PROFILE

void rq_prof_init(void);
void rq_prof_fini(void);
void rq_prof_begin(enum rq_prof_bucket b);
void rq_prof_end(enum rq_prof_bucket b);
void rq_prof_count(enum rq_prof_bucket b);
void rq_prof_frame(void);
void rq_prof_toggle_pause(void);
void rq_prof_note_sleep(int requested_ms, double elapsed_ms, const char *ain_stack);
void rq_prof_hll_begin(void);
void rq_prof_hll_end(const char *lib, const char *fn);
void rq_prof_hll_add(const char *lib, const char *fn, double elapsed_ms);
void rq_prof_note_update_split(double events_ms, double plugin_ms, double scene_ms,
			      double other_ms, int dirty, int nspr);
void rq_prof_note_vmop(unsigned opcode);
void vm_prof_ain_stack(char *buf, size_t n);

#else

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
static inline void vm_prof_ain_stack(char *buf, size_t n)
{
	if (buf && n)
		buf[0] = '\0';
}

#endif

#endif /* RQ_PROFILE_H */
