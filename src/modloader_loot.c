/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Loot-lock + preview: roll chests once at place, skip the open roll,
 * no red-chest explosions, dungeon-store preview of Magnum + locked chests.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

/* sM member slots */
#define SM_MAPTT 1
#define SM_MAPTZ 2

static struct ain *g_ain;
static int g_on;
static int g_fno_gen = -1;
static int g_fno_exist = -1;
static int g_fno_init = -1;
static int g_fno_field = -1;
static int g_fno_field_mag = -1;
static int g_fno_iname = -1;
static int g_gi_item = -1;
static int g_gi_map = -1;
static int g_gi_bmap = -1;
static int g_gi_magnum = -1;

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

static int wrap_red(struct ain *ain, int red_fno, int chest_fno)
{
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, red_fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, SH_LOCALREF, 1);
	emit1(&out, CALLFUNC, chest_fno);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, red_fno);
	commit_wrap(ain, red_fno, &out);
	return 1;
}

static int wrap_store(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;
	uint32_t j_skip;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, PUSH, 0);
	emit0(&out, EQUALE);
	j_skip = emit_ifz(&out);
	emit1(&out, CALLSYS, VM_XSYS_LOOT_PREVIEW);
	patch_j(&out, j_skip, ain);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
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

static void item_set(int ii, int member, int v)
{
	struct page *p = item_page(ii);
	if (!p || member < 0 || member >= p->nr_vars)
		return;
	p->values[member].i = v;
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

static void set_name(int ii, const char *utf8)
{
	int ret, slot;
	char *sjis;
	struct string *s;
	if (g_fno_iname < 0)
		return;
	sjis = utf2sjis(utf8, 0);
	if (!sjis)
		return;
	s = make_string(sjis, strlen(sjis));
	free(sjis);
	slot = heap_alloc_string(s);
	stack_push(ii);
	stack_push(slot);
	vm_call(g_fno_iname, -1);
	ret = stack_pop().i;
	if (heap_index_valid(ret))
		heap_unref(ret);
}

static void copy_name(int dst, int src)
{
	struct page *p = item_page(src);
	struct string *s;
	int slot, ret;
	if (!p || g_fno_iname < 0)
		return;
	slot = p->values[SI_NAME].i;
	if (!string_index_valid(slot))
		return;
	s = heap_get_string(slot);
	if (!s)
		return;
	stack_push(dst);
	stack_push(vm_string_ref(s));
	vm_call(g_fno_iname, -1);
	ret = stack_pop().i;
	if (heap_index_valid(ret))
		heap_unref(ret);
}

static void prepare_preview(int ii, int *save_unid, int *save_cg, int *save_name_slot)
{
	int catalog, twin, seen;
	struct page *p = item_page(ii);

	*save_unid = item_get(ii, SI_UNID);
	*save_cg = item_get(ii, SI_CG);
	*save_name_slot = 0;
	if (p) {
		*save_name_slot = p->values[SI_NAME].i;
		if (heap_index_valid(*save_name_slot))
			heap_ref(*save_name_slot);
	}
	catalog = item_get(ii, SI_CATALOG);
	twin = catalog_seen(catalog, ii);
	seen = twin != 0;
	if (seen) {
		item_set(ii, SI_UNID, 0);
		if (twin != ii) {
			item_set(ii, SI_CG, item_get(twin, SI_CG));
			copy_name(ii, twin);
		}
	} else {
		item_set(ii, SI_UNID, 1);
		item_set(ii, SI_CG, unid_cg(ii));
		set_name(ii, UNID_NAME);
	}
}

static void restore_preview(int ii, int save_unid, int save_cg, int save_name_slot)
{
	int ret;
	item_set(ii, SI_UNID, save_unid);
	item_set(ii, SI_CG, save_cg);
	if (g_fno_iname >= 0 && heap_index_valid(save_name_slot) && string_index_valid(save_name_slot)) {
		stack_push(ii);
		stack_push(save_name_slot);
		heap_ref(save_name_slot);
		vm_call(g_fno_iname, -1);
		ret = stack_pop().i;
		if (heap_index_valid(ret))
			heap_unref(ret);
	}
	if (heap_index_valid(save_name_slot))
		heap_unref(save_name_slot);
}

static void show_card(int ii, int magnum)
{
	int su, sc, sn;
	if (ii <= 0)
		return;
	prepare_preview(ii, &su, &sc, &sn);
	if (magnum && g_fno_field_mag >= 0)
		call_i(g_fno_field_mag, ii);
	else
		call_i(g_fno_field, ii);
	restore_preview(ii, su, sc, sn);
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

static void loot_preview(void)
{
	int mag, x, y, b, saved_bmap;
	int *tt, *tz;
	struct page *marr;

	if (!g_on)
		return;
	mag = magnum_id();
	if (mag > 0 && item_get(mag, SI_EXIST) == 9)
		show_card(mag, 1);

	if (g_gi_bmap < 0 || g_gi_map < 0)
		return;
	saved_bmap = global_get(g_gi_bmap).i;
	marr = heap_get_page(global_get(g_gi_map).i);
	if (!marr)
		return;
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
				show_card(ii, 0);
			}
		}
	}
	global_set(g_gi_bmap, vm_int(saved_bmap), false);
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
	case VM_XSYS_LOOT_PREVIEW:
		loot_preview();
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
	int f_kotei, f_rand, f_gen, f_red, f_chest, f_store, f_end;
	int n = 0;

	if (g_on)
		return 1;
	if (!ain)
		return 0;
	g_ain = ain;

	f_kotei = find_fn(ain, "ＭＤ固定");
	f_rand = find_fn(ain, "ＭＤランダム");
	f_gen = find_fn(ain, "宝箱の中身生成");
	f_red = find_fn(ain, "赤宝箱処理");
	f_chest = find_fn(ain, "宝箱処理");
	f_store = find_fn(ain, "Ｒ８クエスト倉庫");
	f_end = find_fn(ain, "クエスト終了処理");
	g_fno_exist = find_fn(ain, "Ｉ存在");
	g_fno_init = find_fn(ain, "Ｉ初期化");
	g_fno_field = find_fn(ain, "Ｒ８どこかからアイテム");
	g_fno_field_mag = find_fn(ain, "Ｒ８どこかからアイテム／マグナム");
	g_fno_iname = find_fn(ain, "Ｉ名前");
	g_fno_gen = f_gen;
	g_gi_item = find_global(ain, "Ｉ");
	g_gi_map = find_global(ain, "Ｍ");
	g_gi_bmap = find_global(ain, "◆ＢＭＡＰ");
	g_gi_magnum = find_global(ain, "◆マグナムの宝物");

	if (f_kotei < 0 || f_rand < 0 || f_gen < 0 || f_red < 0 || f_chest < 0 || f_store < 0) {
		rq_log("loot-preview: missing AIN function (kotei=%d rand=%d gen=%d red=%d chest=%d store=%d)",
		       f_kotei, f_rand, f_gen, f_red, f_chest, f_store);
		return 0;
	}
	n += wrap_md_kotei(ain, f_kotei);
	n += wrap_md_rand(ain, f_rand);
	n += wrap_gen(ain, f_gen);
	n += wrap_red(ain, f_red, f_chest);
	n += wrap_store(ain, f_store);
	if (f_end >= 0)
		n += wrap_quest_end(ain, f_end);

	g_on = 1;
	rq_log("loot-preview: installed wraps=%d gen=%d exist=%d field=%d mag=%d itemG=%d mapG=%d magG=%d",
	       n, g_fno_gen, g_fno_exist, g_fno_field, g_fno_field_mag,
	       g_gi_item, g_gi_map, g_gi_magnum);
	return 1;
}

#endif /* RQ_MODLOADER */
