/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 * Copyright (C) 2026 RQ_modding
 *
 * CommonSystemData.sav — vanilla "CSD\0" + zlib payload.
 */

#include <stdint.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "system4/zlib.h"

#include "hll.h"

enum csd_type {
	CSD_INT = 1,
	CSD_FLOAT = 2,
	CSD_STRING = 3,
	CSD_BOOL = 4,
};

struct csd_entry {
	enum csd_type type;
	union {
		int i;
		float f;
		struct string *s;
	};
};

static struct string *save_path = NULL;
static bool suppress_save;

static struct hash_table *csd_table(void)
{
	static struct hash_table *table = NULL;
	if (!table)
		table = ht_create(64);
	return table;
}

static void free_entry(struct csd_entry *e)
{
	if (!e)
		return;
	if (e->type == CSD_STRING && e->s)
		free_string(e->s);
	free(e);
}

static char *save_path_utf8(void)
{
	if (!save_path)
		return NULL;
	return sjis2utf(save_path->text, 0);
}

static void csd_save(void);

static struct csd_entry *csd_put(struct string *name, enum csd_type type)
{
	struct ht_slot *slot = ht_put(csd_table(), name->text, NULL);
	if (slot->value)
		free_entry(slot->value);
	struct csd_entry *e = xcalloc(1, sizeof(*e));
	e->type = type;
	slot->value = e;
	return e;
}

static struct csd_entry *csd_get(struct string *name)
{
	return ht_get(csd_table(), name->text, NULL);
}

static void save_one(struct ht_slot *slot, void *data)
{
	struct buffer {
		uint8_t *p;
		size_t len;
		size_t cap;
	} *b = data;
	struct csd_entry *e = slot->value;
	if (!e)
		return;
	size_t klen = strlen(slot->key) + 1;
	size_t extra = klen + 8 + (e->type == CSD_STRING && e->s ? (size_t)e->s->size + 5 : 4);
	if (b->len + extra + 16 > b->cap) {
		b->cap = (b->cap ? b->cap : 256) * 2 + extra;
		b->p = xrealloc(b->p, b->cap);
	}
	memcpy(b->p + b->len, slot->key, klen);
	b->len += klen;
	LittleEndian_putDW(b->p, (int)b->len, e->type);
	b->len += 4;
	switch (e->type) {
	case CSD_INT:
	case CSD_BOOL:
		LittleEndian_putDW(b->p, (int)b->len, (uint32_t)e->i);
		b->len += 4;
		break;
	case CSD_FLOAT: {
		uint32_t u;
		memcpy(&u, &e->f, 4);
		LittleEndian_putDW(b->p, (int)b->len, u);
		b->len += 4;
		break;
	}
	case CSD_STRING: {
		uint32_t n = e->s ? (uint32_t)e->s->size + 1 : 1;
		LittleEndian_putDW(b->p, (int)b->len, n);
		b->len += 4;
		if (e->s && e->s->size)
			memcpy(b->p + b->len, e->s->text, e->s->size);
		b->p[b->len + n - 1] = 0;
		b->len += n;
		break;
	}
	}
}

static uint32_t count_entries;

static void count_one(struct ht_slot *slot, void *data)
{
	if (slot->value)
		count_entries++;
}

static void csd_save(void)
{
	if (suppress_save || !save_path)
		return;
	char *path = save_path_utf8();
	if (!path)
		return;

	count_entries = 0;
	ht_foreach(csd_table(), count_one, NULL);

	struct {
		uint8_t *p;
		size_t len;
		size_t cap;
	} body = {0};
	body.cap = 256;
	body.p = xmalloc(body.cap);
	LittleEndian_putDW(body.p, 0, count_entries);
	body.len = 4;
	ht_foreach(csd_table(), save_one, &body);

	size_t bound = zlib_compress_bound(body.len);
	uint8_t *zbuf = xmalloc(bound);
	size_t zlen = zlib_compress(zbuf, bound, body.p, body.len, ZLIB_DEFAULT_COMPRESSION);
	if (!zlen) {
		WARNING("CommonSystemData: compress failed");
		free(zbuf);
		free(body.p);
		free(path);
		return;
	}

	uint8_t hdr[16];
	memset(hdr, 0, sizeof hdr);
	memcpy(hdr, "CSD", 3);
	LittleEndian_putDW(hdr, 8, (uint32_t)body.len);
	LittleEndian_putDW(hdr, 12, (uint32_t)zlen);

	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("CommonSystemData: cannot write %s", path);
	} else {
		fwrite(hdr, 1, 16, fp);
		fwrite(zbuf, 1, zlen, fp);
		fclose(fp);
	}
	free(zbuf);
	free(body.p);
	free(path);
}

