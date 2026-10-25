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

#define RQ_PROF_SLOW_MS 50.0

static FILE *out;
static FILE *ain_log;
static char out_path[1024];
static char ain_path[1100];
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
	} else {
		snprintf(ain_path, sizeof(ain_path), "%s-ain.log", out_path);
	}
}

static void ain_log_event(const char *kind, double elapsed, double requested, const char *stack)
{
	if (!ain_log || paused)
		return;
	fprintf(ain_log, "%.3f,%s,%.3f,%.3f,", now_t_ms(), kind, elapsed, requested);
	csv_quote(ain_log, stack ? stack : "");
	fputc('\n', ain_log);
	fflush(ain_log);
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
		"dungeon_ms,tapir_ms,swap_ms,sleep_ms,n_sleep,sleep_req_ms,"
		"leftover_ms,leftover_other_ms,n_sprite,n_dungeon,n_tapir,vsync,ain\n");
	fflush(out);
	ain_log = fopen(ain_path, "w");
	if (ain_log) {
		fprintf(ain_log, "t_ms,kind,elapsed_ms,requested_ms,ain_stack\n");
		fflush(ain_log);
	} else {
		fprintf(stderr, "[rq-prof] could not open %s\n", ain_path);
	}
	inited = true;
	atexit(rq_prof_fini);
	fprintf(stderr, "[rq-prof] writing %s (F12 pauses)\n", out_path);
	if (ain_log)
		fprintf(stderr, "[rq-prof] slow AIN log %s (sleep/leftover > %.0f ms)\n",
			ain_path, RQ_PROF_SLOW_MS);
}

void rq_prof_fini(void)
{
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

void rq_prof_note_sleep(int requested_ms, double elapsed_ms, const char *ain_stack)
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
		ain_log_event("sleep", elapsed_ms, (double)requested_ms, ain_stack);
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

	char ain_name[512];
	ain_name[0] = '\0';
	if (sleep_ain[0]) {
		snprintf(ain_name, sizeof(ain_name), "%s", sleep_ain);
	} else if (leftover_ms > RQ_PROF_SLOW_MS) {
		char stack[512];
		vm_prof_ain_stack(stack, sizeof(stack));
		copy_top_name(ain_name, sizeof(ain_name), stack);
		if (leftover_other_ms > RQ_PROF_SLOW_MS)
			ain_log_event("leftover", leftover_other_ms, 0, stack);
	}

	if (leftover_other_ms > RQ_PROF_SLOW_MS && sleep_ain[0]) {
		char stack[512];
		vm_prof_ain_stack(stack, sizeof(stack));
		ain_log_event("leftover", leftover_other_ms, 0, stack);
	}

	if (!paused) {
		int vsync = SDL_GL_GetSwapInterval();
		fprintf(out,
			"%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%u,%u,%u,%d,",
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
			counts[RQ_PROF_SPRITE],
			counts[RQ_PROF_DUNGEON],
			counts[RQ_PROF_TAPIR],
			vsync);
		csv_quote(out, ain_name);
		fputc('\n', out);
		if ((frame_no % 30) == 0)
			fflush(out);
		if ((frame_no % 60) == 0) {
			double fps = frame_ms > 0.1 ? 1000.0 / frame_ms : 0;
			fprintf(stderr,
				"[rq-prof] frame=%u fps=%.1f frame_ms=%.2f sleep=%.2f leftover=%.2f leftover_other=%.2f ain=%s sprites=%u vsync=%d\n",
				frame_no, fps, frame_ms, sleep_ms, leftover_ms, leftover_other_ms,
				ain_name[0] ? ain_name : "-",
				counts[RQ_PROF_SPRITE],
				vsync);
		}
	}
	reset_accum();
}

#endif /* RQ_PROFILE */
