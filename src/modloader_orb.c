/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Recovery Spring (回復の泉): CALLSYS replaces ＰＴＡ体力回復; Lua owns heal
 * policy via rq.orb.on_heal + roster/HP primitives. Carolie / flash / ＭＤ消し
 * stay in vanilla 回復の泉処理 after the hook returns.
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
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"

#define ORB_IIC_LO 20
#define ORB_IIC_HI 200

static int g_on;
static int g_heal_ref = LUA_NOREF;
static int g_fno_state = -1;
static int g_fno_status = -1;
static int g_fno_hp = -1;
static int g_fno_hpmax = -1;
static int g_fno_supply = -1;
static int g_fno_pta_heal = -1;

static uint16_t orb_rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t orb_rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void orb_wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void orb_wr32(uint8_t *p, int32_t v)
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

static void call_supply_full(int iic)
{
	int slot;
	if (g_fno_supply < 0)
		return;
	slot = vm_string_ref(string_ref(&EMPTY_STRING));
	stack_push(iic);
	stack_push(slot);
	vm_call(g_fno_supply, -1);
}

/* Vanilla PTA-only heal if Lua did not register on_heal. */
static void orb_vanilla_pta_heal(void)
{
	int slot;
	if (g_fno_pta_heal < 0)
		return;
	slot = vm_string_ref(string_ref(&EMPTY_STRING));
	stack_push(slot);
	vm_call(g_fno_pta_heal, -1);
}

static void orb_lua_heal(void)
{
	lua_State *L = rq_lua();

	if (!L || g_heal_ref == LUA_NOREF) {
		orb_vanilla_pta_heal();
		return;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_heal_ref);
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		rq_log("orb on_heal error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		orb_vanilla_pta_heal();
	}
}

static int patch_spring_heal(struct ain *ain, int spring_fno, int pta_heal_fno)
{
	uint32_t start, end, ip;
	int i;

	if (!ain || spring_fno < 0 || pta_heal_fno < 0)
		return 0;
	start = ain->functions[spring_fno].address;
	end = (uint32_t)ain->code_size;
	for (i = 0; i < ain->nr_functions; i++) {
		uint32_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	for (ip = start; ip + 6 <= end; ) {
		uint16_t op = orb_rd16(ain->code + ip);
		int len = instruction_width((enum opcode)op);
		if (len < 2)
			len = 2;
		if (op == CALLFUNC && orb_rd32(ain->code + ip + 2) == pta_heal_fno) {
			orb_wr16(ain->code + ip, CALLSYS);
			orb_wr32(ain->code + ip + 2, VM_XSYS_ORB_HEAL);
			rq_log("orb: patched 回復の泉処理 CALLFUNC ＰＴＡ体力回復 -> CALLSYS (Lua on_heal)");
			return 1;
		}
		ip += (uint32_t)len;
	}
	rq_log("orb: CALLFUNC ＰＴＡ体力回復 not found in 回復の泉処理");
	return 0;
}

int rq_orb_syscall(int code)
{
	int str;

	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_ORB_HEAL:
		str = stack_pop().i;
		heap_unref(str);
		orb_lua_heal();
		return 1;
	default:
		return 0;
	}
}

int rq_orb_install(struct ain *ain)
{
	int f_spring, f_pta;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_spring = find_fn(ain, "回復の泉処理");
	f_pta = find_fn(ain, "ＰＴＡ体力回復");
	g_fno_pta_heal = f_pta;
	g_fno_state = find_fn(ain, "Ｃ登録状態");
	g_fno_status = find_fn(ain, "Ｃ状態変化");
	g_fno_hp = find_fn(ain, "Ｃ体力");
	g_fno_hpmax = find_fn(ain, "Ｃ体力最大");
	g_fno_supply = find_fn(ain, "Ｃ補給／体力");

	if (f_spring < 0 || f_pta < 0 || g_fno_state < 0 || g_fno_hp < 0
	    || g_fno_hpmax < 0 || g_fno_supply < 0) {
		rq_log("orb: missing AIN (spring=%d pta=%d state=%d hp=%d max=%d supply=%d status=%d)",
		       f_spring, f_pta, g_fno_state, g_fno_hp, g_fno_hpmax,
		       g_fno_supply, g_fno_status);
		return 0;
	}
	if (!patch_spring_heal(ain, f_spring, f_pta))
		return 0;

	g_on = 1;
	rq_log("orb: spring hook ready (Lua on_heal; Carolie/flash/tile vanilla)");
	return 1;
}

int rq_lua_orb_on_heal(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_heal_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_heal_ref);
	lua_pushvalue(L, 1);
	g_heal_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_orb_roster(lua_State *L)
{
	int iic, n = 0, state_filter = -1;

	if (lua_isnumber(L, 1))
		state_filter = (int)lua_tointeger(L, 1);

	lua_newtable(L);
	for (iic = ORB_IIC_LO; iic <= ORB_IIC_HI; iic++) {
		int state = call_ii(g_fno_state, iic, -1);
		int hp, maxhp;
		if (state == 0)
			continue;
		if (state_filter >= 0 && state != state_filter)
			continue;
		hp = call_ii(g_fno_hp, iic, -1);
		maxhp = call_ii(g_fno_hpmax, iic, -1);
		n++;
		lua_newtable(L);
		lua_pushinteger(L, iic);
		lua_setfield(L, -2, "iic");
		lua_pushinteger(L, state);
		lua_setfield(L, -2, "state");
		lua_pushinteger(L, hp);
		lua_setfield(L, -2, "hp");
		lua_pushinteger(L, maxhp);
		lua_setfield(L, -2, "max_hp");
		lua_rawseti(L, -2, n);
	}
	return 1;
}

int rq_lua_orb_get_state(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, call_ii(g_fno_state, iic, -1));
	return 1;
}

int rq_lua_orb_set_state(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int state = (int)luaL_checkinteger(L, 2);
	call_ii(g_fno_state, iic, state);
	return 0;
}

int rq_lua_orb_set_status(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int status = (int)luaL_checkinteger(L, 2);
	call_ii(g_fno_status, iic, status);
	return 0;
}

int rq_lua_orb_get_hp(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, call_ii(g_fno_hp, iic, -1));
	return 1;
}

int rq_lua_orb_get_max_hp(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, call_ii(g_fno_hpmax, iic, -1));
	return 1;
}

int rq_lua_orb_set_hp(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	int hp = (int)luaL_checkinteger(L, 2);
	call_ii(g_fno_hp, iic, hp);
	return 0;
}

int rq_lua_orb_full_heal(lua_State *L)
{
	int iic = (int)luaL_checkinteger(L, 1);
	call_supply_full(iic);
	return 0;
}

#endif /* RQ_MODLOADER */
