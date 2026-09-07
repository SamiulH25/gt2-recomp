// GT2 native port: GT2.VOL reader. Written from the observed on-disc
// format (see gt2/vol.h); game disassembly used only to confirm semantics:
//   - SCUS 0x8005D74C: file offset = tbl[idx] (u32 at header+0x10+idx*4),
//     sector = offset >> 11. So tbl[] holds BYTE offsets. Confirmed.
//   - SCUS 0x800102DC: init reads 0x8C000 bytes from VOL start, copies the
//     header aside, then indexes names. Our port keeps the tables in the
//     handle instead of fixed RAM addresses.
//   - Hash-dir record layout (32 B) confirmed by parsing 11568 records and
//     extracting byte-exact files.

#include "gt2/vol.h"
#include "gt2/cd.h"

#include <stdlib.h>
#include <string.h>

#define GT2_VOL_MAGIC 0x53465447u   // "GTFS" LE
#define GT2_VOL_DIR_OFF 0xBE7Cu
#define GT2_VOL_DIR_REC 32u
#define GT2_VOL_DIR_CAP 0x20000u    // hard stop; real discs have ~11.6k
#define GT2_VOL_FLAT_NAME_MAX 21    // flat-dir bytes reserved (see Q9)

typedef struct {
    char name[GT2_VOL_FLAT_NAME_MAX + 1];
    u32 index;  // tbl_idx
    u32 hash;   // mastering date (not a hash; see docs/gtfs_notes.md Q5)
} gt2_vol_name_t;

typedef struct {
    u32 date;
    u16 next;
    u8 flags;
    char name[GT2_VOL_NAME_MAX + 1];
} gt2_vol_slot_t;

struct gt2_vol {
    gt2_cd_t *cd;       // raw 2352 dump or cooked ISO (auto-detected)
    u16 data_count;
    u16 entry_count;
    u32 *tbl;           // data_count+1 offsets from VOL base
    gt2_vol_name_t *names;
    u32 name_count;
    gt2_vol_slot_t *slots;  // entry_count slots from VOL+0xB800
    u32 slot_count;
};

const char *gt2_vol_strerror(gt2_vol_status_t st) {
    switch (st) {
    case GT2_VOL_OK: return "ok";
    case GT2_VOL_ERR_IO: return "i/o error";
    case GT2_VOL_ERR_BAD_MAGIC: return "bad GTFS magic";
    case GT2_VOL_ERR_TRUNCATED: return "truncated image";
    case GT2_VOL_ERR_NO_MEM: return "out of memory";
    case GT2_VOL_ERR_NOT_FOUND: return "not found";
    case GT2_VOL_ERR_INVAL: return "invalid argument";
    case GT2_VOL_ERR_IS_DIR: return "is a directory";
    default: return "unknown";
    }
}

static int read_at(gt2_cd_t *cd, u32 off, void *buf, u32 len) {
    return gt2_cd_pread(cd, off, buf, len) == GT2_CD_OK ? 0 : -1;
}

