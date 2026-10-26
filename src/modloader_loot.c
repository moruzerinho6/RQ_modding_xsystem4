/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Loot-lock: roll chests once at place, skip the open roll.
 * Red chests keep vanilla 成功確率 / explode. Lua may raise that %
 * (rq.chest); fail still runs 赤宝箱処理 explode and discards the lock.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define LOOT_ENCODE 10000
#define ITEM_MAX 3001
#define MAP_DIM 51
#define MAP_BMAP_MAX 17
#define UNID_NAME "Unidentified item"
#define UNID_COMMENT "You have not identified this item yet."
/* Vanilla unidentified CGs (common ?). Rare ? CGs would leak rarity. */
#define UNID_CG_WEAPON 11337
#define UNID_CG_ARMOR 11338
#define UNID_CG_ITEM 11339
#define UNID_CG_SPECIAL 11340

/* sI member slots */
#define SI_EXIST 0
#define SI_CATALOG 1
#define SI_CG 2
#define SI_NAME 3
#define SI_KIND 4
#define SI_UNID 7
#define SI_INFO 12

/* sM member slots */
#define SM_MAPTT 1
#define SM_MAPTZ 2

static int g_on;
static int g_chest_wrapped;
static int g_fno_gen = -1;
static int g_fno_exist = -1;
static int g_fno_init = -1;
static int g_gi_item = -1;
static int g_gi_map = -1;
static int g_gi_bmap = -1;
static int g_gi_magnum = -1;
static int g_red_base_pct = 30;
static int g_attempt_ref = LUA_NOREF;
static int g_result_ref = LUA_NOREF;
static int g_rand_seeded;
static int g_dialog_patched;
static int g_pct_local = 5;
static int g_chance_msg = -1;
static int g_prepared;
static int g_prep_base, g_prep_pct, g_prep_id, g_prep_x, g_prep_y;

#define CHEST_KV 24
static struct {
	char key[32];
	int val;
	int used;
} g_kv[CHEST_KV];

static int *map_cell(int member, int x, int y);
static int item_get(int ii, int member);
static void call_i(int fno, int a);
static int chest_lua_pct(int x, int y, int id, int base);

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static int find_fn(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

static int find_global(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int g;
	if (!sjis)
		return -1;
	g = ain_get_global(ain, sjis);
	free(sjis);
	return g;
}

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
}

static uint32_t emit_ifz(struct buffer *out)
{
	uint32_t at = (uint32_t)out->index;
	emit1(out, IFZ, 0);
	return at;
}

static uint32_t emit_ifnz(struct buffer *out)
{
	uint32_t at = (uint32_t)out->index;
	emit1(out, IFNZ, 0);
	return at;
}

static void patch_j(struct buffer *out, uint32_t at, struct ain *ain)
{
	buffer_write_int32_at(out, at + 2, (uint32_t)(ain->code_size + out->index));
}

static int wrap_md_kotei(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	uint32_t j_ret, j_type, j_enc;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, SH_LOCALREF, 3);
	emit1(&out, SH_LOCALREF, 4);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, DUP);
	j_ret = emit_ifz(&out);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, PUSH, 1);
	emit0(&out, EQUALE);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, PUSH, 2);
	emit0(&out, EQUALE);
	emit0(&out, OR);
	j_type = emit_ifz(&out);
	emit1(&out, SH_LOCALREF, 3);
	emit1(&out, PUSH, LOOT_ENCODE);
	emit0(&out, GTE);
	j_enc = emit_ifnz(&out);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 4);
	emit1(&out, CALLSYS, VM_XSYS_LOOT_LOCK_TILE);
	patch_j(&out, j_ret, ain);
	patch_j(&out, j_type, ain);
	patch_j(&out, j_enc, ain);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static int wrap_md_rand(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	uint32_t j_ret, j_type;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, SH_LOCALREF, 3);
	emit1(&out, SH_LOCALREF, 4);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, DUP);
	j_ret = emit_ifz(&out);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, PUSH, 1);
	emit0(&out, EQUALE);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, PUSH, 2);
	emit0(&out, EQUALE);
	emit0(&out, OR);
	j_type = emit_ifz(&out);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, SH_LOCALREF, 3);
	emit1(&out, CALLSYS, VM_XSYS_LOOT_LOCK_SCAN);
	patch_j(&out, j_ret, ain);
	patch_j(&out, j_type, ain);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static int wrap_gen(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	uint32_t j_orig;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, PUSH, LOOT_ENCODE);
	emit0(&out, GTE);
	j_orig = emit_ifz(&out);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, CALLSYS, VM_XSYS_LOOT_OPEN);
	emit0(&out, RETURN);
	patch_j(&out, j_orig, ain);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, SH_LOCALREF, 2);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static uint16_t loot_rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t loot_rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static int loot_op_len(uint16_t op)
{
	switch (op) {
	case PUSH:
	case JUMP:
	case IFZ:
	case IFNZ:
	case CALLFUNC:
	case CALLSYS:
	case SH_LOCALREF:
		return 6;
	case CALLHLL:
		return 10;
	default:
		return 2;
	}
}

