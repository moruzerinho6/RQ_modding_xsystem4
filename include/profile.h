/* Lightweight frame profiler for Rance Quest hub-lag captures.
 * Compiled in only when meson -Dprofile=true (see notes/rq-profiling.md).
 */

#ifndef RQ_PROFILE_H
#define RQ_PROFILE_H

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

#else

static inline void rq_prof_init(void) {}
static inline void rq_prof_fini(void) {}
static inline void rq_prof_begin(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_end(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_count(enum rq_prof_bucket b) { (void)b; }
static inline void rq_prof_frame(void) {}
static inline void rq_prof_toggle_pause(void) {}

#endif

#endif /* RQ_PROFILE_H */
