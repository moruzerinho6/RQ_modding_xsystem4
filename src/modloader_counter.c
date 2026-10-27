/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Counter after Ally Guard: wrap only the FA[77] Counter-rate zero-out inside
 * ダメージ後計算. Other FA[77] effects (hit connect, etc.) stay vanilla.
 * Lua owns policy via rq.counter.on_guard_counter.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

/* ダメージ後計算 locals: ARG iic1,iic2,iis; VAR aa */
#define CTR_LOC_IIC1 0
#define CTR_LOC_IIC2 1
#define CTR_LOC_IIS  2
#define CTR_LOC_AA   3

/* Size of vanilla FA[77]==1 → aa=0 block (SH_GLOBALREF … JUMP). */
#define CTR_FA77_BLOCK 54

static int g_on;
static int g_hook_ref = LUA_NOREF;
static int g_gi_fa = -1;

static uint16_t ctr_rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t ctr_rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void ctr_wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void ctr_wr32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
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

static uint32_t func_end(struct ain *ain, int fno)
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

static int read_fa77(void)
{
	struct page *arr;
	int slot;

	if (g_gi_fa < 0)
		return 0;
	slot = global_get(g_gi_fa).i;
	if (!heap_index_valid(slot))
		return 0;
	arr = heap_get_page(slot);
	if (!arr || 77 >= arr->nr_vars)
		return 0;
	return page_get_var(arr, 77).i;
}

static void zero_aa(void)
{
	struct page *lp = local_page();
	if (lp)
		page_set_var(lp, CTR_LOC_AA, 0);
}

/*
 * Ask Lua whether to allow Counter despite FA[77].
 * true  → skip zero-out (allow rate roll)
 * false / no hook / error → vanilla zero-out
 */