/* First PUSH 2..100 in vanilla 赤宝箱処理 — typically the disarm %. */
static int scan_red_pct(struct ain *ain, int fno)
{
	uint32_t start, end, ip;
	int i, found = 0;

	if (!ain || fno < 0 || fno >= ain->nr_functions)
		return 0;
	start = ain->functions[fno].address;
	end = (uint32_t)ain->code_size;
	for (i = 0; i < ain->nr_functions; i++) {
		uint32_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	for (ip = start; ip + 6 <= end; ) {
		uint16_t op = loot_rd16(ain->code + ip);
		int len = loot_op_len(op);
		if (op == PUSH) {
			int v = loot_rd32(ain->code + ip + 2);
			if (v >= 2 && v <= 100) {
				found = v;
				break;
			}
		}
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	return found;
}

static void loot_wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void loot_wr32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
}

/*
 * Vanilla: PUSH 100; CALLFUNC RAND; SH_LOCALREF 成功確率; GT
 * (RAND is 1..100, explode if roll > pct). Replace with CALLSYS so Lua can
 * raise %, then the original IFZ still takes explode vs open.
 */
static int patch_red_roll(struct ain *ain, int red_fno)
{
	int rand_fno;
	uint32_t start, end, ip;

	if (g_chest_wrapped)
		return 1;
	if (!ain || red_fno < 0)
		return 0;
	rand_fno = find_fn(ain, "RAND");
	if (rand_fno < 0) {
		rq_log("chest: RAND not found; red roll unpatched");
		return 0;
	}
	start = ain->functions[red_fno].address;
	end = (uint32_t)ain->code_size;
	{
		int i;
		for (i = 0; i < ain->nr_functions; i++) {
			uint32_t a = ain->functions[i].address;
			if (a > start && a < end)
				end = a;
		}
	}
	for (ip = start; ip + 20 <= end; ) {
		uint16_t op = loot_rd16(ain->code + ip);
		int len = loot_op_len(op);
		if (op == PUSH && loot_rd32(ain->code + ip + 2) == 100
		    && loot_rd16(ain->code + ip + 6) == CALLFUNC
		    && loot_rd32(ain->code + ip + 8) == rand_fno
		    && loot_rd16(ain->code + ip + 12) == SH_LOCALREF
		    && loot_rd16(ain->code + ip + 18) == GT) {
			int loc = loot_rd32(ain->code + ip + 14);
			uint8_t *p = ain->code + ip;
			loot_wr16(p, SH_LOCALREF);
			loot_wr32(p + 2, loc);
			loot_wr16(p + 6, CALLSYS);
			loot_wr32(p + 8, VM_XSYS_CHEST_ATTEMPT);
			loot_wr16(p + 12, PUSH);
			loot_wr32(p + 14, 0);
			loot_wr16(p + 18, POP);
			g_chest_wrapped = 1;
			rq_log("chest: patched 赤宝箱処理 RAND vs local %d (vanilla explode stays)",
			       loc);
			return 1;
		}
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	rq_log("chest: RAND/GT site not found in 赤宝箱処理");
	return 0;
}

static uint32_t chest_func_end(struct ain *ain, int fno)
{
	uint32_t start, end;
	int i;

	start = ain->functions[fno].address;
	end = (uint32_t)ain->code_size;
	for (i = 0; i < ain->nr_functions; i++) {
		uint32_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	return end;
}

static int chest_second_msg(struct ain *ain, uint32_t from, uint32_t until)
{
	uint32_t ip;
	int n = 0;

	for (ip = from; ip + 6 <= until; ) {
		uint16_t op = loot_rd16(ain->code + ip);
		int w = (op < NR_OPCODES) ? instruction_width(op) : 2;
		if (op == _MSG) {
			n++;
			if (n == 2)
				return loot_rd32(ain->code + ip + 2);
		}
		if (w < 2)
			w = 2;
		ip += (uint32_t)w;
	}
	return -1;
}

/*
 * Before the Open/Back away ADV: CALLSYS PREPARE (Lua pct → 成功確率 + MSG).
 * Vanilla only has 30/60/80/100 lines; force the 30% slot and skip the others
 * so 90% can display. The later 成功確率==100 "opened" line is left alone.
 */
static int patch_red_dialog(struct ain *ain, int red_fno)
{
	int spd_fno;
	uint32_t start, end, ip;

	if (g_dialog_patched)
		return 1;
	if (!ain || red_fno < 0)
		return 0;
	spd_fno = find_fn(ain, "字速度");
	if (spd_fno < 0) {
		rq_log("chest: 字速度 not found; disarm dialog unpatched");
		return 0;
	}
	start = ain->functions[red_fno].address;
	end = chest_func_end(ain, red_fno);
	for (ip = start; ip + 20 <= end; ) {
		uint16_t op = loot_rd16(ain->code + ip);
		int w = (op < NR_OPCODES) ? instruction_width(op) : 2;
		if (op == PUSH && loot_rd32(ain->code + ip + 2) == 0
		    && loot_rd16(ain->code + ip + 6) == CALLFUNC
		    && loot_rd32(ain->code + ip + 8) == spd_fno
		    && loot_rd16(ain->code + ip + 12) == SH_IF_LOC_NE_IMM
		    && loot_rd32(ain->code + ip + 18) == 30) {
			uint32_t if30 = ip + 12;
			int loc = loot_rd32(ain->code + if30 + 2);
			uint32_t skip30 = (uint32_t)loot_rd32(ain->code + if30 + 10);
			int i, expect[] = { 60, 80, 100 };

			loot_wr16(ain->code + ip, CALLSYS);
			loot_wr32(ain->code + ip + 2, VM_XSYS_CHEST_PREPARE);
			loot_wr32(ain->code + if30 + 10, (int32_t)(if30 + instruction_width(SH_IF_LOC_NE_IMM)));
			g_pct_local = loc;
			g_chance_msg = chest_second_msg(ain, if30 + instruction_width(SH_IF_LOC_NE_IMM), skip30);

			ip = skip30;
			for (i = 0; i < 3; i++) {
				if (ip + 14 > end
				    || loot_rd16(ain->code + ip) != SH_IF_LOC_NE_IMM
				    || loot_rd32(ain->code + ip + 2) != loc
				    || loot_rd32(ain->code + ip + 6) != expect[i]) {
					rq_log("chest: dialog IF %d%% not at 0x%x", expect[i], ip);
					break;
				}
				loot_wr32(ain->code + ip + 6, -1);
				ip = (uint32_t)loot_rd32(ain->code + ip + 10);
			}
			g_dialog_patched = 1;
			rq_log("chest: disarm dialog uses attempt %% (MSG %d, 成功確率 local %d)",
			       g_chance_msg, g_pct_local);
			return 1;
		}
		if (w < 2)
			w = 2;
		ip += (uint32_t)w;
	}
	rq_log("chest: 字速度 + 30%% dialog IF not found");
	return 0;
}

static int kv_get(const char *key, int *ok)
{
	int i;
	*ok = 0;
	if (!key)
		return 0;
	for (i = 0; i < CHEST_KV; i++) {
		if (g_kv[i].used && strcmp(g_kv[i].key, key) == 0) {
			*ok = 1;
			return g_kv[i].val;
		}
	}
	return 0;
}

static void kv_set(const char *key, int val)
{
	int i, slot = -1;
	if (!key || !*key)
		return;
	for (i = 0; i < CHEST_KV; i++) {
		if (g_kv[i].used && strcmp(g_kv[i].key, key) == 0) {
			g_kv[i].val = val;
			return;
		}
		if (!g_kv[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0)
		return;
	snprintf(g_kv[slot].key, sizeof g_kv[slot].key, "%s", key);
	g_kv[slot].val = val;
	g_kv[slot].used = 1;
}

static int chest_id_at(int x, int y)
{
	int *tz = map_cell(SM_MAPTZ, x, y);
	if (tz && *tz >= LOOT_ENCODE)
		return *tz - LOOT_ENCODE;
	return 0;
}

static void chest_push_info(lua_State *L, int x, int y, int id, int base)
{
	lua_newtable(L);
	lua_pushinteger(L, x);
	lua_setfield(L, -2, "x");
	lua_pushinteger(L, y);
	lua_setfield(L, -2, "y");
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");
	lua_pushinteger(L, base);
	lua_setfield(L, -2, "base_pct");
	lua_pushstring(L, "red");
	lua_setfield(L, -2, "kind");
}

static void chest_set_chance_msg(int pct)
{
	char utf[80];
	char *sjis;
	struct string *s;

	if (!ain || g_chance_msg < 0 || g_chance_msg >= ain->nr_messages)
		return;
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	snprintf(utf, sizeof utf, "  (%d%% chance to disarm)", pct);
	sjis = utf2sjis(utf, 0);
	if (!sjis)
		return;
	s = make_string(sjis, strlen(sjis));
	free(sjis);
	if (!s)
		return;
	if (ain->messages[g_chance_msg])
		free_string(ain->messages[g_chance_msg]);
	ain->messages[g_chance_msg] = s;
}

static void chest_write_pct_local(int pct)
{
	struct page *lp = local_page();

	if (!lp || g_pct_local < 0 || g_pct_local >= lp->nr_vars)
		return;
	lp->values[g_pct_local].i = pct;
}

static void chest_prepare(void)
{
	int x, y, id, vanilla, pct;

	x = local_get(0).i;
	y = local_get(1).i;
	vanilla = local_get(g_pct_local).i;
	if (vanilla < 0)
		vanilla = 0;
	if (vanilla > 100)
		vanilla = 100;
	id = chest_id_at(x, y);
	pct = chest_lua_pct(x, y, id, vanilla);
	g_prep_x = x;
	g_prep_y = y;
	g_prep_id = id;
	g_prep_base = vanilla;
	g_prep_pct = pct;
	g_prepared = 1;
	chest_write_pct_local(pct);
	chest_set_chance_msg(pct);
	if (g_rq_verbose || g_rq_terminal)
		rq_log("Chest dialog (%d,%d) id=%d vanilla %d%% -> show %d%%",
		       x, y, id, vanilla, pct);
	stack_push(0);
}

static int chest_lua_pct(int x, int y, int id, int base)
{
	lua_State *L = rq_lua();
	int pct = base;

	if (!L || g_attempt_ref == LUA_NOREF)
		return pct;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_attempt_ref);
	chest_push_info(L, x, y, id, base);
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		rq_log("Chest open hook error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return pct;
	}
	if (lua_isnumber(L, -1)) {
		pct = (int)lua_tonumber(L, -1);
	} else if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "pct");
		if (lua_isnumber(L, -1))
			pct = (int)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return pct;
}

static void chest_lua_result(int x, int y, int id, int base, int pct, int rolled, int success)
{
	lua_State *L = rq_lua();
	if (!L || g_result_ref == LUA_NOREF)
		return;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_result_ref);
	chest_push_info(L, x, y, id, base);
	lua_pushinteger(L, pct);
	lua_setfield(L, -2, "pct");
	lua_pushinteger(L, rolled);
	lua_setfield(L, -2, "rolled");
	lua_pushboolean(L, success);
	lua_setfield(L, -2, "success");
	lua_pushboolean(L, !success);
	lua_setfield(L, -2, "exploded");
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		rq_log("Chest result hook error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

/* Drop a lock-rolled instance so explode cannot leak it into warehouse. */
static void loot_discard_lock(int x, int y)
{
	int *tz, ii;

	tz = map_cell(SM_MAPTZ, x, y);
	if (!tz || *tz < LOOT_ENCODE)
		return;
	ii = *tz - LOOT_ENCODE;
	if (ii > 0 && item_get(ii, SI_EXIST) == 9 && g_fno_init >= 0)
		call_i(g_fno_init, ii);
	rq_log("loot-preview: explode discarded locked item %d at (%d,%d)", ii, x, y);
}

/*
 * Vanilla 成功確率 is on the stack. Match RAND(100): 1..100, explode if roll > pct.
 * Return 1 like GT (take explode path), 0 like success.
 */
static int chest_attempt(int vanilla_pct)
{
	int x, y, id, pct, rolled, success;

	if (!g_rand_seeded) {
		srand((unsigned)time(NULL));
		g_rand_seeded = 1;
	}
	if (g_prepared) {
		x = g_prep_x;
		y = g_prep_y;
		id = g_prep_id;
		vanilla_pct = g_prep_base;
		pct = g_prep_pct;
		g_prepared = 0;
	} else {
		x = local_get(0).i;
		y = local_get(1).i;
		if (vanilla_pct < 0)
			vanilla_pct = 0;
		if (vanilla_pct > 100)
			vanilla_pct = 100;
		id = chest_id_at(x, y);
		pct = chest_lua_pct(x, y, id, vanilla_pct);
	}
	rolled = (rand() % 100) + 1;
	success = rolled <= pct;
	if (!success)
		loot_discard_lock(x, y);
	chest_lua_result(x, y, id, vanilla_pct, pct, rolled, success);
	if (g_rq_verbose || g_rq_terminal)
		rq_log("Chest (%d,%d) id=%d vanilla %d%% attempt %d%% roll %d -> %s",
		       x, y, id, vanilla_pct, pct, rolled,
		       success ? "open" : "explode");
	return success ? 0 : 1;
}

static int wrap_quest_end(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, VM_XSYS_LOOT_CLEAR);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static struct page *item_page(int ii)
{
	struct page *arr;
	int slot;

	if (g_gi_item < 0 || ii <= 0 || ii >= ITEM_MAX)
		return NULL;
	arr = heap_get_page(global_get(g_gi_item).i);
	if (!arr || arr->type != ARRAY_PAGE || ii >= arr->nr_vars)
		return NULL;
	slot = arr->values[ii].i;
	if (!heap_index_valid(slot))
		return NULL;
	return heap_get_page(slot);
}

static int item_get(int ii, int member)
{
	struct page *p = item_page(ii);
	if (!p || member < 0 || member >= p->nr_vars)
		return 0;
	return p->values[member].i;
}

static int *map_cell(int member, int x, int y)
{
	int bmap;
	struct page *marr, *sm, *grid, *row;
	int mslot, gslot, rslot;

	if (g_gi_map < 0 || g_gi_bmap < 0)
		return NULL;
	bmap = global_get(g_gi_bmap).i;
	marr = heap_get_page(global_get(g_gi_map).i);
	if (!marr || bmap < 0 || bmap >= marr->nr_vars)
		return NULL;
	mslot = marr->values[bmap].i;
	if (!heap_index_valid(mslot))
		return NULL;
	sm = heap_get_page(mslot);
	if (!sm || member < 0 || member >= sm->nr_vars)
		return NULL;
	gslot = sm->values[member].i;
	if (!heap_index_valid(gslot))
		return NULL;
	grid = heap_get_page(gslot);
	if (!grid || x < 0 || x >= grid->nr_vars)
		return NULL;
	rslot = grid->values[x].i;
	if (!heap_index_valid(rslot))
		return NULL;
	row = heap_get_page(rslot);
	if (!row || y < 0 || y >= row->nr_vars)
		return NULL;
	return &row->values[y].i;
}

static int magnum_id(void)
{
	if (g_gi_magnum < 0)
		return 0;
	return global_get(g_gi_magnum).i;
}

static int count_exist(int exist)
{
	int n = 0, i;
	for (i = 1; i < ITEM_MAX; i++) {
		if (item_get(i, SI_EXIST) == exist)
			n++;
	}
	return n;
}

static int count_locked_hold(void)
{
	int n = 0, i, mag = magnum_id();
	for (i = 1; i < ITEM_MAX; i++) {
		if (item_get(i, SI_EXIST) == 9 && i != mag)
			n++;
	}
	return n;
}

static int warehouse_full(void)
{
	return count_exist(8) + count_locked_hold() >= 15;
}

static int call_iii(int fno, int a, int b, int c)
{
	int r;
	if (fno < 0)
		return 0;
	stack_push(a);
	stack_push(b);
	stack_push(c);
	vm_call(fno, -1);
	r = stack_pop().i;
	return r;
}

static int call_ii(int fno, int a, int b)
{
	int r;
	if (fno < 0)
		return 0;
	stack_push(a);
	stack_push(b);
	vm_call(fno, -1);
	r = stack_pop().i;
	return r;
}

static void call_i(int fno, int a)
{
	if (fno < 0)
		return;
	stack_push(a);
	vm_call(fno, -1);
}

static int catalog_seen(int catalog, int except_ii)
{
	int i, exist, unid;
	if (catalog <= 0)
		return 0;
	for (i = 1; i < ITEM_MAX; i++) {
		if (i == except_ii)
			continue;
		if (item_get(i, SI_CATALOG) != catalog)
			continue;
		unid = item_get(i, SI_UNID);
		if (unid != 0)
			continue;
		exist = item_get(i, SI_EXIST);
		if (exist == 0 || exist == 6 || exist == 9)
			continue;
		return i;
	}
	return 0;
}

static int unid_cg(int ii)
{
	int kind = item_get(ii, SI_KIND);
	if (kind == 11)
		return UNID_CG_ARMOR;
	if (kind == 17)
		return UNID_CG_ITEM;
	if (kind == 21)
		return UNID_CG_SPECIAL;
	return UNID_CG_WEAPON;
}

static char *item_name_utf(int ii)
{
	struct page *p = item_page(ii);
	int slot;
	struct string *s;
	char *utf;
	if (!p)
		return strdup("?");
	slot = p->values[SI_NAME].i;
	if (!string_index_valid(slot))
		return strdup("?");
	s = heap_get_string(slot);
	if (!s || !s->text)
		return strdup("?");
	utf = sjis2utf(s->text, 0);
	return utf ? utf : strdup("?");
}

static char *item_info_utf(int ii)
{
	struct page *p = item_page(ii);
	int slot;
	struct string *s;
	char *utf, *q;
	if (!p || SI_INFO >= p->nr_vars)
		return strdup("");
	slot = p->values[SI_INFO].i;
	if (!string_index_valid(slot))
		return strdup("");
	s = heap_get_string(slot);
	if (!s || !s->text)
		return strdup("");
	utf = sjis2utf(s->text, 0);
	if (!utf)
		return strdup("");
	for (q = utf; *q; q++) {
		if (*q == '$')
			*q = '\n';
	}
	return utf;
}

static void loot_lock_tile(int x, int y, int lank)
{
	int *tt, *tz, type, fixed, ii;

	if (!g_on)
		return;
	tt = map_cell(SM_MAPTT, x, y);
	tz = map_cell(SM_MAPTZ, x, y);
	if (!tt || !tz)
		return;
	type = *tt;
	if (type != 1 && type != 2)
		return;
	fixed = *tz;
	if (fixed >= LOOT_ENCODE)
		return;
	if (warehouse_full()) {
		rq_log("loot-preview: skip lock (%d,%d) warehouse full", x, y);
		return;
	}
	/* Match 宝箱処理 / 赤宝箱処理: random red uses fg=1; fixed always fg=0.
	 * Random open also passes lank=0, not maptv. */
	if (fixed == 0)
		ii = call_iii(g_fno_gen, (type == 2) ? 1 : 0, 0, 0);
	else
		ii = call_iii(g_fno_gen, 0, fixed, lank);
	if (ii <= 0)
		return;
	if (g_fno_exist >= 0)
		call_ii(g_fno_exist, ii, 9);
	*tz = LOOT_ENCODE + ii;
	rq_log("loot-preview: lock (%d,%d) type=%d -> item %d catalog %d",
	       x, y, type, ii, item_get(ii, SI_CATALOG));
}

static void loot_lock_scan(int type, int fixed, int lank)
{
	int x, y;
	int *tt, *tz;

	if (!g_on || (type != 1 && type != 2))
		return;
	if (fixed >= LOOT_ENCODE)
		return;
	for (x = 1; x < MAP_DIM; x++) {
		for (y = 1; y < MAP_DIM; y++) {
			tt = map_cell(SM_MAPTT, x, y);
			tz = map_cell(SM_MAPTZ, x, y);
			if (!tt || !tz)
				continue;
			if (*tt == type && *tz == fixed) {
				loot_lock_tile(x, y, lank);
				return;
			}
		}
	}
}

static int loot_open(int encoded)
{
	int ii = encoded - LOOT_ENCODE;
	if (ii <= 0 || ii >= ITEM_MAX)
		return 0;
	if (item_get(ii, SI_EXIST) == 9 && g_fno_exist >= 0)
		call_ii(g_fno_exist, ii, 8);
	return ii;
}

static void push_item_row(lua_State *L, int ii, const char *source, int floor, int x, int y)
{
	int catalog, twin, seen, cg;
	char *name, *comment;

	lua_newtable(L);
	lua_pushinteger(L, ii);
	lua_setfield(L, -2, "id");
	lua_pushstring(L, source);
	lua_setfield(L, -2, "source");
	lua_pushinteger(L, floor);
	lua_setfield(L, -2, "floor");
	lua_pushinteger(L, x);
	lua_setfield(L, -2, "x");
	lua_pushinteger(L, y);
	lua_setfield(L, -2, "y");
	catalog = item_get(ii, SI_CATALOG);
	lua_pushinteger(L, catalog);
	lua_setfield(L, -2, "catalog");
	twin = catalog_seen(catalog, ii);
	seen = twin != 0;
	lua_pushboolean(L, seen);
	lua_setfield(L, -2, "seen");
	if (seen) {
		int src = twin ? twin : ii;
		cg = item_get(src, SI_CG);
		name = item_name_utf(src);
		comment = item_info_utf(src);
		lua_pushinteger(L, 0);
		lua_setfield(L, -2, "overlay");
	} else {
		cg = unid_cg(ii);
		name = strdup(UNID_NAME);
		comment = strdup(UNID_COMMENT);
		lua_pushinteger(L, 10403);
		lua_setfield(L, -2, "overlay");
	}
	lua_pushinteger(L, cg);
	lua_setfield(L, -2, "cg");
	lua_pushstring(L, name ? name : UNID_NAME);
	lua_setfield(L, -2, "name");
	lua_pushstring(L, comment ? comment : UNID_COMMENT);
	lua_setfield(L, -2, "comment");
	free(name);
	free(comment);
}

int rq_loot_list(lua_State *L)
{
	int mag, x, y, b, saved_bmap, n = 0;
	int *tt, *tz;
	struct page *marr;

	lua_newtable(L);
	if (!g_on)
		return 1;
	mag = magnum_id();
	if (mag > 0 && item_get(mag, SI_EXIST) == 9) {
		lua_pushinteger(L, ++n);
		push_item_row(L, mag, "magnum", 0, 0, 0);
		lua_settable(L, -3);
	}
	if (g_gi_bmap < 0 || g_gi_map < 0)
		return 1;
	saved_bmap = global_get(g_gi_bmap).i;
	marr = heap_get_page(global_get(g_gi_map).i);
	if (!marr) {
		global_set(g_gi_bmap, vm_int(saved_bmap), false);
		return 1;
	}
	for (b = 1; b < marr->nr_vars && b <= MAP_BMAP_MAX; b++) {
		global_set(g_gi_bmap, vm_int(b), false);
		for (x = 1; x < MAP_DIM; x++) {
			for (y = 1; y < MAP_DIM; y++) {
				int ii;
				tt = map_cell(SM_MAPTT, x, y);
				tz = map_cell(SM_MAPTZ, x, y);
				if (!tt || !tz)
					continue;
				if (*tt != 1 && *tt != 2)
					continue;
				if (*tz < LOOT_ENCODE)
					continue;
				ii = *tz - LOOT_ENCODE;
				if (ii == mag)
					continue;
				lua_pushinteger(L, ++n);
				push_item_row(L, ii, "chest", b, x, y);
				lua_settable(L, -3);
			}
		}
	}
	global_set(g_gi_bmap, vm_int(saved_bmap), false);
	return 1;
}

int rq_lua_loot_list(lua_State *L)
{
	return rq_loot_list(L);
}

static void loot_clear(void)
{
	int i, mag;
	if (!g_on)
		return;
	/* Quest-end orig already zeros ◆マグナムの宝物. Skip whatever is
	 * still the Magnum instance so a wrap-order change cannot wipe it
	 * before Ｒ８アイテム取得(345). Leftover locked holds (exist 9) go. */
	mag = magnum_id();
	for (i = 1; i < ITEM_MAX; i++) {
		if (i == mag)
			continue;
		if (item_get(i, SI_EXIST) == 9 && g_fno_init >= 0)
			call_i(g_fno_init, i);
	}
}

int rq_loot_syscall(int code)
{
	int x, y, lank, type, fixed, encoded;

	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_LOOT_LOCK_TILE:
		lank = stack_pop().i;
		y = stack_pop().i;
		x = stack_pop().i;
		loot_lock_tile(x, y, lank);
		return 1;
	case VM_XSYS_LOOT_LOCK_SCAN:
		lank = stack_pop().i;
		fixed = stack_pop().i;
		type = stack_pop().i;
		loot_lock_scan(type, fixed, lank);
		return 1;
	case VM_XSYS_LOOT_OPEN:
		encoded = stack_pop().i;
		stack_push(loot_open(encoded));
		return 1;
	case VM_XSYS_LOOT_CLEAR:
		loot_clear();
		return 1;
	default:
		return 0;
	}
}

int rq_loot_install(struct ain *ain)
{
	int f_kotei, f_rand, f_gen, f_red, f_chest, f_end;
	int n = 0;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_kotei = find_fn(ain, "ＭＤ固定");
	f_rand = find_fn(ain, "ＭＤランダム");
	f_gen = find_fn(ain, "宝箱の中身生成");
	f_red = find_fn(ain, "赤宝箱処理");
	f_chest = find_fn(ain, "宝箱処理");
	f_end = find_fn(ain, "クエスト終了処理");
	g_fno_exist = find_fn(ain, "Ｉ存在");
	g_fno_init = find_fn(ain, "Ｉ初期化");
	g_fno_gen = f_gen;
	g_gi_item = find_global(ain, "Ｉ");
	g_gi_map = find_global(ain, "Ｍ");
	g_gi_bmap = find_global(ain, "◆ＢＭＡＰ");
	g_gi_magnum = find_global(ain, "◆マグナムの宝物");

	if (f_kotei < 0 || f_rand < 0 || f_gen < 0 || f_red < 0 || f_chest < 0) {
		rq_log("loot: missing AIN function (kotei=%d rand=%d gen=%d red=%d chest=%d)",
		       f_kotei, f_rand, f_gen, f_red, f_chest);
		return 0;
	}
	n += wrap_md_kotei(ain, f_kotei);
	n += wrap_md_rand(ain, f_rand);
	n += wrap_gen(ain, f_gen);
	{
		int scanned = scan_red_pct(ain, f_red);
		if (scanned > 0)
			g_red_base_pct = scanned;
	}
	n += patch_red_roll(ain, f_red);
	if (f_end >= 0)
		n += wrap_quest_end(ain, f_end);
	rq_hud_install(ain);

	g_on = 1;
	rq_log("loot: lock-roll wraps=%d gen=%d exist=%d itemG=%d mapG=%d magG=%d",
	       n, g_fno_gen, g_fno_exist, g_gi_item, g_gi_map, g_gi_magnum);
	rq_log("loot: red chests vanilla %d%% / explode (Lua can raise %%). Green untouched.",
	       g_red_base_pct);
	return 1;
}

int rq_chest_install(struct ain *ain)
{
	int f_red, f_chest, scanned;

	if (!ain)
		return 0;
	f_red = find_fn(ain, "赤宝箱処理");
	f_chest = find_fn(ain, "宝箱処理");
	if (f_red < 0 || f_chest < 0) {
		rq_log("chest: missing AIN (red=%d chest=%d)", f_red, f_chest);
		return 0;
	}
	if (g_gi_item < 0)
		g_gi_item = find_global(ain, "Ｉ");
	if (g_gi_map < 0)
		g_gi_map = find_global(ain, "Ｍ");
	if (g_gi_bmap < 0)
		g_gi_bmap = find_global(ain, "◆ＢＭＡＰ");
	if (g_gi_magnum < 0)
		g_gi_magnum = find_global(ain, "◆マグナムの宝物");
	if (g_fno_init < 0)
		g_fno_init = find_fn(ain, "Ｉ初期化");
	if (g_fno_exist < 0)
		g_fno_exist = find_fn(ain, "Ｉ存在");
	scanned = scan_red_pct(ain, f_red);
	if (scanned > 0)
		g_red_base_pct = scanned;
	if (!patch_red_roll(ain, f_red))
		return 0;
	if (!patch_red_dialog(ain, f_red))
		rq_log("chest: disarm dialog still vanilla 30/60/80/100 lines");
	(void)f_chest;
	rq_log("chest: vanilla %d%% / explode; Lua may raise %% (dialog follows attempt %%)",
	       g_red_base_pct);
	return 1;
}

int rq_chest_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_CHEST_PREPARE:
		chest_prepare();
		return 1;
	case VM_XSYS_CHEST_ATTEMPT:
		stack_push(chest_attempt(stack_pop().i));
		return 1;
	default:
		return 0;
	}
}

int rq_lua_chest_on_attempt(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_attempt_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_attempt_ref);
	lua_pushvalue(L, 1);
	g_attempt_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_chest_on_result(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_result_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_result_ref);
	lua_pushvalue(L, 1);
	g_result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_chest_get(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	int ok, v;

	v = kv_get(key, &ok);
	if (!ok)
		lua_pushnil(L);
	else
		lua_pushinteger(L, v);
	return 1;
}

int rq_lua_chest_set(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	int val = (int)luaL_checkinteger(L, 2);

	kv_set(key, val);
	return 0;
}

#endif /* RQ_MODLOADER */