gt2_vol_status_t gt2_vol_open(const char *image_path, gt2_vol_t **out) {
    if (!image_path || !out)
        return GT2_VOL_ERR_INVAL;
    *out = NULL;

    gt2_cd_t *cd = NULL;
    if (gt2_cd_open(image_path, &cd) != GT2_CD_OK)
        return GT2_VOL_ERR_IO;

    u8 hdr[16];
    if (read_at(cd, GT2_VOL_BASE, hdr, sizeof hdr) != 0) {
        gt2_cd_close(cd);
        return GT2_VOL_ERR_IO;
    }
    u32 magic;
    memcpy(&magic, hdr, 4);
    if (magic != GT2_VOL_MAGIC) {
        gt2_cd_close(cd);
        return GT2_VOL_ERR_BAD_MAGIC;
    }
    u16 data_count, entry_count;
    memcpy(&data_count, hdr + 8, 2);
    memcpy(&entry_count, hdr + 10, 2);
    if (data_count == 0) {
        gt2_cd_close(cd);
        return GT2_VOL_ERR_TRUNCATED;
    }

    gt2_vol_t *v = calloc(1, sizeof *v);
    if (!v) {
        gt2_cd_close(cd);
        return GT2_VOL_ERR_NO_MEM;
    }
    v->cd = cd;
    v->data_count = data_count;
    v->entry_count = entry_count;

    // Offset table: data_count+1 entries so every file has an end marker.
    v->tbl = malloc(((size_t)data_count + 1) * sizeof *v->tbl);
    if (!v->tbl) {
        gt2_vol_close(v);
        return GT2_VOL_ERR_NO_MEM;
    }
    if (read_at(cd, GT2_VOL_BASE + 0x10, v->tbl,
                ((size_t)data_count + 1) * sizeof *v->tbl) != 0) {
        gt2_vol_close(v);
        return GT2_VOL_ERR_TRUNCATED;
    }

    // Name directory: fixed 32 B records until first all-zero record.
    gt2_vol_name_t *names = calloc(GT2_VOL_DIR_CAP, sizeof *names);
    if (!names) {
        gt2_vol_close(v);
        return GT2_VOL_ERR_NO_MEM;
    }
    u32 count = 0;
    for (u32 i = 0; i < GT2_VOL_DIR_CAP; i++) {
        u8 rec[GT2_VOL_DIR_REC];
        if (read_at(cd, GT2_VOL_BASE + GT2_VOL_DIR_OFF + i * GT2_VOL_DIR_REC,
                    rec, sizeof rec) != 0)
            break;
        int allzero = 1;
        for (size_t k = 0; k < sizeof rec; k++) {
            if (rec[k]) {
                allzero = 0;
                break;
            }
        }
        if (allzero)
            break;
        u32 hash;
        u16 idx;
        memcpy(&hash, rec + 4, 4);
        memcpy(&idx, rec + 8, 2);
        char name[GT2_VOL_FLAT_NAME_MAX + 1];
        memcpy(name, rec + 11, GT2_VOL_FLAT_NAME_MAX);
        name[GT2_VOL_FLAT_NAME_MAX] = '\0';
        if (name[0] == '\0' || idx > data_count)
            continue;   // skip padding aliases, keep the table clean
        memcpy(names[count].name, name, sizeof names[count].name);
        names[count].index = idx;
        names[count].hash = hash;
        count++;
    }
    v->names = names;
    v->name_count = count;

    // Unified entry table: entry_count 32-byte slots at VOL+0xB800.
    gt2_vol_slot_t *slots = calloc(entry_count ? entry_count : 1, sizeof *slots);
    if (!slots) {
        gt2_vol_close(v);
        return GT2_VOL_ERR_NO_MEM;
    }
    for (u16 i = 0; i < entry_count; i++) {
        u8 rec[GT2_VOL_SLOT_REC];
        if (read_at(cd, GT2_VOL_BASE + GT2_VOL_SLOT_OFF + (u32)i * GT2_VOL_SLOT_REC,
                    rec, sizeof rec) != 0)
            break;  // short image: keep what parsed, walks bounds-check
        memcpy(&slots[v->slot_count].date, rec, 4);
        memcpy(&slots[v->slot_count].next, rec + 4, 2);
        slots[v->slot_count].flags = rec[6];
        memcpy(slots[v->slot_count].name, rec + 7, GT2_VOL_NAME_MAX);
        slots[v->slot_count].name[GT2_VOL_NAME_MAX] = '\0';
        v->slot_count++;
    }
    v->slots = slots;

    *out = v;
    return GT2_VOL_OK;
}

void gt2_vol_close(gt2_vol_t *vol) {
    if (!vol)
        return;
    gt2_cd_close(vol->cd);
    free(vol->tbl);
    free(vol->names);
    free(vol->slots);
    free(vol);
}

u16 gt2_vol_file_count(const gt2_vol_t *vol) {
    return vol ? vol->data_count : 0;
}