static int lua_allow_guard_counter(int guarded, int rate)
{
	lua_State *L = rq_lua();
	int allow = 0;

	if (!L || g_hook_ref == LUA_NOREF)
		return 0;

	lua_rawgeti(L, LUA_REGISTRYINDEX, g_hook_ref);
	lua_newtable(L);
	lua_pushboolean(L, guarded != 0);
	lua_setfield(L, -2, "guarded");
	lua_pushinteger(L, local_get(CTR_LOC_IIC2).i);
	lua_setfield(L, -2, "defender");
	lua_pushinteger(L, local_get(CTR_LOC_IIC1).i);
	lua_setfield(L, -2, "attacker");
	lua_pushinteger(L, local_get(CTR_LOC_IIS).i);
	lua_setfield(L, -2, "skill");
	lua_pushinteger(L, rate);
	lua_setfield(L, -2, "rate");

	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		rq_log("counter on_guard_counter error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	allow = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return allow;
}

static void counter_guard_check(void)
{
	int fa77 = read_fa77();
	int rate;

	if (fa77 != 1)
		return;

	rate = local_get(CTR_LOC_AA).i;
	if (lua_allow_guard_counter(1, rate))
		return;
	zero_aa();
}

/*
 * Unique Counter FA[77] block inside ダメージ後計算 (after skill 259 rate):
 *   SH_GLOBALREF FA; PUSH 77; REF; PUSH 1; EQUALE; IFZ end;
 *   PUSHLOCALPAGE; PUSH 3; PUSH 0; ITOLI; LI_ASSIGN; POP; JUMP end;
 * 命中計算 uses SH_LOCALASSIGN aa 100 instead — not matched.
 */
static int patch_fa77_counter(struct ain *ain, int dmg_fno, int fa_gi)
{
	uint32_t start, end, ip;
	int cs_fno;

	if (!ain || dmg_fno < 0 || fa_gi < 0)
		return 0;
	cs_fno = find_fn(ain, "ＣＳ有無／ランク別効果");
	if (cs_fno < 0) {
		rq_log("counter: ＣＳ有無／ランク別効果 not found");
		return 0;
	}

	start = ain->functions[dmg_fno].address;
	end = func_end(ain, dmg_fno);

	for (ip = start; ip + CTR_FA77_BLOCK <= end; ) {
		uint16_t op = ctr_rd16(ain->code + ip);
		int len = instruction_width((enum opcode)op);
		uint32_t after259;

		if (len < 2)
			len = 2;

		/* Anchor: PUSH 259; … CALLFUNC ＣＳ有無／ランク別効果 nearby */
		if (!(op == PUSH && ctr_rd32(ain->code + ip + 2) == 259)) {
			ip += (uint32_t)len;
			continue;
		}
		after259 = ip;
		/* scan forward a short window for CALLFUNC cs */
		{
			uint32_t j;
			int saw_cs = 0;
			for (j = after259; j + 6 <= end && j < after259 + 40; ) {
				uint16_t jop = ctr_rd16(ain->code + j);
				int jlen = instruction_width((enum opcode)jop);
				if (jlen < 2)
					jlen = 2;
				if (jop == CALLFUNC && ctr_rd32(ain->code + j + 2) == cs_fno) {
					saw_cs = 1;
					break;
				}
				j += (uint32_t)jlen;
			}
			if (!saw_cs) {
				ip += (uint32_t)len;
				continue;
			}
		}

		/* From after259, find FA[77]==1 → aa=0 block */
		{
			uint32_t k;
			for (k = after259; k + CTR_FA77_BLOCK <= end && k < after259 + 400; ) {
				uint16_t kop = ctr_rd16(ain->code + k);
				int klen = instruction_width((enum opcode)kop);
				uint32_t ifz_tgt, jump_tgt;

				if (klen < 2)
					klen = 2;
				if (kop == SH_GLOBALREF && ctr_rd32(ain->code + k + 2) == fa_gi
				    && ctr_rd16(ain->code + k + 6) == PUSH
				    && ctr_rd32(ain->code + k + 8) == 77
				    && ctr_rd16(ain->code + k + 12) == REF
				    && ctr_rd16(ain->code + k + 14) == PUSH
				    && ctr_rd32(ain->code + k + 16) == 1
				    && ctr_rd16(ain->code + k + 20) == EQUALE
				    && ctr_rd16(ain->code + k + 22) == IFZ
				    && ctr_rd16(ain->code + k + 28) == PUSHLOCALPAGE
				    && ctr_rd16(ain->code + k + 30) == PUSH
				    && ctr_rd32(ain->code + k + 32) == CTR_LOC_AA
				    && ctr_rd16(ain->code + k + 36) == PUSH
				    && ctr_rd32(ain->code + k + 38) == 0
				    && ctr_rd16(ain->code + k + 42) == ITOLI
				    && ctr_rd16(ain->code + k + 44) == LI_ASSIGN
				    && ctr_rd16(ain->code + k + 46) == POP
				    && ctr_rd16(ain->code + k + 48) == JUMP) {
					ifz_tgt = (uint32_t)ctr_rd32(ain->code + k + 24);
					jump_tgt = (uint32_t)ctr_rd32(ain->code + k + 50);
					if (ifz_tgt != jump_tgt || ifz_tgt != k + CTR_FA77_BLOCK) {
						k += (uint32_t)klen;
						continue;
					}
					/* CALLSYS; JUMP past dead middle */
					ctr_wr16(ain->code + k, CALLSYS);
					ctr_wr32(ain->code + k + 2, VM_XSYS_COUNTER_GUARD);
					ctr_wr16(ain->code + k + 6, JUMP);
					ctr_wr32(ain->code + k + 8, (int32_t)ifz_tgt);
					rq_log("counter: patched FA[77] Counter zero-out in ダメージ後計算 @ 0x%x",
					       k);
					return 1;
				}
				k += (uint32_t)klen;
			}
		}
		ip += (uint32_t)len;
	}
	rq_log("counter: FA[77] Counter site not found in ダメージ後計算");
	return 0;
}

int rq_counter_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_COUNTER_GUARD:
		counter_guard_check();
		return 1;
	default:
		return 0;
	}
}

int rq_counter_install(struct ain *ain)
{
	int f_dmg, fa_gi;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_dmg = find_fn(ain, "ダメージ後計算");
	fa_gi = find_global(ain, "FA");
	g_gi_fa = fa_gi;

	if (f_dmg < 0 || fa_gi < 0) {
		rq_log("counter: missing AIN (ダメージ後計算=%d FA=%d)", f_dmg, fa_gi);
		return 0;
	}
	if (!patch_fa77_counter(ain, f_dmg, fa_gi))
		return 0;

	g_on = 1;
	rq_log("counter: FA[77] Counter gate hooked (Lua on_guard_counter; other FA[77] vanilla)");
	return 1;
}

int rq_lua_counter_on_guard_counter(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_hook_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_hook_ref);
	lua_pushvalue(L, 1);
	g_hook_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

#endif /* RQ_MODLOADER */
