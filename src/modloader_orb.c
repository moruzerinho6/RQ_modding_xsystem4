/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Recovery Spring (回復の泉): replace PTA-only heal with full roster heal.
 * Lua: rq.orb.install(). Carolie / flash / ＭＤ消し stay vanilla.
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
static int g_fno_state = -1;
static int g_fno_status = -1;
static int g_fno_hp = -1;
static int g_fno_hpmax = -1;
static int g_fno_supply = -1;

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

/* Ｃ補給／体力(iic, "") — void. */
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

static void orb_heal(void)
{
	int iic, state, maxhp, hp;
	int n_full = 0, n_revive = 0;

	for (iic = ORB_IIC_LO; iic <= ORB_IIC_HI; iic++) {
		state = call_ii(g_fno_state, iic, -1);
		if (state == 1 || state == 2) {
			call_supply_full(iic);
			n_full++;
		} else if (state == 3) {
			call_ii(g_fno_state, iic, 2);
			call_ii(g_fno_status, iic, 0);
			maxhp = call_ii(g_fno_hpmax, iic, -1);
			hp = maxhp * 3 / 10;
			if (hp < 1)
				hp = 1;
			call_ii(g_fno_hp, iic, hp);
			n_revive++;
		}
	}
	if (g_rq_verbose || g_rq_terminal)
		rq_log("orb: spring heal full=%d revive30%%=%d (roster %d..%d)",
		       n_full, n_revive, ORB_IIC_LO, ORB_IIC_HI);
}

/*
 * In 回復の泉処理, replace CALLFUNC ＰＴＡ体力回復 with CALLSYS.
 * The empty-string arg stays on the stack; syscall pops it.
 */
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
			rq_log("orb: patched 回復の泉処理 CALLFUNC ＰＴＡ体力回復 -> CALLSYS");
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
		orb_heal();
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
	rq_log("orb: recovery spring heals 登録状態 1+2 full; 3->控え @ 30%% HP");
	return 1;
}

#endif /* RQ_MODLOADER */