u32 gt2_vol_slot_count(const gt2_vol_t *vol) {
    return vol ? vol->slot_count : 0;
}

u32 gt2_vol_name_count(const gt2_vol_t *vol) {
    return vol ? vol->name_count : 0;
}

gt2_vol_status_t gt2_vol_file_range(const gt2_vol_t *vol, u32 index,
                                    u32 *iso_off_out, u32 *size_out) {
    if (!vol || index >= vol->data_count)
        return GT2_VOL_ERR_INVAL;
    u32 start = vol->tbl[index];
    u32 end = vol->tbl[index + 1];
    if (end < start)
        return GT2_VOL_ERR_TRUNCATED;   // corrupt table, never wrap
    if (iso_off_out)
        *iso_off_out = GT2_VOL_BASE + start;
    if (size_out)
        *size_out = end - start;
    return GT2_VOL_OK;
}

gt2_vol_status_t gt2_vol_find(const gt2_vol_t *vol, const char *name,
                              u32 *iso_off_out, u32 *size_out) {
    if (!vol || !name || !name[0])
        return GT2_VOL_ERR_INVAL;
    for (u32 i = 0; i < vol->name_count; i++) {
        if (strcmp(vol->names[i].name, name) == 0)
            return gt2_vol_file_range(vol, vol->names[i].index,
                                      iso_off_out, size_out);
    }
    return GT2_VOL_ERR_NOT_FOUND;
}

gt2_vol_status_t gt2_vol_read(gt2_vol_t *vol, const char *name,
                              u8 **data_out, u32 *size_out) {
    if (!vol || !name || !data_out)
        return GT2_VOL_ERR_INVAL;
    *data_out = NULL;
    u32 off, size;
    gt2_vol_status_t st = gt2_vol_find(vol, name, &off, &size);
    if (st != GT2_VOL_OK)
        return st;
    u8 *buf = malloc(size ? size : 1);
    if (!buf)
        return GT2_VOL_ERR_NO_MEM;
    if (size > 0 && read_at(vol->cd, off, buf, size) != 0) {
        free(buf);
        return GT2_VOL_ERR_IO;
    }
    *data_out = buf;
    if (size_out)
        *size_out = size;
    return GT2_VOL_OK;
}

void gt2_vol_visit_names(const gt2_vol_t *vol, gt2_vol_visit_fn fn, void *ctx) {
    if (!vol || !fn)
        return;
    for (u32 i = 0; i < vol->name_count; i++)
        fn(vol->names[i].name, vol->names[i].index, ctx);
}

// Hierarchical walk. Mirrors SCUS search_vol_dir (0x800100E4): linear scan
// per path component from the current listing start, strcmp per slot, stop
// past an END-flagged slot. One deliberate deviation: the game descends
// into whatever `next` says even for files (reading garbage on bad input);
// we return NOT_FOUND instead.
gt2_vol_status_t gt2_vol_stat_path(const gt2_vol_t *vol, const char *path,
                                   gt2_vol_entry_t *out) {
    if (!vol || !path || !out)
        return GT2_VOL_ERR_INVAL;
    while (*path == '/')
        path++;
    if (*path == '\0')
        return GT2_VOL_ERR_INVAL;

    u32 cur = 0;    // root listing starts at slot 0
    for (;;) {
        const char *sep = strchr(path, '/');
        size_t len = sep ? (size_t)(sep - path) : strlen(path);
        if (len == 0 || len > GT2_VOL_NAME_MAX)
            return GT2_VOL_ERR_INVAL;
        char comp[GT2_VOL_NAME_MAX + 1];
        memcpy(comp, path, len);
        comp[len] = '\0';

        u32 i = cur;
        for (;;) {
            if (i >= vol->slot_count)
                return GT2_VOL_ERR_TRUNCATED;
            const gt2_vol_slot_t *s = &vol->slots[i];
            if (strcmp(s->name, comp) == 0)
                break;
            if (s->flags & GT2_VOL_FLAG_END)
                return GT2_VOL_ERR_NOT_FOUND;
            i++;
        }
        const gt2_vol_slot_t *hit = &vol->slots[i];
        if (!sep) {
            out->date = hit->date;
            out->slot = i;
            out->next = hit->next;
            out->flags = hit->flags;
            memcpy(out->name, hit->name, sizeof out->name);
            return GT2_VOL_OK;
        }
        if (!(hit->flags & GT2_VOL_FLAG_DIR))
            return GT2_VOL_ERR_NOT_FOUND;
        if (hit->next >= vol->slot_count)
            return GT2_VOL_ERR_TRUNCATED;
        cur = hit->next;
        path = sep + 1;
    }
}

