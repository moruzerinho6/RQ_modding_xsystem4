/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Skill hover: wrap スキル_説明文１ so Lua can append lines (rq.skill).
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
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "vm/heap.h"

static int g_on;
static int g_describe_ref = LUA_NOREF;
static int g_fno_name = -1;
static int g_fno_kinou = -1;
static int g_fno_bairitsu = -1;
static int g_fno_target = -1;
static int g_fno_desc1 = -1;
static int g_fno_koudou = -1;
static int g_fno_kouka = -1;

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

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
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

/* String getter: Ｓ名前(id, "") etc. Returns new UTF-8 (caller frees) or "". */
static char *call_get_utf(int fno, int id)
{
	int empty_slot, ret_slot;
	struct string *sj;
	char *utf;

	if (fno < 0)
		return strdup("");
	empty_slot = vm_string_ref(string_ref(&EMPTY_STRING));
	stack_push(id);
	stack_push(empty_slot);
	vm_call(fno, -1);
	ret_slot = stack_pop().i;
	sj = heap_get_string(ret_slot);
	utf = sjis2utf(sj && sj->text ? sj->text : "", 0);
	heap_unref(ret_slot);
	if (!utf)
		return strdup("");
	return utf;
}

static int wrap_describe(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, VM_XSYS_SKILL_DESCRIBE);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("skill: wrapped スキル_説明文１ (dup=%d)", dup);
	return 1;
}

static void skill_describe(void)
{
	int base_slot = stack_pop().i;
	int skill_no = local_get(0).i;
	struct string *base_sj;
	char *base_utf = NULL;
	char *out_utf = NULL;
	char *out_sjis = NULL;
	struct string *out_s;
	lua_State *L = rq_lua();

	base_sj = heap_get_string(base_slot);
	base_utf = sjis2utf(base_sj && base_sj->text ? base_sj->text : "", 0);
	if (!base_utf)
		base_utf = strdup("");

	out_utf = base_utf;
	base_utf = NULL;

	if (L && g_describe_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_describe_ref);
		lua_pushinteger(L, skill_no);
		lua_pushstring(L, out_utf);
		if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
			rq_log("skill on_describe error: %s", lua_tostring(L, -1));
			lua_pop(L, 1);
		} else if (lua_isstring(L, -1)) {
			const char *ret = lua_tostring(L, -1);
			free(out_utf);
			out_utf = strdup(ret ? ret : "");
			lua_pop(L, 1);
		} else {
			lua_pop(L, 1);
		}
	}

	out_sjis = utf2sjis(out_utf ? out_utf : "", 0);
	free(out_utf);
	heap_unref(base_slot);

	if (!out_sjis) {
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
		return;
	}
	out_s = make_string(out_sjis, strlen(out_sjis));
	free(out_sjis);
	if (!out_s) {
		stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
		return;
	}
	{
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = out_s;
		stack_push(slot);
	}
}

int rq_skill_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_SKILL_DESCRIBE:
		skill_describe();
		return 1;
	default:
		return 0;
	}
}

int rq_skill_install(struct ain *ain)
{
	int f_desc;

	if (g_on)
		return 1;
	if (!ain)
		return 0;

	f_desc = find_fn(ain, "スキル_説明文１");
	g_fno_name = find_fn(ain, "Ｓ名前");
	g_fno_kinou = find_fn(ain, "Ｓ機能");
	g_fno_bairitsu = find_fn(ain, "Ｓ倍率");
	g_fno_target = find_fn(ain, "Ｓ対象");
	g_fno_desc1 = find_fn(ain, "Ｓ説明文１");
	g_fno_koudou = find_fn(ain, "Ｓ行動種類");
	g_fno_kouka = find_fn(ain, "Ｓ効果");

	if (f_desc < 0) {
		rq_log("skill: スキル_説明文１ not found");
		return 0;
	}
	if (!wrap_describe(ain, f_desc))
		return 0;

	g_on = 1;
	rq_log("skill: describe wrap on; getters name=%d kinou=%d bairitsu=%d target=%d desc1=%d koudou=%d kouka=%d",
	       g_fno_name, g_fno_kinou, g_fno_bairitsu, g_fno_target, g_fno_desc1,
	       g_fno_koudou, g_fno_kouka);
	return 1;
}

int rq_lua_skill_on_describe(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_describe_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_describe_ref);
	lua_pushvalue(L, 1);
	g_describe_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_skill_get(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	char *name, *desc1;

	lua_newtable(L);
	lua_pushinteger(L, id);
	lua_setfield(L, -2, "id");

	name = call_get_utf(g_fno_name, id);
	lua_pushstring(L, name);
	lua_setfield(L, -2, "name");
	free(name);

	lua_pushinteger(L, call_ii(g_fno_kinou, id, -1));
	lua_setfield(L, -2, "kinou");

	lua_pushinteger(L, call_ii(g_fno_bairitsu, id, -1));
	lua_setfield(L, -2, "bairitsu");

	lua_pushinteger(L, call_ii(g_fno_target, id, -1));
	lua_setfield(L, -2, "target");

	/* 行動種類: 1 physical, 2 magic, 3 special, 4 support, 0 ability */
	lua_pushinteger(L, call_ii(g_fno_koudou, id, -1));
	lua_setfield(L, -2, "koudou");

	lua_pushinteger(L, call_ii(g_fno_kouka, id, -1));
	lua_setfield(L, -2, "kouka");

	desc1 = call_get_utf(g_fno_desc1, id);
	lua_pushstring(L, desc1);
	lua_setfield(L, -2, "desc1");
	free(desc1);

	return 1;
}

#endif /* RQ_MODLOADER */