static bool csd_load(void)
{
	char *path = save_path_utf8();
	if (!path)
		return false;
	size_t len = 0;
	uint8_t *buf = file_read(path, &len);
	free(path);
	if (!buf)
		return true; /* first run: no file yet */
	if (len < 16 || memcmp(buf, "CSD", 3) != 0) {
		WARNING("CommonSystemData: bad magic");
		free(buf);
		return false;
	}
	uint32_t unc = (uint32_t)LittleEndian_getDW(buf, 8);
	uint32_t zlen = (uint32_t)LittleEndian_getDW(buf, 12);
	if (16 + zlen > len || unc == 0 || unc > 16 * 1024 * 1024) {
		WARNING("CommonSystemData: bad sizes");
		free(buf);
		return false;
	}
	uint8_t *plain = xmalloc(unc);
	if (!zlib_decompress_exact(plain, unc, buf + 16, zlen)) {
		WARNING("CommonSystemData: decompress failed");
		free(plain);
		free(buf);
		return false;
	}
	free(buf);

	if (unc < 4) {
		free(plain);
		return false;
	}
	uint32_t n = (uint32_t)LittleEndian_getDW(plain, 0);
	size_t off = 4;
	suppress_save = true;
	for (uint32_t i = 0; i < n && off < unc; i++) {
		const char *key = (const char *)(plain + off);
		size_t klen = 0;
		while (off + klen < unc && plain[off + klen])
			klen++;
		if (off + klen + 1 + 4 > unc)
			break;
		off += klen + 1;
		uint32_t type = (uint32_t)LittleEndian_getDW(plain, (int)off);
		off += 4;
		struct string *name = make_string(key, strlen(key));
		struct csd_entry *e = csd_put(name, (enum csd_type)type);
		switch (type) {
		case CSD_INT:
		case CSD_BOOL:
			if (off + 4 > unc)
				goto done;
			e->i = LittleEndian_getDW(plain, (int)off);
			off += 4;
			break;
		case CSD_FLOAT:
			if (off + 4 > unc)
				goto done;
			{
				uint32_t u = (uint32_t)LittleEndian_getDW(plain, (int)off);
				memcpy(&e->f, &u, 4);
			}
			off += 4;
			break;
		case CSD_STRING: {
			if (off + 4 > unc)
				goto done;
			uint32_t sl = (uint32_t)LittleEndian_getDW(plain, (int)off);
			off += 4;
			if (off + sl > unc)
				goto done;
			e->s = make_string((const char *)(plain + off), sl ? sl - 1 : 0);
			off += sl;
			break;
		}
		default:
			goto done;
		}
		free_string(name);
		continue;
done:
		free_string(name);
		break;
	}
	suppress_save = false;
	free(plain);
	return true;
}

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	if (save_path)
		free_string(save_path);
	save_path = string_dup(filename);
}

static bool CommonSystemData_LoadAtStartup(void)
{
	return csd_load();
}

static bool CommonSystemData_SetDataInt(struct string *name, int value)
{
	csd_put(name, CSD_INT)->i = value;
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataFloat(struct string *name, float value)
{
	csd_put(name, CSD_FLOAT)->f = value;
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataString(struct string *name, struct string *value)
{
	csd_put(name, CSD_STRING)->s = string_dup(value);
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataBool(struct string *name, bool value)
{
	csd_put(name, CSD_BOOL)->i = value;
	csd_save();
	return true;
}

static bool CommonSystemData_GetDataInt(struct string *name, int *value)
{
	struct csd_entry *e = csd_get(name);
	if (!e || e->type != CSD_INT)
		return false;
	*value = e->i;
	return true;
}

static bool CommonSystemData_GetDataFloat(struct string *name, float *value)
{
	struct csd_entry *e = csd_get(name);
	if (!e || e->type != CSD_FLOAT)
		return false;
	*value = e->f;
	return true;
}

static bool CommonSystemData_GetDataString(struct string *name, struct string **value)
{
	struct csd_entry *e = csd_get(name);
	if (!e || e->type != CSD_STRING || !e->s)
		return false;
	if (*value)
		free_string(*value);
	*value = string_dup(e->s);
	return true;
}

static bool CommonSystemData_GetDataBool(struct string *name, bool *value)
{
	struct csd_entry *e = csd_get(name);
	if (!e || (e->type != CSD_BOOL && e->type != CSD_INT))
		return false;
	*value = !!e->i;
	return true;
}

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