gt2_vol_status_t gt2_vol_find_path(const gt2_vol_t *vol, const char *path,
                                   u32 *iso_off_out, u32 *size_out) {
    gt2_vol_entry_t e;
    gt2_vol_status_t st = gt2_vol_stat_path(vol, path, &e);
    if (st != GT2_VOL_OK)
        return st;
    if (e.flags & GT2_VOL_FLAG_DIR)
        return GT2_VOL_ERR_IS_DIR;
    return gt2_vol_file_range(vol, e.next, iso_off_out, size_out);
}

gt2_vol_status_t gt2_vol_read_path(gt2_vol_t *vol, const char *path,
                                   u8 **data_out, u32 *size_out) {
    if (!vol || !path || !data_out)
        return GT2_VOL_ERR_INVAL;
    *data_out = NULL;
    u32 off, size;
    gt2_vol_status_t st = gt2_vol_find_path(vol, path, &off, &size);
    if (st != GT2_VOL_OK)
        return st;
    u8 *buf = malloc(size ? size : 1);
    if (!buf)
        return GT2_VOL_ERR_NO_MEM;
    if (size > 0 && read_at(vol->cd, off, buf, size) != 0) {
        free(buf);
        return GT2_VOL_ERR_IO;
    }
    *data_out = buf;
    if (size_out)
        *size_out = size;
    return GT2_VOL_OK;
}

gt2_vol_status_t gt2_vol_list_dir(const gt2_vol_t *vol, const char *path,
                                  gt2_vol_entry_visit_fn fn, void *ctx) {
    if (!vol || !fn)
        return GT2_VOL_ERR_INVAL;
    u32 cur = 0;
    if (path && path[0] != '\0' && !(path[0] == '/' && path[1] == '\0')) {
        gt2_vol_entry_t e;
        gt2_vol_status_t st = gt2_vol_stat_path(vol, path, &e);
        if (st != GT2_VOL_OK)
            return st;
        if (!(e.flags & GT2_VOL_FLAG_DIR))
            return GT2_VOL_ERR_IS_DIR;
        cur = e.next;
    }
    for (u32 i = cur;; i++) {
        if (i >= vol->slot_count)
            return GT2_VOL_ERR_TRUNCATED;
        const gt2_vol_slot_t *s = &vol->slots[i];
        gt2_vol_entry_t e;
        e.date = s->date;
        e.slot = i;
        e.next = s->next;
        e.flags = s->flags;
        memcpy(e.name, s->name, sizeof e.name);
        fn(&e, ctx);
        if (s->flags & GT2_VOL_FLAG_END)
            return GT2_VOL_OK;
    }
}

gt2_vol_status_t gt2_vol_span(const gt2_vol_t *vol, const char *first,
                              const char *last, u32 *count_out) {
    if (!vol || !first || !last)
        return GT2_VOL_ERR_INVAL;
    gt2_vol_entry_t a, b;
    gt2_vol_status_t st = gt2_vol_stat_path(vol, first, &a);
    if (st != GT2_VOL_OK)
        return st;
    st = gt2_vol_stat_path(vol, last, &b);
    if (st != GT2_VOL_OK)
        return st;
    if (count_out)
        *count_out = b.next - a.next - 1;   // u32 wrap matches subu
    return GT2_VOL_OK;
}
