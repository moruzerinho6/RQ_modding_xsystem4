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

static FILE *out;
static char out_path[1024];
static uint64_t freq;
static uint64_t t_start;
static uint64_t t_last_frame;
static uint64_t t0[RQ_PROF_NR];
static uint64_t accum[RQ_PROF_NR];
static uint32_t counts[RQ_PROF_NR];
static uint32_t frame_no;
static bool paused;
static bool inited;

static double ticks_to_ms(uint64_t ticks)
{
	if (!freq)
		return 0;
	return (double)ticks * 1000.0 / (double)freq;
}

static void reset_accum(void)
{
	memset(accum, 0, sizeof(accum));
	memset(counts, 0, sizeof(counts));
}

static void resolve_path(void)
{
	const char *env = getenv("XSYS4_PROFILE");
	if (env && env[0]) {
		snprintf(out_path, sizeof(out_path), "%s", env);
		return;
	}
	char *base = SDL_GetBasePath();
	if (base) {
		snprintf(out_path, sizeof(out_path), "%sxsystem4-profile.csv", base);
		SDL_free(base);
		return;
	}
	snprintf(out_path, sizeof(out_path), "xsystem4-profile.csv");
}

void rq_prof_init(void)
{
	if (inited)
		return;
	freq = SDL_GetPerformanceFrequency();
	t_start = SDL_GetPerformanceCounter();
	t_last_frame = t_start;
	resolve_path();
	out = fopen(out_path, "w");
	if (!out) {
		fprintf(stderr, "[rq-prof] could not open %s\n", out_path);
		return;
	}
	fprintf(out,
		"frame,t_ms,frame_ms,input_ms,plugin_ms,scene_ms,sprite_ms,"
		"dungeon_ms,tapir_ms,swap_ms,n_sprite,n_dungeon,n_tapir,vsync\n");
	fflush(out);
	inited = true;
	atexit(rq_prof_fini);
	fprintf(stderr, "[rq-prof] writing %s (F12 pauses)\n", out_path);
}

void rq_prof_fini(void)
{
	if (!out)
		return;
	fflush(out);
	fclose(out);
	out = NULL;
	fprintf(stderr, "[rq-prof] closed %s (%u frames)\n", out_path, frame_no);
}

void rq_prof_begin(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	t0[b] = SDL_GetPerformanceCounter();
}

void rq_prof_end(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	accum[b] += SDL_GetPerformanceCounter() - t0[b];
}

void rq_prof_count(enum rq_prof_bucket b)
{
	if ((unsigned)b >= RQ_PROF_NR)
		return;
	counts[b]++;
}

void rq_prof_toggle_pause(void)
{
	paused = !paused;
	fprintf(stderr, "[rq-prof] %s\n", paused ? "paused" : "recording");
}

void rq_prof_frame(void)
{
	if (!inited)
		rq_prof_init();
	if (!out) {
		reset_accum();
		return;
	}

	uint64_t now = SDL_GetPerformanceCounter();
	double frame_ms = ticks_to_ms(now - t_last_frame);
	double t_ms = ticks_to_ms(now - t_start);
	t_last_frame = now;
	frame_no++;

	if (!paused) {
		int vsync = SDL_GL_GetSwapInterval();
		fprintf(out,
			"%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%d\n",
			frame_no,
			t_ms,
			frame_ms,
			ticks_to_ms(accum[RQ_PROF_INPUT]),
			ticks_to_ms(accum[RQ_PROF_PLUGIN]),
			ticks_to_ms(accum[RQ_PROF_SCENE]),
			ticks_to_ms(accum[RQ_PROF_SPRITE]),
			ticks_to_ms(accum[RQ_PROF_DUNGEON]),
			ticks_to_ms(accum[RQ_PROF_TAPIR]),
			ticks_to_ms(accum[RQ_PROF_SWAP]),
			counts[RQ_PROF_SPRITE],
			counts[RQ_PROF_DUNGEON],
			counts[RQ_PROF_TAPIR],
			vsync);
		if ((frame_no % 30) == 0)
			fflush(out);
		if ((frame_no % 60) == 0) {
			double fps = frame_ms > 0.1 ? 1000.0 / frame_ms : 0;
			fprintf(stderr,
				"[rq-prof] frame=%u fps=%.1f frame_ms=%.2f input=%.2f plugin=%.2f scene=%.2f sprite=%.2f dungeon=%.2f tapir=%.2f swap=%.2f sprites=%u vsync=%d\n",
				frame_no, fps, frame_ms,
				ticks_to_ms(accum[RQ_PROF_INPUT]),
				ticks_to_ms(accum[RQ_PROF_PLUGIN]),
				ticks_to_ms(accum[RQ_PROF_SCENE]),
				ticks_to_ms(accum[RQ_PROF_SPRITE]),
				ticks_to_ms(accum[RQ_PROF_DUNGEON]),
				ticks_to_ms(accum[RQ_PROF_TAPIR]),
				ticks_to_ms(accum[RQ_PROF_SWAP]),
				counts[RQ_PROF_SPRITE],
				vsync);
		}
	}
	reset_accum();
}

#endif /* RQ_PROFILE */
