/* Copyright (C) 2026 Rance Quest modding fork
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef RQ_PROFILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#include "profile.h"
#include "gfx/font.h"
#include "system4/instructions.h"

#define RQ_PROF_SLOW_MS 50.0
#define RQ_HLL_CAP 1024
#define RQ_HLL_NEST 8

struct rq_hll_acc {
	const char *lib;
	const char *fn;
	char name[96];
	double ms;
	uint32_t n;
	bool used;
};

#ifdef RQ_PROFILE_FORCE
struct rq_prof_runtime g_rq_prof = {1, 1, 1, 1, 1, 1, 1};
#else
struct rq_prof_runtime g_rq_prof = {0};
#endif

void rq_prof_set_runtime(const struct rq_prof_runtime *rt)
{
	if (!rt)
		return;
	g_rq_prof = *rt;
}

static FILE *out;
static FILE *ain_log;
static FILE *hll_log;
static char out_path[1024];
static char ain_path[1100];
static char hll_path[1100];
static uint64_t freq;
static uint64_t t_start;
static uint64_t t_last_frame;
static uint64_t t0[RQ_PROF_NR];
static uint64_t accum[RQ_PROF_NR];
static uint32_t counts[RQ_PROF_NR];
static uint32_t frame_no;
static bool paused;
static bool inited;

static double sleep_ms;
static double sleep_req_ms;
static uint32_t n_sleep;
static double slowest_sleep_ms;
static char sleep_ain[512];

static struct rq_hll_acc hll_tab[RQ_HLL_CAP];
static int hll_used_idx[RQ_HLL_CAP];
static int n_hll_used;
static uint32_t n_hll;
static double hll_ms;
static double hll_other_ms;
static double chipmunk_sleep_ms;
static uint64_t hll_t0[RQ_HLL_NEST];
static int hll_depth;

static double upd_events_ms;
static double upd_plugin_ms;
static double upd_scene_ms;
static double upd_other_ms;
static uint32_t upd_n;
static uint32_t upd_dirty;
static int upd_nspr;

static uint32_t n_vmop;
static uint32_t vmop_n[NR_OPCODES];

#define RQ_AIN_CAP 2048
#define RQ_AIN_NEST 128

struct rq_ain_acc {
	int fno;
	double ms;
	uint32_t n;
	bool used;
};

struct rq_ain_frame {
	int fno;
	uint64_t t0;
};

static struct rq_ain_acc ain_tab[RQ_AIN_CAP];
static int ain_used_idx[RQ_AIN_CAP];
static int n_ain_used;
static struct rq_ain_frame ain_frame[RQ_AIN_NEST];
static int ain_depth;

static double ticks_to_ms(uint64_t ticks)
{
	if (!freq)
		return 0;
	return (double)ticks * 1000.0 / (double)freq;
}

static double now_t_ms(void)
{
	return ticks_to_ms(SDL_GetPerformanceCounter() - t_start);
}

static void copy_top_name(char *dst, size_t n, const char *stack)
{
	if (!dst || n == 0)
		return;
	dst[0] = '\0';
	if (!stack || !stack[0])
		return;
	const char *semi = strchr(stack, ';');
	size_t len = semi ? (size_t)(semi - stack) : strlen(stack);
	if (len >= n)
		len = n - 1;
	memcpy(dst, stack, len);
	dst[len] = '\0';
}

static void csv_quote(FILE *f, const char *s)
{
	fputc('"', f);
	if (!s)
		s = "";
	for (const char *p = s; *p; p++) {
		if (*p == '"')
			fputc('"', f);
		fputc(*p, f);
	}
	fputc('"', f);
}

static void reset_accum(void)
{
	memset(accum, 0, sizeof(accum));
	memset(counts, 0, sizeof(counts));
	sleep_ms = 0;
	sleep_req_ms = 0;
	n_sleep = 0;
	slowest_sleep_ms = 0;
	sleep_ain[0] = '\0';
	for (int i = 0; i < n_hll_used; i++)
		hll_tab[hll_used_idx[i]].used = false;
	n_hll_used = 0;
	n_hll = 0;
	hll_ms = 0;
	hll_other_ms = 0;
	chipmunk_sleep_ms = 0;
	upd_events_ms = 0;
	upd_plugin_ms = 0;
	upd_scene_ms = 0;
	upd_other_ms = 0;
	upd_n = 0;
	upd_dirty = 0;
	upd_nspr = 0;
	n_vmop = 0;
	memset(vmop_n, 0, sizeof(vmop_n));
	for (int i = 0; i < n_ain_used; i++)
		ain_tab[ain_used_idx[i]].used = false;
	n_ain_used = 0;
	if (ain_depth > 0 && ain_depth <= RQ_AIN_NEST)
		ain_frame[ain_depth - 1].t0 = SDL_GetPerformanceCounter();
}

static void resolve_path(void)
{
	const char *env = getenv("XSYS4_PROFILE");
	if (env && env[0]) {
		snprintf(out_path, sizeof(out_path), "%s", env);
	} else {
		char *base = SDL_GetBasePath();
		if (base) {
			snprintf(out_path, sizeof(out_path), "%sxsystem4-profile.csv", base);
			SDL_free(base);
		} else {
			snprintf(out_path, sizeof(out_path), "xsystem4-profile.csv");
		}
	}

	size_t n = strlen(out_path);
	if (n > 4 && !strcmp(out_path + n - 4, ".csv")) {
		snprintf(ain_path, sizeof(ain_path), "%.*s-ain.log", (int)(n - 4), out_path);
		snprintf(hll_path, sizeof(hll_path), "%.*s-hll.log", (int)(n - 4), out_path);
	} else {
		snprintf(ain_path, sizeof(ain_path), "%s-ain.log", out_path);
		snprintf(hll_path, sizeof(hll_path), "%s-hll.log", out_path);
	}
}

static bool hll_is_present(const char *fn)
{
	return fn && (!strcmp(fn, "Update") || !strcmp(fn, "UpdateView"));
}

static unsigned hll_hash(const char *lib, const char *fn)
{
	uintptr_t a = (uintptr_t)lib;
	uintptr_t b = (uintptr_t)fn;
	return (unsigned)((a * 1315423911u) ^ (b * 2654435761u)) & (RQ_HLL_CAP - 1);
}

static struct rq_hll_acc *hll_slot(const char *lib, const char *fn)
{
	unsigned i = hll_hash(lib, fn);
	for (unsigned n = 0; n < RQ_HLL_CAP; n++) {
		struct rq_hll_acc *s = &hll_tab[i];
		if (!s->used) {
			s->lib = lib;
			s->fn = fn;
			if (lib && fn)
				snprintf(s->name, sizeof(s->name), "%s.%s", lib, fn);
			else if (fn)
				snprintf(s->name, sizeof(s->name), "%s", fn);
			else
				s->name[0] = '\0';
			s->ms = 0;
			s->n = 0;
			s->used = true;
			if (n_hll_used < RQ_HLL_CAP)
				hll_used_idx[n_hll_used++] = (int)i;
			return s;
		}
		if (s->lib == lib && s->fn == fn)
			return s;
		i = (i + 1) & (RQ_HLL_CAP - 1);
	}
	return NULL;
}

static void hll_note(const char *lib, const char *fn, double elapsed_ms)
{
	if (elapsed_ms < 0)
		elapsed_ms = 0;
	n_hll++;
	hll_ms += elapsed_ms;
	if (lib && fn && !strcmp(lib, "ChipmunkSpriteEngine") && !strcmp(fn, "Sleep"))
		chipmunk_sleep_ms += elapsed_ms;
	if (!hll_is_present(fn))
		hll_other_ms += elapsed_ms;
	struct rq_hll_acc *s = hll_slot(lib, fn);
	if (!s)
		return;
	s->ms += elapsed_ms;
	s->n++;
}

static int hll_ptr_cmp(const void *a, const void *b)
{
	const struct rq_hll_acc *x = *(const struct rq_hll_acc *const *)a;
	const struct rq_hll_acc *y = *(const struct rq_hll_acc *const *)b;
	if (y->ms > x->ms)
		return 1;
	if (y->ms < x->ms)
		return -1;
	return 0;
}

static const struct rq_hll_acc *top_leftover_hll(void)
{
	const struct rq_hll_acc *best = NULL;
	for (int i = 0; i < n_hll_used; i++) {
		const struct rq_hll_acc *s = &hll_tab[hll_used_idx[i]];
		if (hll_is_present(s->fn))
			continue;
		if (!best || s->ms > best->ms)
			best = s;
	}
	return best;
}

static void ain_log_event(const char *kind, double elapsed, double requested,
			 const char *stack, const char *hll, double hll_elapsed)
{
	if (!ain_log || paused)
		return;
	fprintf(ain_log, "%.3f,%s,%.3f,%.3f,", now_t_ms(), kind, elapsed, requested);
	csv_quote(ain_log, stack ? stack : "");
	fputc(',', ain_log);
	csv_quote(ain_log, hll ? hll : "");
	fprintf(ain_log, ",%.3f\n", hll_elapsed);
	fflush(ain_log);
}

static void write_hll_breakdown(double leftover_other, const char *stack)
{
	if (!hll_log || paused || leftover_other <= RQ_PROF_SLOW_MS || n_hll_used == 0)
		return;

	struct rq_hll_acc *ptrs[RQ_HLL_CAP];
	int n = 0;
	for (int i = 0; i < n_hll_used; i++)
		ptrs[n++] = &hll_tab[hll_used_idx[i]];
	qsort(ptrs, (size_t)n, sizeof(ptrs[0]), hll_ptr_cmp);

	int written = 0;
	for (int i = 0; i < n && written < 64; i++) {
		if (ptrs[i]->n == 0 && ptrs[i]->ms <= 0)
			continue;
		fprintf(hll_log, "%.3f,", now_t_ms());
		csv_quote(hll_log, ptrs[i]->name);
		fprintf(hll_log, ",%.3f,%u,", ptrs[i]->ms, ptrs[i]->n);
		csv_quote(hll_log, stack ? stack : "");
		fputc('\n', hll_log);
		written++;
	}
	fflush(hll_log);
}

void rq_prof_init(void)
{
	int want_hll_log;
	if (inited)
		return;
	if (!rq_prof_any()) {
		inited = true;
		return;
	}
	freq = SDL_GetPerformanceFrequency();
	t_start = SDL_GetPerformanceCounter();
	t_last_frame = t_start;
	resolve_path();
	if (g_rq_prof.csv) {
		out = fopen(out_path, "w");
		if (!out) {
			fprintf(stderr, "[rq-prof] could not open %s\n", out_path);
		} else {
			fprintf(out,
				"frame,t_ms,frame_ms,input_ms,plugin_ms,scene_ms,sprite_ms,"
				"dungeon_ms,tapir_ms,swap_ms,sleep_ms,n_sleep,sleep_req_ms,"
				"leftover_ms,leftover_other_ms,script_ms,hll_ms,hll_other_ms,n_hll,"
				"chipmunk_sleep_ms,n_sprite,n_dungeon,n_tapir,vsync,ain,top_hll,top_hll_ms,"
				"tc_hit,tc_skip,tc_miss,tc_entries\n");
			fflush(out);
			fprintf(stderr, "[rq-prof] writing %s (F12 pauses)\n", out_path);
		}
	}
	if (g_rq_prof.leftover_ain) {
		ain_log = fopen(ain_path, "w");
		if (ain_log) {
			fprintf(ain_log, "t_ms,kind,elapsed_ms,requested_ms,ain_stack,hll,hll_ms\n");
			fflush(ain_log);
			fprintf(stderr, "[rq-prof] slow AIN log %s (sleep/leftover > %.0f ms)\n",
				ain_path, RQ_PROF_SLOW_MS);
		} else {
			fprintf(stderr, "[rq-prof] could not open %s\n", ain_path);
		}
	}
	want_hll_log = g_rq_prof.hll || g_rq_prof.update_split || g_rq_prof.vm_ops
		|| g_rq_prof.ain_fn || g_rq_prof.text_cache;
	if (want_hll_log) {
		hll_log = fopen(hll_path, "w");
		if (hll_log) {
			fprintf(hll_log, "t_ms,hll,ms,n,ain_stack\n");
			fflush(hll_log);
			fprintf(stderr, "[rq-prof] HLL breakdown %s (leftover > %.0f ms)\n",
				hll_path, RQ_PROF_SLOW_MS);
		} else {
			fprintf(stderr, "[rq-prof] could not open %s\n", hll_path);
		}
	}
	inited = true;
	atexit(rq_prof_fini);
	if (g_rq_prof.text_cache)
		fprintf(stderr, "[rq-prof] text-sprite cache counters on\n");
	if (g_rq_prof.update_split)
		fprintf(stderr, "[rq-prof] Chipmunk Update split on leftover HLL\n");
	if (g_rq_prof.vm_ops)
		fprintf(stderr, "[rq-prof] leftover vm-ops row on\n");
	if (g_rq_prof.ain_fn)
		fprintf(stderr, "[rq-prof] leftover ain-fn rows on (exclusive AIN time; not a JIT)\n");
}

void rq_prof_fini(void)
{
	if (hll_log) {
		fflush(hll_log);
		fclose(hll_log);
		hll_log = NULL;
	}
	if (ain_log) {
		fflush(ain_log);
		fclose(ain_log);
		ain_log = NULL;
	}
	if (!out)
		return;
	fflush(out);
	fclose(out);
	out = NULL;
	{
		uint64_t hits = 0, misses = 0, skips = 0;
		unsigned entries = 0;
		gfx_text_cache_stats(&hits, &misses, &skips, &entries);
		fprintf(stderr,
			"[rq-prof] closed %s (%u frames) text-cache hit=%llu skip=%llu miss=%llu entries=%u\n",
			out_path, frame_no,
			(unsigned long long)hits, (unsigned long long)skips,
			(unsigned long long)misses, entries);
	}
}

void rq_prof_begin_impl(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	t0[b] = SDL_GetPerformanceCounter();
}

void rq_prof_end_impl(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	accum[b] += SDL_GetPerformanceCounter() - t0[b];
}

void rq_prof_count_impl(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	counts[b]++;
}

void rq_prof_toggle_pause(void)
{
	if (!rq_prof_any())
		return;
	paused = !paused;
	fprintf(stderr, "[rq-prof] %s\n", paused ? "paused" : "recording");
}

void rq_prof_note_sleep_impl(int requested_ms, double elapsed_ms, const char *ain_stack)
{
	if (!inited)
		rq_prof_init();
	if (elapsed_ms < 0)
		elapsed_ms = 0;
	if (requested_ms < 0)
		requested_ms = 0;
	sleep_ms += elapsed_ms;
	sleep_req_ms += (double)requested_ms;
	n_sleep++;
	if (elapsed_ms >= slowest_sleep_ms) {
		slowest_sleep_ms = elapsed_ms;
		copy_top_name(sleep_ain, sizeof(sleep_ain), ain_stack);
	}
	if (elapsed_ms > RQ_PROF_SLOW_MS)
		ain_log_event("sleep", elapsed_ms, (double)requested_ms, ain_stack, "", 0);
}

void rq_prof_hll_begin_impl(void)
{
	if (!inited)
		rq_prof_init();
	if (hll_depth < RQ_HLL_NEST)
		hll_t0[hll_depth] = SDL_GetPerformanceCounter();
	hll_depth++;
}

void rq_prof_hll_end_impl(const char *lib, const char *fn)
{
	if (hll_depth <= 0)
		return;
	hll_depth--;
	if (hll_depth >= RQ_HLL_NEST)
		return;
	double elapsed = ticks_to_ms(SDL_GetPerformanceCounter() - hll_t0[hll_depth]);
	hll_note(lib, fn, elapsed);
}

void rq_prof_hll_add_impl(const char *lib, const char *fn, double elapsed_ms)
{
	if (!inited)
		rq_prof_init();
	hll_note(lib, fn, elapsed_ms);
}

void rq_prof_note_update_split_impl(double events_ms, double plugin_ms, double scene_ms,
				  double other_ms, int dirty, int nspr)
{
	if (!inited)
		rq_prof_init();
	if (events_ms < 0)
		events_ms = 0;
	if (plugin_ms < 0)
		plugin_ms = 0;
	if (scene_ms < 0)
		scene_ms = 0;
	if (other_ms < 0)
		other_ms = 0;
	upd_events_ms += events_ms;
	upd_plugin_ms += plugin_ms;
	upd_scene_ms += scene_ms;
	upd_other_ms += other_ms;
	upd_n++;
	if (dirty)
		upd_dirty++;
	upd_nspr = nspr;
}

void rq_prof_note_vmop_impl(unsigned opcode)
{
	n_vmop++;
	if (opcode < NR_OPCODES)
		vmop_n[opcode]++;
}

static unsigned ain_hash(int fno)
{
	return (unsigned)((unsigned)fno * 2654435761u) & (RQ_AIN_CAP - 1);
}

static struct rq_ain_acc *ain_slot(int fno)
{
	unsigned i = ain_hash(fno);
	for (unsigned n = 0; n < RQ_AIN_CAP; n++) {
		struct rq_ain_acc *s = &ain_tab[i];
		if (!s->used) {
			s->fno = fno;
			s->ms = 0;
			s->n = 0;
			s->used = true;
			if (n_ain_used < RQ_AIN_CAP)
				ain_used_idx[n_ain_used++] = (int)i;
			return s;
		}
		if (s->fno == fno)
			return s;
		i = (i + 1) & (RQ_AIN_CAP - 1);
	}
	return NULL;
}

static void ain_add(int fno, double elapsed_ms, uint32_t calls)
{
	if (elapsed_ms < 0)
		elapsed_ms = 0;
	struct rq_ain_acc *s = ain_slot(fno);
	if (!s)
		return;
	s->ms += elapsed_ms;
	s->n += calls;
}

static void ain_flush_current(void)
{
	if (ain_depth <= 0 || ain_depth > RQ_AIN_NEST)
		return;
	uint64_t now = SDL_GetPerformanceCounter();
	struct rq_ain_frame *fr = &ain_frame[ain_depth - 1];
	ain_add(fr->fno, ticks_to_ms(now - fr->t0), 0);
	fr->t0 = now;
}

void rq_prof_ain_enter_impl(int fno)
{
	if (!inited)
		rq_prof_init();
	uint64_t now = SDL_GetPerformanceCounter();
	if (ain_depth > 0 && ain_depth <= RQ_AIN_NEST) {
		struct rq_ain_frame *parent = &ain_frame[ain_depth - 1];
		ain_add(parent->fno, ticks_to_ms(now - parent->t0), 0);
	}
	if (ain_depth < RQ_AIN_NEST) {
		ain_frame[ain_depth].fno = fno;
		ain_frame[ain_depth].t0 = now;
	}
	ain_depth++;
	ain_add(fno, 0, 1);
}

void rq_prof_ain_leave_impl(void)
{
	if (ain_depth <= 0)
		return;
	uint64_t now = SDL_GetPerformanceCounter();
	ain_depth--;
	if (ain_depth < RQ_AIN_NEST) {
		struct rq_ain_frame *fr = &ain_frame[ain_depth];
		ain_add(fr->fno, ticks_to_ms(now - fr->t0), 0);
	}
	if (ain_depth > 0 && ain_depth <= RQ_AIN_NEST)
		ain_frame[ain_depth - 1].t0 = now;
}

void rq_prof_ain_unwind_impl(void)
{
	while (ain_depth > 0)
		rq_prof_ain_leave_impl();
}

static int ain_ptr_cmp(const void *a, const void *b)
{
	const struct rq_ain_acc *x = *(const struct rq_ain_acc *const *)a;
	const struct rq_ain_acc *y = *(const struct rq_ain_acc *const *)b;
	if (y->ms > x->ms)
		return 1;
	if (y->ms < x->ms)
		return -1;
	return 0;
}

void rq_prof_frame_impl(void)
{
	if (!rq_prof_any())
		return;
	if (!inited)
		rq_prof_init();
	if (!out && !ain_log && !hll_log) {
		reset_accum();
		return;
	}

	uint64_t now = SDL_GetPerformanceCounter();
	double frame_ms = ticks_to_ms(now - t_last_frame);
	double t_ms = ticks_to_ms(now - t_start);
	t_last_frame = now;
	frame_no++;

	double input_ms = ticks_to_ms(accum[RQ_PROF_INPUT]);
	double plugin_ms = ticks_to_ms(accum[RQ_PROF_PLUGIN]);
	double scene_ms = ticks_to_ms(accum[RQ_PROF_SCENE]);
	double sprite_ms = ticks_to_ms(accum[RQ_PROF_SPRITE]);
	double dungeon_ms = ticks_to_ms(accum[RQ_PROF_DUNGEON]);
	double tapir_ms = ticks_to_ms(accum[RQ_PROF_TAPIR]);
	double swap_ms = ticks_to_ms(accum[RQ_PROF_SWAP]);
	double leftover_ms = frame_ms - input_ms - plugin_ms - scene_ms - swap_ms;
	if (leftover_ms < 0)
		leftover_ms = 0;
	double leftover_other_ms = leftover_ms - sleep_ms;
	if (leftover_other_ms < 0)
		leftover_other_ms = 0;

	double script_ms = leftover_other_ms - hll_other_ms;
	if (script_ms < 0)
		script_ms = 0;

	const struct rq_hll_acc *top = top_leftover_hll();
	const char *top_name = top ? top->name : "";
	double top_ms = top ? top->ms : 0;

	char ain_name[512];
	char stack[512];
	ain_name[0] = '\0';
	stack[0] = '\0';
	if (sleep_ain[0]) {
		snprintf(ain_name, sizeof(ain_name), "%s", sleep_ain);
	}
	if (leftover_ms > RQ_PROF_SLOW_MS || leftover_other_ms > RQ_PROF_SLOW_MS) {
		vm_prof_ain_stack(stack, sizeof(stack));
		if (!ain_name[0])
			copy_top_name(ain_name, sizeof(ain_name), stack);
	}

	uint64_t tc_hit = 0, tc_miss = 0, tc_skip = 0;
	unsigned tc_entries = 0;
	if (g_rq_prof.csv || g_rq_prof.text_cache)
		gfx_text_cache_take_frame(&tc_hit, &tc_miss, &tc_skip, &tc_entries);

	if (leftover_other_ms > RQ_PROF_SLOW_MS) {
		if (!stack[0])
			vm_prof_ain_stack(stack, sizeof(stack));
		if (g_rq_prof.leftover_ain)
			ain_log_event("leftover", leftover_other_ms, 0, stack, top_name, top_ms);
		if (g_rq_prof.hll)
			write_hll_breakdown(leftover_other_ms, stack);
		if (hll_log && !paused) {
			if (g_rq_prof.text_cache) {
				char tc_stack[192];
				snprintf(tc_stack, sizeof(tc_stack),
					 "hit=%llu skip=%llu miss=%llu entries=%u",
					 (unsigned long long)tc_hit, (unsigned long long)tc_skip,
					 (unsigned long long)tc_miss, tc_entries);
				fprintf(hll_log, "%.3f,", now_t_ms());
				csv_quote(hll_log, "text-cache");
				fprintf(hll_log, ",0.000,%llu,",
					(unsigned long long)(tc_hit + tc_skip + tc_miss));
				csv_quote(hll_log, tc_stack);
				fputc('\n', hll_log);
			}
			if (g_rq_prof.update_split && upd_n) {
				char upd_stack[256];
				snprintf(upd_stack, sizeof(upd_stack),
					 "n=%u dirty=%u nspr=%d events=%.3f plugin=%.3f scene=%.3f other=%.3f",
					 upd_n, upd_dirty, upd_nspr,
					 upd_events_ms, upd_plugin_ms, upd_scene_ms, upd_other_ms);
				fprintf(hll_log, "%.3f,", now_t_ms());
				csv_quote(hll_log, "ChipmunkSpriteEngine.Update_split");
				fprintf(hll_log, ",%.3f,%u,",
					upd_events_ms + upd_plugin_ms + upd_scene_ms + upd_other_ms,
					upd_n);
				csv_quote(hll_log, upd_stack);
				fputc('\n', hll_log);
			}
			if (g_rq_prof.vm_ops) {
				char vm_stack[384];
				int off = snprintf(vm_stack, sizeof(vm_stack), "n_vmop=%u n_hll=%u",
						   n_vmop, n_hll);
				uint32_t peeled[8];
				int n_peeled = 0;
				for (int k = 0; k < 8 && off > 0 && off < (int)sizeof(vm_stack) - 8; k++) {
					unsigned best_op = NR_OPCODES;
					uint32_t best_n = 0;
					for (unsigned op = 0; op < NR_OPCODES; op++) {
						if (vmop_n[op] <= best_n)
							continue;
						int skip = 0;
						for (int p = 0; p < n_peeled; p++) {
							if (peeled[p] == op) {
								skip = 1;
								break;
							}
						}
						if (skip)
							continue;
						best_n = vmop_n[op];
						best_op = op;
					}
					if (best_n == 0 || best_op >= NR_OPCODES)
						break;
					peeled[n_peeled++] = best_op;
					const char *nm = instructions[best_op].name;
					off += snprintf(vm_stack + off, sizeof(vm_stack) - (size_t)off,
							" %s=%u", nm ? nm : "?", best_n);
				}
				fprintf(hll_log, "%.3f,", now_t_ms());
				csv_quote(hll_log, "vm-ops");
				fprintf(hll_log, ",0.000,%u,", n_vmop);
				csv_quote(hll_log, vm_stack);
				fputc('\n', hll_log);
			}
			if (g_rq_prof.ain_fn) {
				ain_flush_current();
				if (n_ain_used > 0) {
					struct rq_ain_acc *aptrs[RQ_AIN_CAP];
					int na = 0;
					for (int i = 0; i < n_ain_used; i++)
						aptrs[na++] = &ain_tab[ain_used_idx[i]];
					qsort(aptrs, (size_t)na, sizeof(aptrs[0]), ain_ptr_cmp);
					int written = 0;
					for (int i = 0; i < na && written < 16; i++) {
						if (aptrs[i]->ms <= 0 && aptrs[i]->n == 0)
							continue;
						char fname[192];
						vm_prof_ain_fname(aptrs[i]->fno, fname, sizeof(fname));
						fprintf(hll_log, "%.3f,", now_t_ms());
						csv_quote(hll_log, "ain-fn");
						fprintf(hll_log, ",%.3f,%u,", aptrs[i]->ms, aptrs[i]->n);
						csv_quote(hll_log, fname[0] ? fname : "?");
						fputc('\n', hll_log);
						written++;
					}
				}
			}
			fflush(hll_log);
		}
	}

	if (!paused && out) {
		int vsync = SDL_GL_GetSwapInterval();
		fprintf(out,
			"%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%u,%u,%u,%d,",
			frame_no,
			t_ms,
			frame_ms,
			input_ms,
			plugin_ms,
			scene_ms,
			sprite_ms,
			dungeon_ms,
			tapir_ms,
			swap_ms,
			sleep_ms,
			n_sleep,
			sleep_req_ms,
			leftover_ms,
			leftover_other_ms,
			script_ms,
			hll_ms,
			hll_other_ms,
			n_hll,
			chipmunk_sleep_ms,
			counts[RQ_PROF_SPRITE],
			counts[RQ_PROF_DUNGEON],
			counts[RQ_PROF_TAPIR],
			vsync);
		csv_quote(out, ain_name);
		fputc(',', out);
		csv_quote(out, top_name);
		fprintf(out, ",%.3f,%llu,%llu,%llu,%u\n", top_ms,
			(unsigned long long)tc_hit, (unsigned long long)tc_skip,
			(unsigned long long)tc_miss, tc_entries);
		if ((frame_no % 30) == 0)
			fflush(out);
		if ((frame_no % 60) == 0) {
			double fps = frame_ms > 0.1 ? 1000.0 / frame_ms : 0;
			uint64_t hits = 0, misses = 0, skips = 0;
			unsigned entries = 0;
			gfx_text_cache_stats(&hits, &misses, &skips, &entries);
			fprintf(stderr,
				"[rq-prof] frame=%u fps=%.1f frame_ms=%.2f leftover_other=%.2f hll_other=%.2f script=%.2f top=%s:%.2f ain=%s text-cache hit=%llu skip=%llu miss=%llu entries=%u\n",
				frame_no, fps, frame_ms, leftover_other_ms, hll_other_ms, script_ms,
				top_name[0] ? top_name : "-",
				top_ms,
				ain_name[0] ? ain_name : "-",
				(unsigned long long)hits, (unsigned long long)skips,
				(unsigned long long)misses, entries);
		}
	}
	reset_accum();
}

#endif /* RQ_PROFILE */
