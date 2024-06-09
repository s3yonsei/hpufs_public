#include "ftl.h"

static void *ftl_thread(void *arg);


bool should_gc(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;

#ifdef HPB_PLUS
    if (u_info->gc_info.write_cnt > 4096)
    {
        if (u_info->gc_info.blk_idx >= ssd->blk_per_plane)
        {
            if (u_info->gc_info.next_blk_idx < ssd->blk_per_plane) 
            {
                u_info->gc_info.blk_idx = u_info->gc_info.next_blk_idx;
                u_info->gc_info.next_blk_idx = ssd->blk_per_plane;
                u_info->gc_info.searched_cnt = 0;
                u_info->gc_info.scan_idx = 0;
            }
        }
    }

    if ((u_info->gc_info.write_cnt > 4096) &&
        (u_info->gc_info.blk_idx < ssd->blk_per_plane))
#else
    if (u_info->gc_info.write_cnt > 4096)
#endif
    {
        return true;
    }

    return FALSE;
}

void increase_gc_cnt(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    uint32_t threshold = u_info->free_blk_cnt;

    if (GC_THRESHOLD_1 > threshold)
    {
        u_info->gc_info.write_cnt++;
    }
    else
    {
        u_info->gc_info.write_cnt = 0;
        u_info->gc_info.blk_idx = ssd->blk_per_plane;
        u_info->gc_info.next_blk_idx = ssd->blk_per_plane;
    }
}

void decrease_gc_cnt(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    uint32_t threshold = u_info->free_blk_cnt;

    if (GC_THRESHOLD_3 > threshold)
    {
        if (u_info->gc_info.write_cnt >= u_info->gc_info.searched_cnt)
            u_info->gc_info.write_cnt -= u_info->gc_info.searched_cnt;
        else
            u_info->gc_info.write_cnt = 0;
    }
    else if (GC_THRESHOLD_2 > threshold)
    {
        if (u_info->gc_info.write_cnt >= (u_info->gc_info.searched_cnt * 2))
            u_info->gc_info.write_cnt -= (u_info->gc_info.searched_cnt * 2);
        else
            u_info->gc_info.write_cnt = 0;
    }
    else if (GC_THRESHOLD_1 > threshold)
    {
        if (u_info->gc_info.write_cnt >= (u_info->gc_info.searched_cnt * 3))
            u_info->gc_info.write_cnt -= (u_info->gc_info.searched_cnt * 3);
        else
            u_info->gc_info.write_cnt = 0;
    }
}

static void ssd_init_params(struct ssd *ssd, struct ssdparams *spp)
{
    spp->secsz = 4096;
    spp->secs_per_pg = 1;               /* 4KB */
    spp->pgs_per_blk = PAGES_PER_BLK;   /* 256KB */
    spp->blks_per_pl = ssd->blk_per_plane;   /* 256MB */
    spp->pls_per_lun = 1;
    spp->luns_per_ch = LUNS_PER_CH;     /* 2GB */
    spp->nchs = CHS;                    /* 16GB */

    spp->pg_rd_lat = NAND_READ_LATENCY;
    spp->pg_wr_lat = NAND_PROG_LATENCY;
    spp->blk_er_lat = NAND_ERASE_LATENCY;
    spp->ch_xfer_lat = 0;

    /* calculated values */
    spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
    spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
    spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
    spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
    spp->tt_secs = spp->secs_per_ch * spp->nchs;

    spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
    spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

    spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun;
    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch;
    spp->tt_blks = spp->blks_per_ch * spp->nchs;

    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch;
    spp->tt_pls = spp->pls_per_ch * spp->nchs;

    spp->tt_luns = spp->luns_per_ch * spp->nchs;

    /* line is special, put it at the end */
    spp->blks_per_line = spp->tt_luns; /* TODO: to fix under multiplanes */
    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
    spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */

    spp->enable_gc_delay = true;
}

static void ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp)
{
    lun->npls = spp->pls_per_lun;
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}

static void ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

void ssd_init(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    ftl_assert(ssd);

    ssd->blk_per_plane = n->blk_per_plane;
    ssd->meta_blk_per_plane = n->meta_blk_per_plane;
    ssd->total_l2p_segment_in_table = n->total_l2p_segment_in_table;

#ifdef TEST
    for (int i = 0; i < 131072; i++)
    {
        ssd->array[i] = 0;
    }
    ssd->arraycnt = 0;
    ssd->arraytime = 0;
#endif

#ifdef HPB
    ssd_init_hpb(ssd);
#endif
    ssd_init_buf(ssd);
    ssd_init_params(ssd, spp);
    ssd_init_log_buffer(n, ssd);
    ssd_init_meta_info(ssd);
    ssd_init_user_info(n, ssd);
    ssd_init_l2p_table(n, ssd);

    /* initialize ssd internal layout architecture */
    ssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }

    qemu_thread_create(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}

static inline bool valid_ppa(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;

    if (ch >= 0 && ch < spp->nchs && lun >= 0 && lun < spp->luns_per_ch && pl >=
        0 && pl < spp->pls_per_lun && blk >= 0 && blk < spp->blks_per_pl && pg
        >= 0 && pg < spp->pgs_per_blk && sec >= 0 && sec < spp->secs_per_pg)
        return true;

    return false;
}

static inline bool valid_lpn(struct ssd *ssd, uint64_t lpn)
{
    return (lpn < ssd->sp.tt_pgs);
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static inline struct ssd_channel *get_ch(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

static inline struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}

static uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct nand_cmd *ncmd)
{
    int c = ncmd->cmd;
    uint64_t cmd_stime;
    uint64_t nand_stime;
    struct ssdparams *spp = &ssd->sp;
    struct ssd_channel *ch = get_ch(ssd, ppa);
    uint64_t lat = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    switch (c) {
    case NAND_READ:
        cmd_stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        ssd->u_info.nand_read_cnt++;

        if ((ncmd->cnt == 0) || (spp->read_ppa.g.ch != ppa->g.ch) || ((spp->read_ppa.ppa + 1) != ppa->ppa))
        {
            cmd_stime = ncmd->stime;
            nand_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : ch->next_ch_avail_time;

            if ((ncmd->type == USER_IO) || (ncmd->type == GC_IO)) {
                ch->next_ch_avail_time = nand_stime + spp->pg_rd_lat;
            } else {
                ch->next_ch_avail_time = nand_stime + NAND_SLC_READ_LATENCY;
            }
        }
        spp->read_ppa.ppa = ppa->ppa;

        insert_buf(ssd, ch->next_ch_avail_time, ncmd->segment_idx, ncmd->type, false);
        lat = ch->next_ch_avail_time;
        break;

    case NAND_WRITE:
        cmd_stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        nand_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : ch->next_ch_avail_time;
        ssd->u_info.nand_write_cnt++;

        if ((ncmd->type == USER_IO) || (ncmd->type == GC_IO)) 
        {
            if ((spp->user_write_ppa.g.ch != ppa->g.ch) || ((spp->user_write_ppa.ppa + 1) != ppa->ppa))
            {
                ch->next_ch_avail_time = nand_stime + spp->pg_wr_lat;
            }
            spp->user_write_ppa.ppa = ppa->ppa;
        } 
        else 
        {
            if ((ncmd->cnt == 0) || (spp->meta_write_ppa.g.lun != ppa->g.ch) || ((spp->meta_write_ppa.ppa + 1) != ppa->ppa))
            {
                ch->next_ch_avail_time = nand_stime + NAND_SLC_PROG_LATENCY;
            }
            spp->meta_write_ppa.ppa = ppa->ppa;
        }

        insert_buf(ssd, ch->next_ch_avail_time, ncmd->segment_idx, ncmd->type, false);

        if (ncmd->fua)
        {
            lat = ch->next_ch_avail_time;
        }
        else
        {
            lat = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        }
        break;

    case NAND_ERASE:
        cmd_stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        nand_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : ch->next_ch_avail_time;
        ch->next_ch_avail_time = nand_stime + spp->blk_er_lat;

        lat = ch->next_ch_avail_time;
        break;

    default:
        ftl_err("Unsupported NAND command: 0x%x\n", c);
    }

    return lat;
}

#ifndef HPB_PLUS
uint32_t gc_search_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t src_blk, uint32_t total_buf_cnt)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct gc_blk_info *gc_info = &ssd->u_info.gc_info;
    struct nand_cmd srd;
    struct ppa ppn;
    int segment_idx = get_l2p_segment_idx(gc_info->scan_idx);
    int gc_copy_cnt = 0;
    uint64_t stime = 0;

    do_sync(ssd);
    stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    srd.type = META_IO;
    srd.cmd = NAND_READ;
    srd.fua = false;
    srd.segment_idx = ssd->total_l2p_segment_in_table;
    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    ppn.ppa = ssd->u_info.bitmap_ppn[ssd->u_info.blk_idx];
    ssd_advance_status(ssd, &ppn, &srd);
    do_sync(ssd);

    while (segment_idx < ssd->total_l2p_segment_in_table)
    {
        uint32_t buf_cnt = 0;
        srd.cnt = 0;

        do_sync(ssd);
        // read l2p segment from nand flash memory
        while (buf_cnt < total_buf_cnt)
        {
            uint32_t bitmap_offset = segment_idx % 32;
            uint32_t bitmap_idx = segment_idx / 32;

            srd.type = META_IO;
            srd.cmd = NAND_READ;
            srd.fua = false;
            srd.segment_idx = ssd->total_l2p_segment_in_table;
            srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            ppn = l2p_table->arr_segment_ppn[segment_idx];

            if (ssd->u_info.arr_user_blk[gc_info->blk_idx].bitmap.segment_bitmap[bitmap_idx] & ((uint32_t)0x1 << bitmap_offset))
            {
                ssd_advance_status(ssd, &ppn, &srd);
                gc_info->scan_cnt++;
                buf_cnt++;
                srd.cnt++;
            }

            segment_idx++;
            
            if (segment_idx >= ssd->total_l2p_segment_in_table)
            {
                break;
            }
        }

        do_sync(ssd);

        while (get_l2p_segment_idx(gc_info->scan_idx) < segment_idx)
        {
            int copy_segment_idx = get_l2p_segment_idx(gc_info->scan_idx);
            int copy_offset = get_l2p_entry_idx_in_segment(gc_info->scan_idx);
            uint32_t bitmap_offset = copy_segment_idx % 32;
            uint32_t bitmap_idx = copy_segment_idx / 32;

            if (ssd->u_info.arr_user_blk[gc_info->blk_idx].bitmap.segment_bitmap[bitmap_idx] & ((uint32_t)0x1 << bitmap_offset))
            {
                if (l2p_table->arr_segment[copy_segment_idx]->arr_entry[copy_offset].ppn.g.blk == src_blk) 
                {
                    ppa[gc_copy_cnt].ppa = l2p_table->arr_segment[copy_segment_idx]->arr_entry[copy_offset].ppn.ppa;
                    lpn[gc_copy_cnt] = gc_info->scan_idx;
                    gc_copy_cnt++;
                }
            }
            gc_info->scan_idx++;

            if (gc_copy_cnt >= total_buf_cnt)
            {
                break;
            }
        }

        if (gc_copy_cnt >= total_buf_cnt)
        {
            break;
        }
    }

    do_sync(ssd);

    gc_info->gc_search_latency += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - stime;

    return gc_copy_cnt;
}
#endif

void gc_read_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t searched_cnt)
{
#ifdef HPB_PLUS
    struct gc_blk_info *gc_info = &ssd->u_info.gc_info;
    struct valid_page_bitmap* u_bitmap = gc_info->user_bitmap;
    struct nand_cmd srd;
    struct ppa ppn;

    int gc_entry_idx = gc_info->blk_idx % 8;
    int blk_idx = gc_info->blk_idx;
    int total_buf_cnt = 0;
    int buf_cnt = 0;
    srd.cnt = 0;

    do_sync(ssd);
    total_buf_cnt = ssd->buf.free_buf_cnt;

    while(gc_info->scan_idx < PAGES_PER_BLK_LINE)
    {
        int lun = gc_info->scan_idx % LUNS_PER_CH;
        int ch = (gc_info->scan_idx / LUNS_PER_CH) % CHS;
        int pg = gc_info->scan_idx / (LUNS_PER_CH * CHS);

        gc_info->scan_idx++;

        if (u_bitmap->bitmap[gc_entry_idx][lun][ch] & ((uint64_t)0x1 << pg))
        {
            srd.type = USER_IO;
            srd.cmd = NAND_READ;
            srd.fua = false;
            srd.segment_idx = ssd->total_l2p_segment_in_table;
            srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            ppn.ppa = 0;
            ppn.g.blk = blk_idx;
            ppn.g.lun = lun;
            ppn.g.ch = ch;
            ppn.g.pg = pg;

            u_bitmap->bitmap[gc_entry_idx][lun][ch] &= ~((uint64_t)0x1 << pg);
            lpn[buf_cnt] = ssd->u_info.arr_user_blk[blk_idx].lpn.arr_lpn[pg][lun][ch];
            ppa[buf_cnt].ppa = ppn.ppa;

            buf_cnt++;

            ssd_advance_status(ssd, &ppn, &srd);
            srd.cnt++;

            if (buf_cnt >= total_buf_cnt) break;
        }
    }

    gc_info->searched_cnt = buf_cnt;
#else
#ifdef IDEAL
    struct gc_blk_info *gc_info = &ssd->u_info.gc_info;
    struct user_blk *u_blk = &ssd->u_info.arr_user_blk[gc_info->blk_idx];
    struct nand_cmd srd;
    struct ppa ppn;

    int blk_idx = gc_info->blk_idx;
    int total_buf_cnt = 0;
    int buf_cnt = 0;
    srd.cnt = 0;

    do_sync(ssd);
    total_buf_cnt = ssd->buf.free_buf_cnt;

    while(gc_info->scan_idx < PAGES_PER_BLK_LINE)
    {
        int lun = gc_info->scan_idx % LUNS_PER_CH;
        int ch = (gc_info->scan_idx / LUNS_PER_CH) % CHS;
        int pg = gc_info->scan_idx / (LUNS_PER_CH * CHS);

        gc_info->scan_idx++;

        if (u_blk->valid_page_bitmap[lun][ch] & ((uint64_t)0x1 << pg))
        {
            srd.type = USER_IO;
            srd.cmd = NAND_READ;
            srd.fua = false;
            srd.segment_idx = ssd->total_l2p_segment_in_table;
            srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            ppn.ppa = 0;
            ppn.g.blk = blk_idx;
            ppn.g.lun = lun;
            ppn.g.ch = ch;
            ppn.g.pg = pg;

            lpn[buf_cnt] = ssd->u_info.arr_user_blk[blk_idx].lpn.arr_lpn[pg][lun][ch];
            ppa[buf_cnt].ppa = ppn.ppa;

            buf_cnt++;

            ssd_advance_status(ssd, &ppn, &srd);
            srd.cnt++;

            if (buf_cnt >= total_buf_cnt) break;
        }
    }

    gc_info->searched_cnt = buf_cnt;
#else
    struct nand_cmd gcr;
    gcr.cnt = 0;

    for (int idx = 0; idx < searched_cnt; idx++) 
    {
        gcr.type = GC_IO;
        gcr.cmd = NAND_READ;
        gcr.fua = false;
        gcr.segment_idx = ssd->total_l2p_segment_in_table;
        gcr.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        ssd_advance_status(ssd, &ppa[idx], &gcr);
        gcr.cnt++;
    }
#endif
#endif
    do_sync(ssd);
}

void gc_write_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t searched_cnt)
{
    struct ppa new_ppa;
    struct nand_cmd gcw;

    gcw.cnt = 0;

    for (int idx = 0; idx < searched_cnt; idx++) 
    {
        gcw.type = GC_IO;
        gcw.cmd = NAND_WRITE;
        gcw.fua = false;
        gcw.segment_idx = ssd->total_l2p_segment_in_table;
        gcw.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        new_ppa = get_new_user_ppn(ssd);
        ssd->u_info.gc_cnt++;

        ssd_advance_status(ssd, &new_ppa, &gcw);

#ifdef IDEAL
        {
            struct user_info *u_info = &ssd->u_info;
            struct l2p_table  *l2p_table = &ssd->l2p_table;
            uint32_t segment_idx = lpn[idx] / TOTAL_L2P_ENTRY_IN_SEGMENT;
            uint32_t entry_idx_in_segment = lpn[idx] % TOTAL_L2P_ENTRY_IN_SEGMENT;
            struct ppa src_ppn;

            ftl_assert(l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa == ppa[idx].ppa);
            ssd->u_info.arr_user_blk[new_ppa.g.blk].lpn.arr_lpn[new_ppa.g.pg][new_ppa.g.lun][new_ppa.g.ch] = lpn[idx];
            src_ppn.ppa = ppa[idx].ppa;

            update_user_info(ssd,
                             src_ppn,
                             new_ppa.ppa,
                             lpn[idx]);

            u_info->arr_user_blk[src_ppn.g.blk].valid_page_bitmap[src_ppn.g.lun][src_ppn.g.ch] &= ~((uint64_t)0x1 << src_ppn.g.pg);
            u_info->arr_user_blk[new_ppa.g.blk].valid_page_bitmap[new_ppa.g.lun][new_ppa.g.ch] |= ((uint64_t)0x1 << new_ppa.g.pg);

            l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa = new_ppa.ppa;

#ifdef DEBUG_DRAMLESS_SSD
            check_ssd_user_valid_page(ssd);
#endif
        }
#else
        insert_log_buffer(ssd, lpn[idx], &new_ppa, ppa[idx].g.blk, gcw.cnt);
#endif
        gcw.cnt++;
    }

    do_sync(ssd);
}

#ifndef HPB_PLUS
void check_user_free_blk(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;

    for (int idx = 0; idx < ssd->blk_per_plane; idx++) 
    {
        struct user_blk *u_blk = &u_info->arr_user_blk[idx];

        if (u_info->blk_idx != u_blk->blk_idx) 
        {
            if (u_blk->bfree == false)
            {
                if (u_blk->valid_page_count == 0) 
                {
                    if (u_info->gc_info.blk_idx == u_blk->blk_idx)
                    {
                        u_info->gc_info.blk_idx = ssd->blk_per_plane;
                        u_info->gc_info.scan_idx = 0;
                    }

                    //printf("gc_user_free_blk : %d\n", u_blk->blk_idx);
#ifdef DEBUG_DRAMLESS_SSD
                    check_free_valid_cnt(ssd, u_blk->blk_idx);
#endif
                    u_blk->bfree = true;
                    u_blk->bdogc = false;
                    u_blk->free_age = u_info->free_age;
                    u_info->free_age++;
                    u_info->free_blk_cnt++;
                }
            }
        }
    }
}
#endif

#ifndef HPB_PLUS
void gc_select_blk(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct gc_blk_info *gc_info = &u_info->gc_info;

    do_sync(ssd);
    gc_info->total_buf_cnt = ssd->buf.free_buf_cnt;

    if (gc_info->blk_idx >= ssd->blk_per_plane) 
    {
        uint32_t min_valid_page_count = 0xFFFFFFFF;

        for (int idx = 0; idx < ssd->blk_per_plane; idx++) 
        {
            struct user_blk *u_blk = &u_info->arr_user_blk[idx];

            if (u_info->blk_idx != u_blk->blk_idx) 
            {
                if ((u_blk->bfree == false) && (u_blk->bdogc == false))
                {
                    if (u_blk->valid_page_count < min_valid_page_count) 
                    {
                        min_valid_page_count = u_blk->valid_page_count;
                        gc_info->blk_idx = u_blk->blk_idx;
                    }
                }
            }
        }

        if (gc_info->blk_idx < ssd->blk_per_plane)
        {
            u_info->arr_user_blk[gc_info->blk_idx].bdogc = true;
            gc_info->scan_idx = 0;
        }
    }

    ftl_assert(gc_info->blk_idx < ssd->blk_per_plane);
}
#endif

void do_gc(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct gc_blk_info *gc_info = &u_info->gc_info;
    struct ppa ppa[TOTAL_BUF_CNT];
    uint32_t lpn[TOTAL_BUF_CNT];

#ifdef HPB_PLUS
    if ((ssd->log_buf.cur_index - ssd->log_buf.complete_index) > TOTAL_TRANSFER_IDX_THRESHOLD)
    {
        return;    
    }

    if (ssd->log_buf.cur_index < TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_GC_THRESHOLD)
#else
    gc_select_blk(ssd);

    if (ssd->log_buf.cur_index < TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_THRESHOLD)
#endif
    {
        uint64_t stime = 0;
        ftl_assert(gc_info->blk_idx == u_info->arr_user_blk[gc_info->blk_idx].blk_idx);

#ifndef HPB_PLUS 
#ifndef IDEAL
        u_info->gc_info.searched_cnt = gc_search_page(ssd, ppa, lpn, gc_info->blk_idx, gc_info->total_buf_cnt);
#endif        
#endif

        stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        gc_read_page(ssd, ppa, lpn, u_info->gc_info.searched_cnt);
        gc_write_page(ssd, ppa, lpn, u_info->gc_info.searched_cnt);
        gc_info->gc_latency += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - stime;

#ifdef HPB_PLUS
        if (gc_info->scan_idx >= PAGES_PER_BLK_LINE)
        {
            //printf("GC END  gc_blk_idx: %7d  scan_idx: %7d\n", gc_info->blk_idx, gc_info->scan_idx);
            gc_info->blk_idx = ssd->blk_per_plane;
        }
#else
#ifdef IDEAL
        if (gc_info->scan_idx >= PAGES_PER_BLK_LINE)
        {
            //printf("GC END  gc_blk_idx: %7d  scan_idx: %7d\n", gc_info->blk_idx, gc_info->scan_idx);
            gc_info->blk_idx = ssd->blk_per_plane;
            check_user_free_blk(ssd);
        }
#else
        if (gc_info->scan_idx >= (ssd->total_l2p_segment_in_table * TOTAL_L2P_ENTRY_IN_SEGMENT))
        {
            //printf("GC END  search: %8ld gc_blk_idx: %7d  scan_idx: %7d\n", ssd->u_info.gc_info.gc_search_latency / 1000 / 1000, gc_info->blk_idx, gc_info->scan_idx);
            gc_info->blk_idx = ssd->blk_per_plane;
            gc_info->scan_idx = 0;
        }
#endif
#endif
    }
    
    decrease_gc_cnt(ssd);
}

uint64_t ssd_read(struct ssd *ssd, NvmeRequest *req)
{
    struct ssdparams *spp = &ssd->sp;
    uint32_t lba = req->slba;
    int nsecs = req->nlb;
    struct nand_cmd srd;
    struct ppa ppa;
    uint32_t start_lpn = lba / spp->secs_per_pg;
    uint32_t end_lpn = (lba + nsecs - 1) / spp->secs_per_pg;
    uint32_t lpn;
    uint32_t idx = 0;
    uint64_t sublat = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    uint64_t maxlat = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    //bool bhit = false;

    srd.cnt = 0;

    for (lpn = start_lpn; lpn <= end_lpn; lpn++, idx++) 
    {
#ifdef IDEAL
        {
            struct l2p_table  *l2p_table = &ssd->l2p_table;
            int segment_idx = lpn / TOTAL_L2P_ENTRY_IN_SEGMENT;
            int entry_idx_in_segment = lpn % TOTAL_L2P_ENTRY_IN_SEGMENT;

            ppa.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa;
        }
#else
        struct shared_buf *buf = &ssd->buf;

        ppa.ppa = search_log_buffer(ssd, lpn);

        if (ppa.ppa == INVALID_PPA) {
            ppa.ppa = search_map_buf(ssd, lpn);
        }

#ifdef HPB
        ssd_check_hpb_l2p_entry(ssd, req, &ppa, lpn, idx);

/*
        if ((ppa.ppa != INVALID_PPA) && (ppa.ppa != WAITING_PPA)) {
            bhit = true;
        }
*/
#endif

#ifdef HPB_PLUS
        if ((ppa.ppa == INVALID_PPA) || (ppa.ppa == WAITING_PPA)) {
            struct l2p_table  *l2p_table = &ssd->l2p_table;
            int segment_idx = lpn / TOTAL_L2P_ENTRY_IN_SEGMENT;
            int entry_idx_in_segment = lpn % TOTAL_L2P_ENTRY_IN_SEGMENT;

            ppa.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa;
        }
#endif
        if ((ppa.ppa == INVALID_PPA) || (ppa.ppa == WAITING_PPA)) {
            if (ppa.ppa == INVALID_PPA) {
                req->nlb -= (lpn - start_lpn);
                req->slba = lpn;

                if (buf->map_buf_cnt >= buf->max_map_buf_cnt) {
                    delete_map_buf(ssd);
                }

                if (buf->map_buf_cnt < buf->max_map_buf_cnt) {
                    srd.type = SEGMENT_READ_FOR_USER_READ;
                    srd.cmd = NAND_READ;
                    srd.fua = false;
                    srd.segment_idx = get_l2p_segment_idx(lpn);
                    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
                    srd.cnt = 0;

                    ppa = get_meta_ppn(ssd, srd.segment_idx);
                    ftl_assert(ppa.ppa != UNMAPPED_PPA);
                    ftl_assert(ppa.ppa != WAITING_PPA);
                    ftl_assert(ppa.ppa != INVALID_PPA);

                    ssd_advance_status(ssd, &ppa, &srd);

                    ssd->m_info.map_read_cnt++;
                    req->map_read++;
                }
            }

            maxlat = WAITING_LATENCY;
            break;
        }
#endif

        if (!mapped_ppa(&ppa) || (ppa.ppa >= INVALID_PPA) || !valid_ppa(ssd, &ppa)) {
            //printf("%s,lpn(%" PRId64 ") not mapped to valid ppa\n", ssd->ssdname, lpn);
            //printf("Invalid ppa,ch:%d,lun:%d,blk:%d,pl:%d,pg:%d,sec:%d\n",
            //ppa.g.ch, ppa.g.lun, ppa.g.blk, ppa.g.pl, ppa.g.pg, ppa.g.sec);
            continue;
        }

        ftl_assert(lpn == ssd->u_info.arr_user_blk[ppa.g.blk].lpn.arr_lpn[ppa.g.pg][ppa.g.lun][ppa.g.ch]);
        ssd->u_info.read_cnt++;
/*
        if ((ssd->u_info.read_cnt % 10000) == 0)
            printf("READ CMD:%8d  READ:%8d  MAP HIT:%8d  MAP READ:%8d  READ BUFFER:%8d  Transfer:%8d\n", 
                    ssd->u_info.read_cmd_cnt, 
                    ssd->u_info.read_cnt, 
                    ssd->m_info.map_hit_cnt,
                    ssd->m_info.map_read_cnt, 
                    ssd->m_info.read_l2p_segment_cnt, 
                    ssd->m_info.transfer_l2p_segment_cnt);
*/
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.fua = false;
        srd.segment_idx = ssd->total_l2p_segment_in_table;
        srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        sublat = ssd_advance_status(ssd, &ppa, &srd);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
        srd.cnt++;
    }

/*
    if (req->init == 0)
    {
        ssd->u_info.read_cmd_cnt++;

        if (bhit) {
            ssd->m_info.map_hit_cnt++;
        }

        if ((ssd->u_info.read_cmd_cnt % 10000) == 0)
        {
            printf("READ CMD:%8d  READ:%8d  MAP HIT:%8d  MAP INVALID:%8d  MAP READ:%8d  READ BUFFER:%8d  Transfer:%8d\n", 
                ssd->u_info.read_cmd_cnt, 
                ssd->u_info.read_cnt, 
                ssd->m_info.map_hit_cnt,
                ssd->m_info.map_invalid_cnt,
                ssd->m_info.map_read_cnt, 
                ssd->m_info.read_l2p_segment_cnt, 
                ssd->m_info.transfer_l2p_segment_cnt);

            ssd->u_info.read_cmd_cnt = 0;
            ssd->u_info.read_cnt = 0;
            ssd->m_info.map_hit_cnt = 0;
            ssd->m_info.map_invalid_cnt = 0;
            ssd->m_info.map_read_cnt = 0;
            ssd->m_info.read_l2p_segment_cnt = 0;
            ssd->m_info.transfer_l2p_segment_cnt = 0;
        }
    }

    req->init = 1;
*/

    return maxlat;
}

uint64_t ssd_write(struct ssd *ssd, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    struct ssdparams *spp = &ssd->sp;
    int len = req->nlb;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
    struct nand_cmd swr;
    struct ppa ppa;
    uint64_t lpn;
    uint64_t curlat = 0, maxlat = 0;

    swr.cnt = 0;

#ifdef HPB_PLUS
    ssd_check_hpb_free_blk_idx(ssd, req);
#endif

    increase_gc_cnt(ssd);

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) 
    {
        ppa = get_new_user_ppn(ssd);
        ssd->u_info.write_cnt++;

        swr.type = USER_IO;
        swr.cmd = NAND_WRITE;
        swr.fua = false;
        swr.segment_idx = ssd->total_l2p_segment_in_table;
        swr.stime = req->stime;

        if (req->control & NVME_RW_FUA)
        {
            swr.fua = true;
        }
        
        /* get latency statistics */
        curlat = ssd_advance_status(ssd, &ppa, &swr);

#ifdef IDEAL
        {
            struct l2p_table *l2p_table = &ssd->l2p_table;
            struct user_info *u_info = &ssd->u_info;
            uint32_t segment_idx = lpn / TOTAL_L2P_ENTRY_IN_SEGMENT;
            uint32_t entry_idx_in_segment = lpn % TOTAL_L2P_ENTRY_IN_SEGMENT;
            struct ppa src_ppn;

            ssd->u_info.arr_user_blk[ppa.g.blk].lpn.arr_lpn[ppa.g.pg][ppa.g.lun][ppa.g.ch] = lpn;
            src_ppn.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa;

            update_user_info(ssd,
                             src_ppn,
                             ppa.ppa,
                             lpn);

            u_info->arr_user_blk[src_ppn.g.blk].valid_page_bitmap[src_ppn.g.lun][src_ppn.g.ch] &= ~((uint64_t)0x1 << src_ppn.g.pg);
            u_info->arr_user_blk[ppa.g.blk].valid_page_bitmap[ppa.g.lun][ppa.g.ch] |= ((uint64_t)0x1 << ppa.g.pg);

            l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa = ppa.ppa;
        }
#else
        insert_log_buffer(ssd, lpn, &ppa, INVALID_BLK, swr.cnt);
#endif

        maxlat = (curlat > maxlat) ? curlat : maxlat;
        swr.cnt++;

        if ((ssd->u_info.write_cnt % 10000) == 0)
        {
            uint32_t average = 0;

            if (ssd->log_buf.log_flush_cnt > 0)
                average = ssd->log_buf.log_flush_segment_cnt / ssd->log_buf.log_flush_cnt;

            printf("WRITE:%8d  N_WRITE:%8d  N_READ:%8d  A_F_M:%4d  GC:%8d  GC_SEARCH:%8d  M_W:%6d  HOST M_W:%6d HOST R_B:%6d FREE_BLK: %4d REPLAY: %4d (CUR:%8ldms  MAP_W:%6ldms  GC_S:%6ldms  GC:%6ldms)\n", 
                    ssd->u_info.write_cnt, 
                    ssd->u_info.nand_write_cnt,
                    ssd->u_info.nand_read_cnt,
                    average,
                    ssd->u_info.gc_cnt,
                    ssd->u_info.gc_info.scan_cnt,
                    ssd->m_info.write_cnt,
                    ssd->m_info.host_write_cnt,
                    ssd->m_info.read_l2p_segment_cnt,
                    ssd->u_info.free_blk_cnt,
                    ssd->u_info.replay_valid_cnt,
                    qemu_clock_get_ns(QEMU_CLOCK_REALTIME) / 1000 / 1000,
                    ssd->log_buf.map_write_latency / 1000 / 1000,
                    ssd->u_info.gc_info.gc_search_latency / 1000 / 1000,
                    ssd->u_info.gc_info.gc_latency / 1000 / 1000);

            ssd->log_buf.log_flush_segment_cnt = 0;
            ssd->log_buf.log_flush_cnt = 0;
        }
    }

    return maxlat;
}

uint64_t ssd_unmap(struct ssd *ssd, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    struct ssdparams *spp = &ssd->sp;
    struct nand_cmd swr;
    int len = req->nlb;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
    uint64_t lpn;

    swr.cnt = 0;

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) 
    {
#ifdef IDEAL
        {
            struct l2p_table *l2p_table = &ssd->l2p_table;
            struct user_info *u_info = &ssd->u_info;
            uint32_t segment_idx = lpn / TOTAL_L2P_ENTRY_IN_SEGMENT;
            uint32_t entry_idx_in_segment = lpn % TOTAL_L2P_ENTRY_IN_SEGMENT;
            struct ppa src_ppn;

            src_ppn.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa;

            update_user_info(ssd,
                             src_ppn,
                             UNMAPPED_PPA,
                             lpn);

            u_info->arr_user_blk[src_ppn.g.blk].valid_page_bitmap[src_ppn.g.lun][src_ppn.g.ch] &= ~((uint64_t)0x1 << src_ppn.g.pg);
            l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa = UNMAPPED_PPA;
        }
#else
        struct ppa ppa;
        ppa.ppa = UNMAPPED_PPA;

        insert_log_buffer(ssd, lpn, &ppa, INVALID_BLK, swr.cnt);
#endif
        swr.cnt++;
    }

    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

uint64_t ssd_fsync(struct ssd *ssd, NvmeRequest *req)
{
    do_sync(ssd);
    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

void ssd_recovery(struct ssd *ssd)
{
    struct l2p_table* l2p_table = &ssd->l2p_table;
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    struct user_info* u_info = &ssd->u_info;
    struct nand_cmd srd;
    struct ppa ppn;

    bool arr_update_segment[TOTAL_L2P_SEGMENT_IN_TABLE] = { false, };
    bool arr_load_segment[TOTAL_L2P_SEGMENT_IN_TABLE] = { false, };
    uint32_t array_blk[100] = { 0, };
    uint32_t array_blk_cnt = 0;
    uint32_t array_blk_idx;
    uint32_t segment_idx;
    uint32_t idx = 0;
    uint32_t lpn;
    uint64_t s_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    
    uint64_t s_scan_time = 0;
    uint64_t s_l2p_read_time = 0;
    uint64_t s_l2p_update_time = 0;
    uint64_t s_l2p_write_time = 0;

    uint64_t e_scan_time = 0;
    uint64_t e_l2p_read_time = 0;
    uint64_t e_l2p_update_time = 0;
    uint64_t e_l2p_write_time = 0;

    uint32_t l2p_segment_cnt = 0;
    uint32_t page_cnt = 0;
    uint32_t cnt = 0;

    do_sync(ssd);

    srd.cnt = 0;

    while (u_info->replay_valid_cnt > 1) 
    {
        int replay_pop_idx = u_info->replay_pop_idx;
        int replay_blk_idx = u_info->replay_blk[replay_pop_idx];

        array_blk[array_blk_cnt] = replay_blk_idx;
        array_blk_cnt++;

        for (int pg = 0; pg < PAGES_PER_BLK; pg++)
        {
            for (int ch = 0; ch < CHS; ch++)
            {
                for (int lun = 0; lun < LUNS_PER_CH; lun++)
                {
                    while (ssd->buf.free_buf_cnt == 0) {
                        delete_io_buf(ssd);
                    }

                    ppn.ppa = 0;
                    ppn.g.blk = replay_blk_idx;
                    ppn.g.lun = lun;
                    ppn.g.ch = ch;
                    ppn.g.pg = pg;       

                    srd.type = USER_IO;
                    srd.cmd = NAND_READ;
                    srd.fua = false;
                    srd.segment_idx = ssd->total_l2p_segment_in_table;
                    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

                    ssd_advance_status(ssd, &ppn, &srd);

                    lpn = ssd->u_info.arr_user_blk[ppn.g.blk].lpn.arr_lpn[ppn.g.pg][ppn.g.lun][ppn.g.ch];
                    arr_update_segment[get_l2p_segment_idx(lpn)] = true;
                    page_cnt++;
                    srd.cnt++;
                }  
            }
        }

        u_info->replay_bitmap_idx = 0;
        u_info->replay_pop_idx++;
        u_info->replay_valid_cnt--;

        if (u_info->replay_pop_idx >= TOTAL_REPLAY_BLK_CNT)
            u_info->replay_pop_idx = 0;

        if (array_blk_cnt >= TOTAL_POWER_FAIL_RECOVERY_CNT)
        {
            break;
        }
    }

    do_sync(ssd);

    s_l2p_read_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    s_scan_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    e_scan_time = s_scan_time - s_time;

    srd.cnt = 0;

    for (segment_idx = 0; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) 
    {
        if (arr_update_segment[segment_idx])
        {
            srd.type = META_IO;
            srd.cmd = NAND_READ;
            srd.fua = false;
            srd.segment_idx = ssd->total_l2p_segment_in_table;
            srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            
            ppn = get_meta_ppn(ssd, segment_idx);
            ssd_advance_status(ssd, &ppn, &srd);
            
            arr_load_segment[segment_idx] = true;
            l2p_segment_cnt++;
            cnt++;
        }

        if (cnt == 64)
        {
            do_sync(ssd);

            e_l2p_read_time += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s_l2p_read_time;
            s_l2p_update_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            for (array_blk_idx = 0; array_blk_idx < array_blk_cnt; array_blk_idx++) 
            {
                for (int pg = 0; pg < PAGES_PER_BLK; pg++)
                {
                    for (int ch = 0; ch < CHS; ch++)
                    {
                        for (int lun = 0; lun < LUNS_PER_CH; lun++)
                        {
                            ppn.ppa = 0;
                            ppn.g.blk = array_blk[array_blk_idx];
                            ppn.g.lun = lun;
                            ppn.g.ch = ch;
                            ppn.g.pg = pg;

                            lpn = ssd->u_info.arr_user_blk[ppn.g.blk].lpn.arr_lpn[ppn.g.pg][ppn.g.lun][ppn.g.ch];

                            if (arr_load_segment[get_l2p_segment_idx(lpn)])
                            {
                                update_user_info(ssd,
                                                l2p_table->arr_segment[get_l2p_segment_idx(lpn)]->arr_entry[get_l2p_entry_idx_in_segment(lpn)].ppn,
                                                ppn.ppa,
                                                lpn);

                                l2p_table->arr_segment[get_l2p_segment_idx(lpn)]->arr_entry[get_l2p_entry_idx_in_segment(lpn)].ppn.ppa = ppn.ppa;
                                l2p_table->arr_segment_sequence_num[get_l2p_segment_idx(lpn)] = log_buf->sequence_num;
                            }
                        }  
                    }
                }
            }

            srd.cnt = 0;
            cnt = 0;

            e_l2p_update_time += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s_l2p_update_time;
            s_l2p_write_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            do_sync(ssd);

            for (idx = 0; idx < ssd->total_l2p_segment_in_table; idx++) 
            {
                if (arr_load_segment[idx])
                {
                    srd.type = META_IO;
                    srd.cmd = NAND_WRITE;
                    srd.fua = false;
                    srd.segment_idx = ssd->total_l2p_segment_in_table;
                    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
                    
                    ppn = get_new_meta_ppn(ssd, false);
                    ssd_advance_status(ssd, &ppn, &srd);
                    
                    arr_load_segment[idx] = false;
                    srd.cnt++;
                }
            }

            do_sync(ssd);
            srd.cnt = 0;

            e_l2p_write_time += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s_l2p_write_time;
            s_l2p_read_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        }
    }

    printf("Power Fail Recovery: %ldms  user read: %ldms  l2p read: %ldms  l2p update: %ldms  l2p write: %ldms  L2P Segment Cnt: %d  Scan Page Cnt: %d  Scan Blk Cnt: %d\n", 
            (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s_time) / 1000000,
            e_scan_time / 1000000,
            e_l2p_read_time / 1000000,
            e_l2p_update_time / 1000000,
            e_l2p_write_time / 1000000,
            l2p_segment_cnt,
            page_cnt, 
            array_blk_cnt);
}

uint64_t normal_io(void *arg, uint64_t base_lat)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    ssd->to_ftl = n->to_ftl;
    ssd->to_poller = n->to_poller;

    for (i = 1; i <= n->num_poller; i++) {
        delete_io_buf(ssd);

        if (!ssd->to_ftl[i] || !femu_ring_count(ssd->to_ftl[i]))
            continue;

        rc = femu_ring_dequeue(ssd->to_ftl[i], (void *)&req, 1);
        if (rc != 1) {
            printf("FEMU: FTL to_ftl dequeue failed\n");
        }

        while (ssd->buf.free_buf_cnt < req->nlb) {
            delete_io_buf(ssd);
        }
        
        ftl_assert(req);
        switch (req->cmd.opcode) {
        case NVME_CMD_WRITE:
            lat = ssd_write(ssd, req);
            base_lat = lat;
            break;
        case NVME_CMD_READ:
            lat = ssd_read(ssd, req);

            if (lat != WAITING_LATENCY)
                base_lat = lat;

            break;
        case NVME_CMD_DSM:
            lat = ssd_unmap(ssd, req);
            base_lat = lat;
            break;
        case NVME_CMD_FLUSH:
            lat = ssd_fsync(ssd, req);
            base_lat = lat;
            break;
        default:
            ;//ftl_err("FTL received unkown request type, ERROR\n");
        }

        req->reqlat = lat;
        req->expire_time = lat;

        if (lat == WAITING_LATENCY) {
            rc = femu_ring_enqueue(ssd->to_ftl[i], (void *)&req, 1);
        }
        else {
            if (should_gc(ssd)) do_gc(ssd);
            rc = femu_ring_enqueue(ssd->to_poller[i], (void *)&req, 1);
        }

        if (rc != 1) {
            ftl_err("FTL to_poller enqueue failed\n");
        }
    }

    return base_lat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    uint64_t threshold_lat = 60000000000;
    uint64_t base_lat = 0;
    uint64_t check_lat = 0;

    while (!*(ssd->dataplane_started_ptr)) 
    {
        usleep(100000);
    }

    while (1) 
    {
        base_lat = normal_io(arg, base_lat);
#ifdef HPB
        hpb_io(arg);
#endif

        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) > base_lat)
            check_lat = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - base_lat;

        if (check_lat > threshold_lat)
        {
#ifdef POWER_FAIL_RECOVERY
            ssd_recovery(ssd);
#endif
            base_lat = 0xFFFFFFFFFFFFFFFF;
            check_lat = 0;
        }
    }

    return NULL;
}

void ssd_init_buf(struct ssd *ssd)
{
    struct shared_buf *buf = &ssd->buf;

    buf->head = NULL;
    buf->tail = NULL;

    buf->segment_head = NULL;
    buf->segment_tail = NULL;

    buf->free_buf_cnt = TOTAL_BUF_CNT;
    buf->io_buf_cnt = 0;

    buf->max_map_buf_cnt = TOTAL_MAP_BUF_CNT;
    buf->map_buf_cnt = 0;
}

void ssd_init_meta_info(struct ssd *ssd)
{
    struct meta_info *m_info = &ssd->m_info;

    for (int i = 0; i < ssd->meta_blk_per_plane; i++) {
        m_info->arr_meta_blk[i].blk_idx = ssd->blk_per_plane + i;
        m_info->arr_meta_blk[i].valid_page_count = 0;
        m_info->arr_meta_blk[i].bfree = true;

        for (int lun = 0; lun < LUNS_PER_CH; lun++) {
            for (int ch = 0; ch < CHS; ch++) {
                m_info->arr_meta_blk[i].valid_page_bitmap[lun][ch] = 0;

                for (int pg = 0; pg < PAGES_PER_BLK; pg++) {
                    m_info->arr_meta_blk[i].segment_idx.arr_p2l_segment_idx[pg][lun][ch] = ssd->total_l2p_segment_in_table;
                }
            }
        }
    }

    m_info->blk_idx = ssd->meta_blk_per_plane;
    m_info->free_blk_cnt = ssd->meta_blk_per_plane;
    m_info->meta_pointer = PAGES_PER_BLK_LINE;

    m_info->read_l2p_segment_cnt = 0;
    m_info->transfer_l2p_segment_cnt = 0;

    m_info->map_invalid_cnt = 0;
    m_info->map_hit_cnt = 0;
    m_info->map_read_cnt = 0;
    m_info->host_write_cnt = 0;
    m_info->write_cnt = 0;
    m_info->gc_cnt = 0;
}

void ssd_init_user_info(FemuCtrl *n, struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct gc_blk_info *gc_info = &u_info->gc_info;
    void* vpb_base_space;
    int64_t log_buf_size;
    int64_t meta_size;
    int64_t bs_size;

    bs_size = ((int64_t)n->memsz) * 1024 * 1024;
    meta_size = ((int64_t)n->memsz) * 1024;
    log_buf_size = 16 * 1536;
    vpb_base_space = (int8_t*)n->mbe->logical_space + bs_size + meta_size + log_buf_size;
    u_info->free_age = 0;

    for (int i = 0; i < ssd->blk_per_plane; i++) 
    {
        u_info->arr_user_blk[i].blk_idx = i;
        u_info->arr_user_blk[i].valid_page_count = 0;
        u_info->arr_user_blk[i].bfree = true;
        u_info->arr_user_blk[i].free_age = u_info->free_age;
        u_info->free_age++;

        for (int lun = 0; lun < LUNS_PER_CH; lun++) 
        {
            for (int ch = 0; ch < CHS; ch++) 
            {
                u_info->arr_user_blk[i].valid_page_bitmap[lun][ch] = 0;

                for (int pg = 0; pg < PAGES_PER_BLK; pg++) 
                {
                    u_info->arr_user_blk[i].lpn.arr_lpn[pg][lun][ch] = UNMAPPED_PPA;
                }
            }
        }
    }

    for (int i = 0; i < TOTAL_BLK_PER_PLANE; i++) {
        u_info->bitmap_ppn[i] = UNMAPPED_PPA;
    }


    for (int i = 0; i < TOTAL_REPLAY_BLK_CNT; i++) {
        for (int j = 0; j < TOTAL_REPLAY_BITMAP_CNT; j++)
            u_info->replay_bitmap[i][j] = 0;

        u_info->replay_blk[i] = 0;
    }

    u_info->replay_bitmap_idx = 0;
    u_info->replay_valid_cnt = 0;
    u_info->replay_cur_idx = 0;
    u_info->replay_push_idx = 0;
    u_info->replay_pop_idx = 0;

    u_info->free_blk_cnt = ssd->blk_per_plane;
    u_info->user_pointer = PAGES_PER_BLK_LINE;

    u_info->read_cnt = 0;
    u_info->read_cmd_cnt = 0;
    u_info->write_cnt = 0;
    u_info->nand_write_cnt = 0;
    u_info->nand_read_cnt = 0;
    u_info->gc_cnt = 0;

    gc_info->user_bitmap = (struct valid_page_bitmap*)vpb_base_space;
    gc_info->gc_vpb_request = false;
    gc_info->gc_search_latency = 0;
    gc_info->gc_latency = 0;
    gc_info->total_buf_cnt = 0;
    gc_info->searched_cnt = 0;
    gc_info->scan_idx = 0;
    gc_info->scan_cnt = 0;

    gc_info->next_blk_idx = ssd->blk_per_plane;
    gc_info->blk_idx = ssd->blk_per_plane;
}

void ssd_init_l2p_table(FemuCtrl *n, struct ssd *ssd)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct ppa dst_ppn;
    void* meta_base_space;
    int64_t bs_size;

    bs_size = ((int64_t)n->memsz) * 1024 * 1024;
    meta_base_space = (int8_t*)n->mbe->logical_space + bs_size;

    for (int segment_idx = 0; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) 
    {
        l2p_table->arr_segment[segment_idx] = (struct nand_l2p_segment*)meta_base_space + segment_idx;

        for (int entry_idx = 0; entry_idx < TOTAL_L2P_ENTRY_IN_SEGMENT; entry_idx++) {
            l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx].ppn.ppa = UNMAPPED_PPA;
        }

        dst_ppn = get_new_meta_ppn(ssd, true);
        l2p_table->arr_segment_ppn[segment_idx].ppa = UNMAPPED_PPA;
        l2p_table->arr_segment_write_request[segment_idx] = false;
        update_meta_info(ssd, l2p_table->arr_segment_ppn[segment_idx], dst_ppn, segment_idx);

        l2p_table->arr_segment_ppn[segment_idx] = dst_ppn;
        ssd->m_info.write_cnt++;
    }
}

void ssd_init_log_buffer(FemuCtrl *n, struct ssd *ssd)
{
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    void* log_buf_base_space;
    int64_t meta_size;
    int64_t bs_size;
    int idx = 0;

    bs_size = ((int64_t)n->memsz) * 1024 * 1024;
    meta_size = ((int64_t)n->memsz) * 1024;
    log_buf_base_space = (int8_t*)n->mbe->logical_space + bs_size + meta_size;

    log_buf->log_entry_buf = (struct l2p_entry_in_log_buf*)log_buf_base_space;    
    log_buf->sequence_num = 0;
    log_buf->cur_index = 0;
    log_buf->update_cnt = 0;

    log_buf->transfer_index = 0;
    log_buf->map_write_latency = 0;
    log_buf->complete_index = 0;
    log_buf->multi_transfer_cnt = 0;
    log_buf->multi_transfer_complete_index = 0;
    log_buf->multi_update_l2p_request = false;

    log_buf->log_flush_cnt = 0;
    log_buf->log_flush_segment_cnt = 0;

    for (idx = 0; idx < TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER; idx++) 
    {
        log_buf->log_entry_buf->arr_entry[idx].lpn = 0;
        log_buf->log_entry_buf->arr_entry[idx].ppn.ppa = 0;
        log_buf->log_entry_buf->arr_entry[idx].cnt = 0;
        log_buf->log_entry_buf->arr_entry[idx].sequence_num = 0;
        log_buf->log_entry_buf->arr_entry[idx].src_blk = 0xFFFF;
    }

    for (idx = 0; idx < TOTAL_L2P_SEGMENT_IN_TABLE; idx++)
    {
        log_buf->l2p_table_bit[idx] = false;
    }
}

uint32_t get_l2p_segment_idx(uint32_t lpn)
{
    return lpn / TOTAL_L2P_ENTRY_IN_SEGMENT;
}

uint32_t get_l2p_entry_idx_in_segment(uint32_t lpn)
{
    return lpn % TOTAL_L2P_ENTRY_IN_SEGMENT;
}

struct ppa get_meta_ppn(struct ssd *ssd, uint32_t segment_idx)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    return l2p_table->arr_segment_ppn[segment_idx];
}

struct ppa get_new_meta_ppn(struct ssd *ssd, bool init)
{
    struct meta_info *m_info = &ssd->m_info;
    struct ppa ppn;
    
    if (m_info->meta_pointer == PAGES_PER_BLK_LINE) 
        alloc_meta_blk(ssd, init);

    ppn.ppa = 0;
    ppn.g.blk = m_info->blk_idx;
    ppn.g.lun = m_info->meta_pointer % LUNS_PER_CH;
    ppn.g.ch = (m_info->meta_pointer / LUNS_PER_CH) % CHS;
    ppn.g.pg = m_info->meta_pointer / (LUNS_PER_CH * CHS);

    m_info->meta_pointer++;

    return ppn;
}

struct ppa get_new_user_ppn(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct ppa ppn;
    
    if (u_info->user_pointer == PAGES_PER_BLK_LINE) 
        alloc_user_blk(ssd);

    ppn.ppa = 0;
    ppn.g.blk = u_info->blk_idx;
    ppn.g.lun = u_info->user_pointer % LUNS_PER_CH;
    ppn.g.ch = (u_info->user_pointer / LUNS_PER_CH) % CHS;
    ppn.g.pg = u_info->user_pointer / (LUNS_PER_CH * CHS);

    ftl_assert(ppn.g.blk == u_info->blk_idx);
    ftl_assert(u_info->arr_user_blk[ppn.g.blk].bfree == false);

    u_info->user_pointer++;

    return ppn;
}

void do_sync(struct ssd *ssd)
{
    struct shared_buf *buf = &ssd->buf;

    while (buf->io_buf_cnt != 0) {
        delete_io_buf(ssd);
        delete_map_buf(ssd);
    }
}

void do_sync_erase(struct ssd *ssd, bool init)
{
    if (init == false)
    {
        struct ssd_channel *ch = NULL;
        struct nand_cmd srd;
        struct ppa ppa;
        int ch_idx;

        for (ch_idx = 0; ch_idx < CHS; ch_idx++)
        {
            ppa.ppa = 0;
            ppa.g.ch = ch_idx;

            srd.type = USER_IO;
            srd.cmd = NAND_ERASE;
            srd.fua = false;
            srd.segment_idx = ssd->total_l2p_segment_in_table;
            srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            srd.cnt = 0;
            
            ssd_advance_status(ssd, &ppa, &srd);
        }

        for (ch_idx = 0; ch_idx < CHS; ch_idx++)
        {
            ppa.ppa = 0;
            ppa.g.ch = ch_idx;

            ch = get_ch(ssd, &ppa);

            while (true)
            {
                if (ch->next_ch_avail_time < qemu_clock_get_ns(QEMU_CLOCK_REALTIME))
                {
                    break;
                }
            }
        }
    }
}

void insert_buf(struct ssd *ssd, int64_t cur_ns, uint32_t segment_idx, int type, bool bhost)
{
    struct shared_buf *buf = &ssd->buf;
    buf_node *new_buf_node = (buf_node*)malloc(sizeof(buf_node));

    new_buf_node->done_ns = cur_ns;
    new_buf_node->segment_idx = segment_idx;
    new_buf_node->bhost = bhost;
    new_buf_node->next = NULL;

    buf->free_buf_cnt--;

    if (type == SEGMENT_READ_FOR_USER_READ) {
        if (buf->segment_head == NULL) buf->segment_head = new_buf_node;
        else buf->segment_tail->next = new_buf_node;
        
        buf->segment_tail = new_buf_node;
        buf->map_buf_cnt++;
    }
    else {
        if (buf->head == NULL) buf->head = new_buf_node;
        else buf->tail->next = new_buf_node;

        buf->tail = new_buf_node;
        buf->io_buf_cnt++;
    }

    ftl_assert((buf->free_buf_cnt + buf->map_buf_cnt + buf->io_buf_cnt) == TOTAL_BUF_CNT);

#ifdef DEBUG_DRAMLESS_SSD
    check_list(ssd);
#endif
}

void delete_io_buf(struct ssd *ssd)
{
    struct shared_buf *buf = &ssd->buf;
    buf_node *del_buf_node = buf->head;
    buf_node *prev_buf_node = NULL;
    buf_node *next_buf_node = NULL;

    while (del_buf_node != NULL) {
        next_buf_node = del_buf_node->next;

        if (del_buf_node->done_ns <= qemu_clock_get_ns(QEMU_CLOCK_REALTIME)) {
            if (del_buf_node == buf->head) buf->head = next_buf_node;
            else prev_buf_node->next = next_buf_node;

            if (del_buf_node == buf->tail) buf->tail = prev_buf_node;

            free(del_buf_node);
            buf->free_buf_cnt++;
            buf->io_buf_cnt--;
        }
        else {
            prev_buf_node = del_buf_node;
        }

        del_buf_node = next_buf_node;
    }

    if (buf->tail != NULL) buf->tail->next = NULL;
    ftl_assert((buf->free_buf_cnt + buf->map_buf_cnt + buf->io_buf_cnt) == TOTAL_BUF_CNT);

#ifdef DEBUG_DRAMLESS_SSD
    check_list(ssd);
#endif
}

void delete_map_buf(struct ssd *ssd)
{
    struct shared_buf *buf = &ssd->buf;

    while (buf->map_buf_cnt >= buf->max_map_buf_cnt) {
        buf_node *del_buf_node = buf->segment_head;
        buf_node *next_buf_node = NULL;
        buf_node *prev_buf_node = NULL;

        while (del_buf_node != NULL) {
            next_buf_node = del_buf_node->next;

            if (del_buf_node->done_ns <= qemu_clock_get_ns(QEMU_CLOCK_REALTIME)) {
                if (del_buf_node == buf->segment_head) buf->segment_head = next_buf_node;
                else prev_buf_node->next = next_buf_node;

                if (del_buf_node == buf->segment_tail) buf->segment_tail = prev_buf_node;

                free(del_buf_node);
                buf->free_buf_cnt++;
                buf->map_buf_cnt--;
                break;
            }
            else {
                prev_buf_node = del_buf_node;
            }

            del_buf_node = next_buf_node;
        }

        if (buf->segment_tail != NULL) buf->segment_tail->next = NULL;
        ftl_assert((buf->free_buf_cnt + buf->map_buf_cnt + buf->io_buf_cnt) == TOTAL_BUF_CNT);

#ifdef DEBUG_DRAMLESS_SSD
        check_list(ssd);
#endif
    }
}

void change_map_buf(struct ssd *ssd, uint32_t lpn)
{
    struct shared_buf *buf = &ssd->buf;
    uint32_t segment_idx = get_l2p_segment_idx(lpn);
    buf_node *change_buf_node = buf->segment_head;
    buf_node *next_buf_node = NULL;
    buf_node *prev_buf_node = NULL;

    while (change_buf_node != NULL) {
        next_buf_node = change_buf_node->next;

        if (change_buf_node->segment_idx == segment_idx) {
            if (change_buf_node == buf->segment_tail) break;

            if (change_buf_node == buf->segment_head) buf->segment_head = next_buf_node;
            else prev_buf_node->next = next_buf_node;

            buf->segment_tail->next = change_buf_node;
            buf->segment_tail = change_buf_node;
            break;
        }
        else {
            prev_buf_node = change_buf_node;
        }

        change_buf_node = next_buf_node;
    }

    if (buf->segment_head == NULL) buf->segment_head = buf->segment_tail;
    if (buf->segment_tail != NULL) buf->segment_tail->next = NULL;
    ftl_assert((buf->free_buf_cnt + buf->map_buf_cnt + buf->io_buf_cnt) == TOTAL_BUF_CNT);

#ifdef DEBUG_DRAMLESS_SSD
    check_list(ssd);
#endif
}

uint32_t search_map_buf(struct ssd *ssd, uint32_t lpn)
{
    struct shared_buf *buf = &ssd->buf;
    struct l2p_table *l2p_table = &ssd->l2p_table;
    buf_node *map_buf_node = buf->segment_head;

    int64_t cur_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    uint32_t segment_idx = get_l2p_segment_idx(lpn);
    uint32_t entry_idx = get_l2p_entry_idx_in_segment(lpn);

    while (map_buf_node != NULL) {
        if (map_buf_node->segment_idx == segment_idx) {
            if (map_buf_node->done_ns <= cur_ns)
            {
                change_map_buf(ssd, lpn);
                ftl_assert(segment_idx < ssd->total_l2p_segment_in_table);
                return l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx].ppn.ppa;
            }
            else
            {
                return WAITING_PPA;
            }
        }
        map_buf_node = map_buf_node->next;
    }

    return INVALID_PPA;
}

void gc_meta_blk(struct ssd *ssd)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct meta_info *m_info = &ssd->m_info;
    struct meta_blk *m_blk = NULL;
    struct nand_cmd srd;
    struct ppa ppa;
    uint32_t min_blk_idx = ssd->blk_per_plane + ssd->meta_blk_per_plane;
    uint32_t min_valid_page_count = 0xFFFFFFFF;
    uint32_t total_buf_cnt = 0;
    uint32_t buf_cnt = 0;

    for (int idx = 0; idx < ssd->meta_blk_per_plane; idx++) {
        struct meta_blk *m_blk = &m_info->arr_meta_blk[idx];

        if (m_info->blk_idx != m_blk->blk_idx) {
            if (m_blk->bfree == false) {
                if (m_blk->valid_page_count < min_valid_page_count) {
                    min_valid_page_count = m_blk->valid_page_count;
                    min_blk_idx = m_blk->blk_idx;
                }
            }
        }
    }

    do_sync(ssd);
    total_buf_cnt = ssd->buf.free_buf_cnt;
    ftl_assert(min_blk_idx >= ssd->blk_per_plane);

    m_blk = &m_info->arr_meta_blk[(min_blk_idx - ssd->blk_per_plane)];
    ftl_assert((min_blk_idx - ssd->blk_per_plane) < ssd->meta_blk_per_plane);

    srd.cnt = 0;

    // read src l2p segment
    for (int pg = 0; pg < PAGES_PER_BLK; pg++) {
        for (int ch = 0; ch < CHS; ch++) {
            for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                if (m_blk->valid_page_bitmap[lun][ch] & ((uint64_t)0x1 << pg)) {

                    srd.type = META_IO;
                    srd.cmd = NAND_READ;
                    srd.fua = false;
                    srd.segment_idx = ssd->total_l2p_segment_in_table;
                    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

                    ppa.ppa = 0;
                    ppa.g.blk = min_blk_idx;
                    ppa.g.lun = lun;
                    ppa.g.ch = ch;
                    ppa.g.pg = pg;

                    buf_cnt++;

                    ssd_advance_status(ssd, &ppa, &srd);
                    srd.cnt++;

                    if (buf_cnt >= total_buf_cnt) break;  
                }
            }

            if (buf_cnt >= total_buf_cnt) break;
        }

        if (buf_cnt >= total_buf_cnt) break;
    }

    do_sync(ssd);
    buf_cnt = 0;
    srd.cnt = 0;

    // write dst l2p segment
    for (int pg = 0; pg < PAGES_PER_BLK; pg++) {
        for (int ch = 0; ch < CHS; ch++) {
            for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                if (m_blk->valid_page_bitmap[lun][ch] & ((uint64_t)0x1 << pg)) {
                    uint32_t segment_idx = m_blk->segment_idx.arr_p2l_segment_idx[pg][lun][ch];
                    struct ppa dst_ppa;

                    srd.type = META_IO;
                    srd.cmd = NAND_WRITE;
                    srd.fua = false;
                    srd.segment_idx = ssd->total_l2p_segment_in_table;
                    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

                    ftl_assert(segment_idx < ssd->total_l2p_segment_in_table);
                    ftl_assert(l2p_table->arr_segment_ppn[segment_idx].g.blk == min_blk_idx);
                    ftl_assert(l2p_table->arr_segment_ppn[segment_idx].g.lun == lun);
                    ftl_assert(l2p_table->arr_segment_ppn[segment_idx].g.ch == ch);
                    ftl_assert(l2p_table->arr_segment_ppn[segment_idx].g.pg == pg);

                    dst_ppa = get_new_meta_ppn(ssd, false);
                    update_meta_info(ssd, l2p_table->arr_segment_ppn[segment_idx], dst_ppa, segment_idx);

                    l2p_table->arr_segment_ppn[segment_idx] = dst_ppa;
                    ssd->m_info.gc_cnt++;
                    buf_cnt++;

                    ssd_advance_status(ssd, &dst_ppa, &srd);
                    srd.cnt++;

                    if (buf_cnt >= total_buf_cnt) break;
                }
            }

            if (buf_cnt >= total_buf_cnt) break;
        }

        if (buf_cnt >= total_buf_cnt) break;
    }

    do_sync(ssd);

    if (m_blk->valid_page_count == 0) {
        for (int ch = 0; ch < CHS; ch++) {
            for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                ftl_assert(m_blk->valid_page_bitmap[lun][ch] == 0);
            }
        }

        m_blk->bfree = true;
        m_info->free_blk_cnt++;
    }

    //printf("Meta Gc (free_mblk: %4d  free_ublk: %4d  gc:%8d  blk:%8d)\n", ssd->m_info.free_blk_cnt, ssd->u_info.free_blk_cnt, ssd->m_info.gc_cnt, min_blk_idx);
}

void do_gc_meta_blk(struct ssd *ssd)
{
    struct meta_info *m_info = &ssd->m_info;
    uint64_t stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    while (m_info->free_blk_cnt <= 2) {
        gc_meta_blk(ssd);
    }

    ssd->log_buf.map_write_latency += (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - stime);
}

void alloc_meta_blk(struct ssd *ssd, bool init)
{
    struct meta_info *m_info = &ssd->m_info;
    m_info->blk_idx = ssd->meta_blk_per_plane;

    for (int idx = 0; idx < ssd->meta_blk_per_plane; idx++) {
        struct meta_blk *m_blk = &m_info->arr_meta_blk[idx];

        if (m_blk->bfree == true) {
            for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                for (int ch = 0; ch < CHS; ch++) {
                    ftl_assert(m_blk->valid_page_bitmap[lun][ch] == 0);
                }
            }
            ftl_assert(m_blk->valid_page_count == 0);

            m_blk->bfree = false;

            m_info->blk_idx = m_blk->blk_idx;
            m_info->free_blk_cnt--;
            m_info->meta_pointer = 0;

            break;
        }
    }

    ftl_assert(m_info->blk_idx != ssd->meta_blk_per_plane);

    do_sync_erase(ssd, init);
}

void alloc_user_blk(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct user_blk *u_blk = NULL;
    uint32_t base_age = 0xFFFFFFF;
    uint32_t blk_idx = ssd->blk_per_plane;
    int idx;

    if (u_info->blk_idx < ssd->blk_per_plane) 
        u_info->arr_user_blk[u_info->blk_idx].sequence_num = ssd->log_buf.sequence_num;

    u_info->blk_idx = ssd->blk_per_plane;

    for (idx = 0; idx < ssd->blk_per_plane; idx++) 
    {
        u_blk = &u_info->arr_user_blk[idx];

        if (u_blk->bfree)
        {
            if (u_blk->free_age < base_age)
            {
                ftl_assert(u_blk->valid_page_count == 0);
                base_age = u_blk->free_age;
                blk_idx = u_blk->blk_idx;
            }
        }
    }

    if (blk_idx < ssd->blk_per_plane)
    {
        u_info->arr_user_blk[blk_idx].bfree = false;
        u_info->blk_idx = blk_idx;
        u_info->free_blk_cnt--;
        u_info->user_pointer = 0;

        for (int i = 0; i < TOTAL_SEGMENT_BITMAP_CNT; i++)
        {
            u_info->arr_user_blk[blk_idx].bitmap.segment_bitmap[i] = 0;
        }

#ifdef HPB_PLUS
        for (int i = 0; i < TOTAL_REPLAY_BITMAP_CNT; i++) {
            u_info->replay_bitmap[u_info->replay_push_idx][i] = 0;
        }
        
        u_info->replay_blk[u_info->replay_push_idx] = blk_idx;
        u_info->replay_cur_idx = u_info->replay_push_idx;
        u_info->replay_valid_cnt++;
        u_info->replay_push_idx++;

        ftl_assert (u_info->replay_valid_cnt < TOTAL_REPLAY_BLK_CNT);

        if (u_info->replay_push_idx >= TOTAL_REPLAY_BLK_CNT)
            u_info->replay_push_idx = 0;
#endif
    }

    ftl_assert(u_info->blk_idx != ssd->blk_per_plane);

    do_sync_erase(ssd, false);
}

void insert_log_buffer(struct ssd *ssd, uint64_t lpn, struct ppa *ppa, uint16_t src_blk, uint32_t cnt)
{
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    struct l2p_entry_in_log *entry = &log_buf->log_entry_buf->arr_entry[log_buf->cur_index];
    uint32_t bitmap_offset = get_l2p_segment_idx(lpn) % 32;
    uint32_t bitmap_idx = get_l2p_segment_idx(lpn) / 32;
    bool new_entry = true;

#ifdef DEBUG_DRAMLESS_SSD
    int base_sequence_number = log_buf->log_entry_buf->arr_entry[0].sequence_num;

    for (int idx = 0; idx < log_buf->cur_index; idx++)
    {
        ftl_assert((base_sequence_number + idx) == log_buf->log_entry_buf->arr_entry[idx].sequence_num);
    }
#endif

    ftl_assert(lpn < ssd->total_l2p_segment_in_table * TOTAL_L2P_ENTRY_IN_SEGMENT);

    if (ppa->ppa < INVALID_PPA)
    {
        ftl_assert(ssd->u_info.arr_user_blk[ppa->g.blk].bfree == false);

        ssd->u_info.arr_user_blk[ppa->g.blk].lpn.arr_lpn[ppa->g.pg][ppa->g.lun][ppa->g.ch] = lpn;
        ssd->u_info.arr_user_blk[ssd->u_info.blk_idx].bitmap.segment_bitmap[bitmap_idx] |= ((uint32_t)0x1 << bitmap_offset);
    }
#ifdef HPB_PLUS
    if (ppa->ppa < INVALID_PPA)
        ssd->u_info.replay_bitmap[ssd->u_info.replay_cur_idx][bitmap_idx] |= ((uint32_t)0x1 << bitmap_offset);
#endif

    ftl_assert(get_l2p_segment_idx(lpn) < TOTAL_L2P_SEGMENT_IN_TABLE);
    log_buf->l2p_table_bit[get_l2p_segment_idx(lpn)] = true;

    if (entry->cnt > 0)
    {
        if ((cnt != 0) &&
            (entry->src_blk == src_blk) &&
            (entry->cnt < TOTAL_BOOSTING_CNT) &&
            ((entry->lpn + entry->cnt) == (uint32_t)lpn) &&
            (((entry->lpn + entry->cnt) % TOTAL_L2P_ENTRY_IN_SEGMENT) != 0) &&
            (((entry->ppn.ppa == UNMAPPED_PPA) && (entry->ppn.ppa == ppa->ppa)) ||
             ((entry->ppn.ppa != UNMAPPED_PPA) && ((entry->ppn.ppa + entry->cnt) == ppa->ppa))))
        {
            new_entry = false;
            entry->cnt++;
        }
        else
        {
            log_buf->cur_index++;
        }
    }

    if (new_entry)
    {
        log_buf->log_entry_buf->arr_entry[log_buf->cur_index].lpn = (uint32_t)lpn;
        log_buf->log_entry_buf->arr_entry[log_buf->cur_index].ppn.ppa = ppa->ppa;
        log_buf->log_entry_buf->arr_entry[log_buf->cur_index].src_blk = src_blk;
        log_buf->log_entry_buf->arr_entry[log_buf->cur_index].sequence_num = log_buf->sequence_num;
        log_buf->log_entry_buf->arr_entry[log_buf->cur_index].cnt = 1;
        log_buf->update_cnt++;
        log_buf->sequence_num++;

        ftl_assert (log_buf->cur_index <= TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER);

#ifdef HPB_PLUS
        if (src_blk == INVALID_BLK) 
        {
            if (log_buf->complete_index >= TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_HPB_PLUS_THRESHOLD)
            {
                reset_log_buffer(ssd);
                do_gc_meta_blk(ssd);
            }
        }
#else
        if (log_buf->cur_index > TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_THRESHOLD)
        {
            log_buf->cur_index++;

            do_log_flush(ssd);

            log_buf->log_entry_buf->arr_entry[0].cnt = 0;
            log_buf->cur_index = 0;
        }
#endif
    }
}

uint32_t search_log_buffer(struct ssd *ssd, uint32_t lpn)
{
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    uint32_t cur_idx = log_buf->cur_index;

    if (log_buf->log_entry_buf->arr_entry[0].cnt == 0) {
        return INVALID_PPA;
    }

    for (int idx = cur_idx; idx >= 0 ; idx--) 
    {
        int start_lpn = log_buf->log_entry_buf->arr_entry[idx].lpn;
        int end_lpn = log_buf->log_entry_buf->arr_entry[idx].lpn + log_buf->log_entry_buf->arr_entry[idx].cnt - 1;

        if ((start_lpn <= lpn) && (lpn <= end_lpn))
        {
            return log_buf->log_entry_buf->arr_entry[idx].ppn.ppa + (lpn - start_lpn);
        }
    }

    return INVALID_PPA;
}

void update_meta_info(struct ssd *ssd, struct ppa src_ppn, struct ppa dst_ppn, uint32_t segment_idx)
{
    struct meta_info *m_info = &ssd->m_info;

    ftl_assert(src_ppn.ppa != INVALID_PPA);
    ftl_assert(src_ppn.ppa != WAITING_PPA);
    ftl_assert(dst_ppn.ppa != INVALID_PPA);
    ftl_assert(dst_ppn.ppa != WAITING_PPA);

    if (src_ppn.ppa < INVALID_PPA) {
        uint32_t src_blk_idx = src_ppn.g.blk - ssd->blk_per_plane;

        ftl_assert(src_blk_idx < ssd->meta_blk_per_plane);
        ftl_assert(src_ppn.g.pg < PAGES_PER_BLK);
        ftl_assert(src_ppn.g.lun < LUNS_PER_CH);
        ftl_assert(src_ppn.g.ch < CHS);

        m_info->arr_meta_blk[src_blk_idx].valid_page_bitmap[src_ppn.g.lun][src_ppn.g.ch] &= ~((uint64_t)0x1 << src_ppn.g.pg);
        m_info->arr_meta_blk[src_blk_idx].valid_page_count--;
    }

    if (dst_ppn.ppa < INVALID_PPA) {
        uint32_t dst_blk_idx = dst_ppn.g.blk - ssd->blk_per_plane;

        ftl_assert(dst_blk_idx < ssd->meta_blk_per_plane);
        ftl_assert(dst_ppn.g.pg < PAGES_PER_BLK);
        ftl_assert(dst_ppn.g.lun < LUNS_PER_CH);
        ftl_assert(dst_ppn.g.ch < CHS);
        
        m_info->arr_meta_blk[dst_blk_idx].valid_page_bitmap[dst_ppn.g.lun][dst_ppn.g.ch] |= ((uint64_t)0x1 << dst_ppn.g.pg);
        m_info->arr_meta_blk[dst_blk_idx].valid_page_count++;
        m_info->arr_meta_blk[dst_blk_idx].segment_idx.arr_p2l_segment_idx[dst_ppn.g.pg][dst_ppn.g.lun][dst_ppn.g.ch] = segment_idx;
    }

#ifdef DEBUG_DRAMLESS_SSD
    check_valid_page(ssd);
#endif
}

void update_user_info(struct ssd *ssd, struct ppa src_ppn, uint32_t dst_ppa, uint32_t lpn)
{
    struct user_info *u_info = &ssd->u_info;
    struct ppa dst_ppn;

    dst_ppn.ppa = dst_ppa;
  
    ftl_assert(lpn < ssd->total_l2p_segment_in_table * TOTAL_L2P_ENTRY_IN_SEGMENT);

    if (src_ppn.ppa < INVALID_PPA) {
        uint32_t src_blk_idx = src_ppn.g.blk;

        ftl_assert(src_blk_idx < ssd->blk_per_plane);
        ftl_assert(src_ppn.g.pg < PAGES_PER_BLK);
        ftl_assert(src_ppn.g.lun < LUNS_PER_CH);
        ftl_assert(src_ppn.g.ch < CHS);
        ftl_assert(u_info->arr_user_blk[src_ppn.g.blk].lpn.arr_lpn[src_ppn.g.pg][src_ppn.g.lun][src_ppn.g.ch] == lpn);

        u_info->arr_user_blk[src_blk_idx].valid_page_count--;
    }

    if (dst_ppn.ppa < INVALID_PPA) {
        uint32_t dst_blk_idx = dst_ppn.g.blk;

        ftl_assert(dst_blk_idx < ssd->blk_per_plane);
        ftl_assert(dst_ppn.g.pg < PAGES_PER_BLK);
        ftl_assert(dst_ppn.g.lun < LUNS_PER_CH);
        ftl_assert(dst_ppn.g.ch < CHS);
        ftl_assert(u_info->arr_user_blk[dst_ppn.g.blk].lpn.arr_lpn[dst_ppn.g.pg][dst_ppn.g.lun][dst_ppn.g.ch] == lpn);
        ftl_assert(u_info->arr_user_blk[dst_ppn.g.blk].bfree == false);

        u_info->arr_user_blk[dst_blk_idx].valid_page_count++;
    }
}

void do_log_flush(struct ssd *ssd)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    struct nand_cmd srd;
    struct ppa ppa;
    uint32_t end_idx = ssd->total_l2p_segment_in_table;
    uint32_t cur_start_idx = 0;
    uint32_t cur_end_idx = 0;
    uint32_t buf_cnt = 0;
    uint64_t stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    do_sync(ssd);

    while (cur_end_idx < end_idx)
    {
        srd.cnt = 0;

        // read l2p segment from nand flash memory
        for (int idx = cur_start_idx; idx < end_idx; idx++, cur_end_idx++) 
        {
            ftl_assert(idx < ssd->total_l2p_segment_in_table);

            if (log_buf->l2p_table_bit[idx])
            {
                if (buf_cnt >= TOTAL_INTERLEAVING_FLUSH) break;

                srd.type = META_IO;
                srd.cmd = NAND_READ;
                srd.fua = false;
                srd.segment_idx = ssd->total_l2p_segment_in_table;
                srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
                
                ppa = l2p_table->arr_segment_ppn[idx];
                buf_cnt++;

                ssd_advance_status(ssd, &ppa, &srd);
                srd.cnt++;
            }
        }

        do_sync(ssd);

        srd.cnt = 0;
        // write l2p segment in nand flash memory
        for (int idx = cur_start_idx; idx < cur_end_idx; idx++) 
        {
            if (log_buf->l2p_table_bit[idx])
            {
                struct nand_cmd srd;
                struct ppa dst_ppa;

                srd.type = META_IO;
                srd.cmd = NAND_WRITE;
                srd.fua = false;
                srd.segment_idx = ssd->total_l2p_segment_in_table;
                srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

                dst_ppa = get_new_meta_ppn(ssd, false);
                update_meta_info(ssd, l2p_table->arr_segment_ppn[idx], dst_ppa, idx);
                ssd->m_info.write_cnt++;
                log_buf->log_flush_segment_cnt++;

                l2p_table->arr_segment_ppn[idx] = dst_ppa;
                log_buf->l2p_table_bit[idx] = false;

                ssd_advance_status(ssd, &dst_ppa, &srd);
            }
        }

        do_sync(ssd);
        cur_start_idx = cur_end_idx;
        buf_cnt = 0;
    }

    // update l2p entry
    for (int idx = 0; idx < log_buf->cur_index; idx++) 
    {
        for (int cnt = 0; cnt < log_buf->log_entry_buf->arr_entry[idx].cnt; cnt++) 
        {
            uint32_t segment_idx = get_l2p_segment_idx(log_buf->log_entry_buf->arr_entry[idx].lpn + cnt);
            uint32_t entry_idx_in_segment = get_l2p_entry_idx_in_segment(log_buf->log_entry_buf->arr_entry[idx].lpn + cnt);
            uint32_t src_blk = INVALID_BLK;
            uint32_t dst_ppa = log_buf->log_entry_buf->arr_entry[idx].ppn.ppa;

            if (l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa != UNMAPPED_PPA)
                src_blk = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.g.blk;

            if (dst_ppa != UNMAPPED_PPA)
                dst_ppa = dst_ppa + cnt;

            if ((log_buf->log_entry_buf->arr_entry[idx].src_blk == INVALID_BLK) || (log_buf->log_entry_buf->arr_entry[idx].src_blk == src_blk)) 
            {
                update_user_info(ssd,
                                 l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn,
                                 dst_ppa,
                                 log_buf->log_entry_buf->arr_entry[idx].lpn + cnt);

                if (dst_ppa != UNMAPPED_PPA)
                    l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa = log_buf->log_entry_buf->arr_entry[idx].ppn.ppa + cnt;
                else 
                    l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx_in_segment].ppn.ppa = UNMAPPED_PPA;

                l2p_table->arr_segment_sequence_num[segment_idx] = log_buf->log_entry_buf->arr_entry[idx].sequence_num;
#ifdef HPB
                ssd_update_hpb_key_info(ssd, segment_idx);
#endif
            }
        }
    }

    srd.type = META_IO;
    srd.cmd = NAND_WRITE;
    srd.fua = false;
    srd.segment_idx = ssd->total_l2p_segment_in_table;
    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    ppa = get_new_meta_ppn(ssd, false);
    ssd->u_info.bitmap_ppn[ssd->u_info.blk_idx] = ppa.ppa;
    ssd_advance_status(ssd, &ppa, &srd);

    do_sync(ssd);

    log_buf->log_flush_cnt++;

#ifndef HPB_PLUS
    check_user_free_blk(ssd);
#ifdef DEBUG_DRAMLESS_SSD
    check_user_valid_page(ssd);
#endif
#endif

    do_gc_meta_blk(ssd);

    log_buf->map_write_latency += (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - stime);
    //printf("Flush   (free_mblk: %4d  free_ublk: %4d  wr:%8d)\n", ssd->m_info.free_blk_cnt, ssd->u_info.free_blk_cnt, ssd->m_info.write_cnt);
}

#ifdef HPB
void ssd_init_hpb(struct ssd *ssd)
{
    struct l2p_table *table = &ssd->l2p_table;
    struct key_info *k_info = &table->k_info;

    table->hpb_hit_cnt = 0;

    for (int segment_idx = 0; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) {
        k_info->arr_key_value[segment_idx] = 0;
        k_info->arr_valid[segment_idx] = false;
    }
}

void ssd_update_hpb_key_info(struct ssd *ssd, uint32_t segment_idx)
{
    struct l2p_table *table = &ssd->l2p_table;
    struct key_info *k_info = &table->k_info;

    k_info->arr_key_value[segment_idx] = table->arr_segment_sequence_num[segment_idx];
}

uint64_t ssd_read_l2p_segment(struct ssd *ssd, NvmeRequest *req)
{
    struct nand_cmd srd;
    uint32_t segment_idx = req->slba;
    struct ppa ppa;
    uint64_t lat = 0;

    ftl_assert(segment_idx < ssd->total_l2p_segment_in_table);

    ssd->l2p_table.k_info.arr_valid[segment_idx] = true;
    req->cqe.type |= (0x1 << NVME_READ_L2P_SEGMENT);
#ifdef HPB
    req->cqe.segment_idx_or_src_blk = segment_idx;
    req->cqe.sequence_number = ssd->l2p_table.k_info.arr_key_value[segment_idx];
#endif

#ifdef TEST
        if ((qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - ssd->arraytime) > 1000000000)
        {
            ssd->arraycnt = 0;
        }

        ssd->arraytime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        ssd->arraycnt++;

        if (ssd->arraycnt > 1000000)
        {
            if ((ssd->arraycnt % 10000) == 0)
            {
                printf("ssd->arraycnt : %8d \n", ssd->arraycnt);
            }
        }
        else
        {
            printf("ssd->arraycnt : %8d \n", ssd->arraycnt);
        }
#endif

    ppa = get_meta_ppn(ssd, segment_idx);

    srd.type = META_IO;
    srd.cmd = NAND_READ;
    srd.fua = false;
    srd.segment_idx = ssd->total_l2p_segment_in_table;
    srd.stime = req->stime;
    srd.cnt = 0;
    lat = ssd_advance_status(ssd, &ppa, &srd);

    ssd->m_info.read_l2p_segment_cnt++;
    return lat;
}

uint64_t ssd_transfer_l2p_segment_for_read(struct ssd *ssd, NvmeRequest *req)
{
    uint32_t segment_idx = req->slba;
    uint64_t lat = 0;

    ftl_assert(segment_idx < ssd->total_l2p_segment_in_table);

    if (search_map_buf(ssd, segment_idx * TOTAL_L2P_ENTRY_IN_SEGMENT) == INVALID_PPA)
    {
        if (ssd->buf.map_buf_cnt >= ssd->buf.max_map_buf_cnt) 
        {
            delete_map_buf(ssd);
        }
        insert_buf(ssd, qemu_clock_get_ns(QEMU_CLOCK_REALTIME), segment_idx, SEGMENT_READ_FOR_USER_READ, true);
    }

#ifdef DEBUG_DRAMLESS_SSD
    for (int idx = 0; idx < TOTAL_L2P_ENTRY_IN_SEGMENT; idx++)
    {
        struct l2p_table *l2p_table = &ssd->l2p_table;
        int check_lpn = segment_idx * TOTAL_L2P_ENTRY_IN_SEGMENT + idx;
        struct ppa check_ppa;

        check_ppa.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[idx].ppn.ppa;

        if (l2p_table->arr_segment[segment_idx]->arr_entry[idx].ppn.ppa < INVALID_PPA) 
        {
            ftl_assert(check_lpn == ssd->u_info.arr_user_blk[check_ppa.g.blk].lpn.arr_lpn[check_ppa.g.pg][check_ppa.g.lun][check_ppa.g.ch]);
        }
    }
#endif
    ssd->m_info.transfer_l2p_segment_cnt++;

    return lat;
}

void hpb_io(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;

    ssd->to_hpb = n->to_hpb;
    ssd->to_hpb_poller = n->to_hpb_poller;

    if (!ssd->to_hpb[1] || !femu_ring_count(ssd->to_hpb[1]))
        return;

    rc = femu_ring_dequeue(ssd->to_hpb[1], (void *)&req, 1);
    if (rc != 1) {
        printf("FEMU: FTL to hpb dequeue failed\n");
    }

    while (ssd->buf.free_buf_cnt < req->nlb) {
        delete_io_buf(ssd);
    }

    switch (req->cmd.opcode) {
    case NVME_CMD_HPB_READ:
        lat = ssd_read_l2p_segment(ssd, req);
        break;
    case NVME_CMD_HPB_TRANSFER_SEGMENT:
        lat = ssd_transfer_l2p_segment_for_read(ssd, req);
        break;
#ifdef HPB_PLUS
    case NVME_CMD_HPB_MULTI_L2P_ENTRY:
        lat = ssd_multi_update_l2p_entry(ssd, req);
        break;
    case NVME_CMD_HPB_WRITE_SEGMENT:
        lat = ssd_write_l2p_segment(ssd, req);
        break;
    case NVME_CMD_HPB_TRANSFER_VPB:
        lat = ssd_transfer_gc_vpb(ssd, req);
        break;
#endif
    default:
        ;//ftl_err("FTL received unkown request type, ERROR\n");
    }

    req->reqlat = lat;
    req->expire_time = lat;

    rc = femu_ring_enqueue(ssd->to_hpb_poller[1], (void *)&req, 1);

    if (rc != 1) {
        ftl_err("HPB to_poller enqueue failed\n");
    }
}

void ssd_check_hpb_l2p_entry(struct ssd *ssd, NvmeRequest *req, struct ppa *ppa, uint32_t lpn, uint32_t idx)
{
    uint32_t segment_idx = get_l2p_segment_idx(lpn);

    if ((ppa->ppa == INVALID_PPA) || (ppa->ppa == WAITING_PPA)) 
    {
        if (req->ppa != 0)
        {
#ifdef HPB
#ifdef HPB_PLUS
            if (ssd->l2p_table.k_info.arr_valid[segment_idx])
            {
                ssd->l2p_table.hpb_hit_cnt++;

                if (req->ppa != UNMAPPED_PPA)
                    ppa->ppa = req->ppa + idx;
                else
                    ppa->ppa = req->ppa;
            }
#else
            if (ssd->l2p_table.k_info.arr_valid[segment_idx])
            {
                if (req->key == ssd->l2p_table.k_info.arr_key_value[segment_idx])
                {
                    ssd->l2p_table.hpb_hit_cnt++;
                    ppa->ppa = req->ppa + idx;
                }
            }
#endif
#endif

#ifdef HPB
            if ((ssd->l2p_table.k_info.arr_valid[segment_idx]) &&
                (req->key != ssd->l2p_table.k_info.arr_key_value[segment_idx]))
            {
                ssd->l2p_table.k_info.arr_valid[segment_idx] = false;
                req->cqe.type |= (0x1 << NVME_INVLIAD_SEGMENT);
                req->cqe.segment_idx_or_src_blk = cpu_to_le16(segment_idx);
                ssd->m_info.map_invalid_cnt++;
            }
#endif
        }
    }
}
#endif

#ifdef HPB_PLUS
void ssd_check_hpb_free_blk_idx(struct ssd *ssd, NvmeRequest *req)
{
    uint32_t free_blk_idx = req->ppa;

    if (free_blk_idx != 0xFFFF)
    {
        if (ssd->u_info.gc_info.blk_idx == free_blk_idx)
        {
            ssd->u_info.gc_info.blk_idx = ssd->blk_per_plane;
        }

        ssd->u_info.arr_user_blk[free_blk_idx].bfree = true;
        ssd->u_info.arr_user_blk[free_blk_idx].free_age = ssd->u_info.free_age;
        ssd->u_info.free_blk_cnt++;
        ssd->u_info.free_age++;

        //printf("ssd_check_hpb_free_blk_idx: %d\n", free_blk_idx);
    }
}

void reset_log_buffer(struct ssd *ssd)
{
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    int end_entry_idx = log_buf->cur_index;
    int dst_entry_idx = 0;

    log_buf->cur_index = 0;

    for (int src_entry_idx = TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_HPB_PLUS_THRESHOLD; src_entry_idx <= end_entry_idx; src_entry_idx++) {
        struct l2p_entry_in_log* src_entry = &log_buf->log_entry_buf->arr_entry[src_entry_idx];
        struct l2p_entry_in_log* dst_entry = &log_buf->log_entry_buf->arr_entry[dst_entry_idx];

        dst_entry->lpn = src_entry->lpn;
        dst_entry->ppn = src_entry->ppn;
        dst_entry->sequence_num = src_entry->sequence_num;
        dst_entry->src_blk = src_entry->src_blk;
        dst_entry->cnt = src_entry->cnt;

        log_buf->cur_index++;
        dst_entry_idx++;
    }

    if (log_buf->cur_index == 0)
        log_buf->log_entry_buf->arr_entry[0].cnt = 0;
    else 
        log_buf->cur_index--;

    log_buf->transfer_index -= TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_HPB_PLUS_THRESHOLD;
    log_buf->complete_index -= TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_HPB_PLUS_THRESHOLD;
    log_buf->multi_transfer_complete_index = 0;
    //printf("reset: %d  %d  %d\n", log_buf->cur_index, log_buf->transfer_index, log_buf->complete_index);
}

void ssd_write_segment_request(FemuCtrl *n, NvmeRequest *req)
{
    struct ssd *ssd = n->ssd;
    struct l2p_table* l2p_table = &ssd->l2p_table;
    struct user_info* u_info = &ssd->u_info;
    int replay_pop_idx = u_info->replay_pop_idx;
    int replay_blk_idx = u_info->replay_blk[replay_pop_idx];
    int segment_idx = u_info->replay_bitmap_idx;

    if (u_info->replay_valid_cnt > TOTAL_REPLAY_BLK_CNT_THRESHOLD) 
    {
        for (; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) 
        {
            uint32_t bitmap_offset = segment_idx % 32;
            uint32_t bitmap_idx = segment_idx / 32;

            u_info->replay_bitmap_idx++;

            if (u_info->replay_bitmap[replay_pop_idx][bitmap_idx] & ((uint32_t)0x1 << bitmap_offset))
            {       
                if (l2p_table->arr_segment_write_request[segment_idx] == false)
                {
                    if (u_info->arr_user_blk[replay_blk_idx].sequence_num > l2p_table->arr_segment_sequence_num[segment_idx]) 
                    {
                        req->cqe.type |= (0x1 << NVME_WRITE_L2P_SEGMENT);
                        req->cqe.segment_idx_or_src_blk = segment_idx;
                        l2p_table->arr_segment_write_request[segment_idx] = true;
                        break;
                    }
                }
            }
        }

        if (u_info->replay_bitmap_idx >= ssd->total_l2p_segment_in_table) 
        {
            u_info->replay_bitmap_idx = 0;
            u_info->replay_pop_idx++;
            u_info->replay_valid_cnt--;

            if (u_info->replay_pop_idx >= TOTAL_REPLAY_BLK_CNT)
                u_info->replay_pop_idx = 0;
        }
    }
}

void ssd_complete_sequence_number(FemuCtrl *n, NvmeRequest *req, NvmeRwCmd *rw)
{
    struct ssd *ssd = n->ssd;
    struct l2p_log_buf *log_buf = &ssd->log_buf;
    uint32_t base_sequence_num = log_buf->log_entry_buf->arr_entry[0].sequence_num;
    int idx = 0;

    if (base_sequence_num < req->complete_sequence_number) {
        idx = req->complete_sequence_number - base_sequence_num;
        
        if (idx > log_buf->complete_index) {
            log_buf->complete_index = idx;
        }
    }
}

void ssd_transfer_l2p_entry(FemuCtrl *n, NvmeRequest *req)
{
    struct ssd *ssd = n->ssd;
    struct l2p_log_buf *log_buf = &ssd->log_buf;

    if (req->cqe.type & (0x1 << NVME_UPDATE_MULTI_L2P_ENTRY_COMPLETE))
    {
        return;
    }

    if (log_buf->transfer_index < log_buf->cur_index)
    {
        struct l2p_entry_in_log *entry = &log_buf->log_entry_buf->arr_entry[log_buf->transfer_index];

        if (req->cqe.type & (0x1 << NVME_WRITE_L2P_SEGMENT))
        {
            if (entry->src_blk != INVALID_BLK)
            {
                return;
            }
        }
        else 
        {
            req->cqe.segment_idx_or_src_blk = entry->src_blk;
        }

        req->cqe.type |= (0x1 << NVME_UPDATE_L2P_ENTRY);
        req->cqe.lpn = entry->lpn;
        req->cqe.ppn = entry->ppn.ppa;
        req->cqe.length = entry->cnt;
        req->cqe.sequence_number = entry->sequence_num;
        log_buf->transfer_index++;
    }
}

void ssd_transfer_gc_vpb_request(FemuCtrl *n, NvmeRequest *req)
{
    struct ssd *ssd = n->ssd;
    struct user_info *u_info = &ssd->u_info;
    struct gc_blk_info *gc_info = &u_info->gc_info;

    if (u_info->gc_info.write_cnt > 0)
    {
        if (gc_info->next_blk_idx >= ssd->blk_per_plane)
        {
            if (gc_info->gc_vpb_request == false)
            {
                req->cqe.type |= (0x1 << NVME_TRNSFER_VALID_PAGE_BITMAP_PER_BLK);
                gc_info->gc_vpb_request = true;

                //printf("ssd_transfer_gc_vpb_request\n");
            }
        }
    }
}

void ssd_multi_update_set_queue_entry(FemuCtrl *n, NvmeRequest *req)
{
    struct ssd *ssd = n->ssd;
    struct l2p_log_buf *log_buf = &ssd->log_buf;

    req->cqe.type |= (0x1 << NVME_UPDATE_MULTI_L2P_ENTRY_COMPLETE);
    req->cqe.lpn = 0;
    req->cqe.ppn = log_buf->cur_index;

    log_buf->transfer_index = log_buf->cur_index;
    log_buf->multi_transfer_complete_index = log_buf->complete_index;
}

void ssd_multi_update_l2p_request(FemuCtrl *n, NvmeRequest *req)
{
    struct ssd *ssd = n->ssd;
    struct l2p_log_buf *log_buf = &ssd->log_buf;

    if ((log_buf->cur_index - log_buf->complete_index) > TOTAL_TRANSFER_IDX_THRESHOLD) 
    {
        if ((log_buf->multi_update_l2p_request == false) &&
            (log_buf->complete_index != log_buf->multi_transfer_complete_index))
        {
            log_buf->multi_update_l2p_request = true;
            req->cqe.type |= (0x1 << NVME_UPDATE_MULTI_L2P_ENTRY);
            //printf("ssd_multi_update_l2p_request\n");
        }
    }
}

uint64_t ssd_multi_update_l2p_entry(struct ssd *ssd, NvmeRequest *req)
{
    struct l2p_log_buf *log_buf = &ssd->log_buf;

    log_buf->multi_update_l2p_request = false;
    log_buf->multi_transfer_cnt++;

    //printf("multi    l2p entry: %7d  %7d\n", req->complete_sequence_number, log_buf->complete_index);
    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

uint64_t ssd_write_l2p_segment(struct ssd *ssd, NvmeRequest *req)
{
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct nand_cmd srd;
    struct ppa dst_ppa;
    uint32_t segment_idx = req->slba;
    uint64_t lat;

    ftl_assert(segment_idx < ssd->total_l2p_segment_in_table);

    srd.type = META_IO;
    srd.cmd = NAND_WRITE;
    srd.fua = false;
    srd.segment_idx = ssd->total_l2p_segment_in_table;
    srd.stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    srd.cnt = 0;

#ifdef TEST
        if ((qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - ssd->arraytime) > 1000000000)
        {
            ssd->arraycnt = 0;
        }

        ssd->arraytime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        ssd->arraycnt++;

        if (ssd->arraycnt > 1000000)
        {
            if ((ssd->arraycnt % 10000) == 0)
            {
                printf("ssd->arraycnt : %8d \n", ssd->arraycnt);
            }
        }
        else
        {
            printf("ssd->arraycnt : %8d \n", ssd->arraycnt);
        }
#endif

    dst_ppa = get_new_meta_ppn(ssd, false);
    update_meta_info(ssd, l2p_table->arr_segment_ppn[segment_idx], dst_ppa, segment_idx);

    l2p_table->arr_segment_ppn[segment_idx] = dst_ppa;
    l2p_table->arr_segment_sequence_num[segment_idx] = req->complete_sequence_number;
    l2p_table->arr_segment_write_request[segment_idx] = false;

    lat = ssd_advance_status(ssd, &dst_ppa, &srd);
    ssd->m_info.host_write_cnt++;
    ssd->log_buf.map_write_latency += (NAND_SLC_PROG_LATENCY / CHS / LUNS_PER_CH);

#ifdef DEBUG_DRAMLESS_SSD
    for (int idx = 0; idx < TOTAL_L2P_ENTRY_IN_SEGMENT; idx++)
    {
        int check_lpn = segment_idx * TOTAL_L2P_ENTRY_IN_SEGMENT + idx;
        struct ppa check_ppa;

        check_ppa.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[idx].ppn.ppa;

        if (l2p_table->arr_segment[segment_idx]->arr_entry[idx].ppn.ppa < INVALID_PPA) {
            if (check_lpn != ssd->u_info.arr_user_blk[check_ppa.g.blk].lpn.arr_lpn[check_ppa.g.pg][check_ppa.g.lun][check_ppa.g.ch])
            {
                printf("ppn: %d  %d\n", l2p_table->arr_segment[segment_idx]->arr_entry[idx].ppn.ppa, check_ppa.g.blk);
            }

            ftl_assert(check_lpn == ssd->u_info.arr_user_blk[check_ppa.g.blk].lpn.arr_lpn[check_ppa.g.pg][check_ppa.g.lun][check_ppa.g.ch]);
        }
    }
#endif
    //printf("ssd_write_l2p_segment: %d  %d\n", segment_idx, ssd->journal.valid_journal_cnt);

    return lat;
}

uint64_t ssd_transfer_gc_vpb(struct ssd *ssd, NvmeRequest *req)
{
    struct user_info *u_info = &ssd->u_info;
    struct gc_blk_info *gc_info = &u_info->gc_info;

    gc_info->gc_vpb_request = false;
    gc_info->next_blk_idx = req->slba;

    return qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}
#endif

#ifdef DEBUG_DRAMLESS_SSD
void check_list(struct ssd *ssd)
{
    struct shared_buf *buf = &ssd->buf;
    buf_node *node = buf->head;
    buf_node *segment_node = buf->segment_head;

    int map_buf_cnt = 0;
    int io_buf_cnt = 0;

    while (node != NULL) {
        if (node->next == NULL) {
            ftl_assert(node == buf->tail); 
        }

        node = node->next;
        io_buf_cnt++;
    }

    while (segment_node != NULL) {
        if (segment_node->next == NULL) {
            ftl_assert(segment_node == buf->segment_tail);
        }

        segment_node = segment_node->next;
        map_buf_cnt++;
    }

    ftl_assert(map_buf_cnt == buf->map_buf_cnt);
    ftl_assert(io_buf_cnt == buf->io_buf_cnt);
}

void check_free_valid_cnt(struct ssd *ssd, uint32_t blk_idx)
{
    int segment_idx;
    int entry_idx;

    for (segment_idx = 0; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) 
    {
        for (entry_idx = 0; entry_idx < TOTAL_L2P_ENTRY_IN_SEGMENT; entry_idx++) 
        {
            ftl_assert(ssd->l2p_table.arr_segment[segment_idx]->arr_entry[entry_idx].ppn.g.blk != blk_idx);
        }
    }
}

#ifndef HPB_PLUS
void check_user_valid_page(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;
    struct l2p_table *l2p_table = &ssd->l2p_table;
    struct ppa ppn;
    int valid_user_page_count[TOTAL_L2P_SEGMENT_IN_TABLE] = { 0, };
    int segment_idx;
    int entry_idx;
    int blk_idx;

    for (segment_idx = 0; segment_idx < ssd->total_l2p_segment_in_table; segment_idx++) 
    {
        for (entry_idx = 0; entry_idx < TOTAL_L2P_ENTRY_IN_SEGMENT; entry_idx++) 
        {
            ppn.ppa = l2p_table->arr_segment[segment_idx]->arr_entry[entry_idx].ppn.ppa;

            if (ppn.ppa < INVALID_PPA)
            {
                valid_user_page_count[ppn.g.blk]++;

                if (u_info->arr_user_blk[ppn.g.blk].lpn.arr_lpn[ppn.g.pg][ppn.g.lun][ppn.g.ch] != segment_idx * TOTAL_L2P_ENTRY_IN_SEGMENT + entry_idx) {
                    ftl_assert(u_info->arr_user_blk[ppn.g.blk].lpn.arr_lpn[ppn.g.pg][ppn.g.lun][ppn.g.ch] == segment_idx * TOTAL_L2P_ENTRY_IN_SEGMENT + entry_idx);
                }
            }
        }
    }

    for (blk_idx = 0; blk_idx < ssd->blk_per_plane; blk_idx++) 
    {
        ftl_assert(u_info->arr_user_blk[blk_idx].valid_page_count == valid_user_page_count[blk_idx]);

        for (int pg_idx = 0; pg_idx < PAGES_PER_BLK; pg_idx++) 
        {
            for (int ch = 0; ch < CHS; ch++) 
            {
                for (int lun = 0; lun < LUNS_PER_CH; lun++) 
                {
                    if (u_info->arr_user_blk[blk_idx].lpn.arr_lpn[pg_idx][lun][ch] != UNMAPPED_PPA)
                    {
                        ftl_assert(u_info->arr_user_blk[blk_idx].lpn.arr_lpn[pg_idx][lun][ch] < ssd->total_l2p_segment_in_table * TOTAL_L2P_ENTRY_IN_SEGMENT);
                    }
                }
            }
        }
    }
}
#endif

void check_valid_page(struct ssd *ssd)
{
    struct meta_info *m_info = &ssd->m_info;

    for (int i = 0; i < ssd->meta_blk_per_plane; i++) {
        uint32_t valid_page_count = 0;

        for (int pg = 0; pg < PAGES_PER_BLK; pg++) {
            for (int ch = 0; ch < CHS; ch++) {
                for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                    if (m_info->arr_meta_blk[i].valid_page_bitmap[lun][ch] & ((uint64_t)0x1 << pg)) {
                        valid_page_count++;
                    }
                }
            }
        }

        ftl_assert(valid_page_count == m_info->arr_meta_blk[i].valid_page_count);
    }
}

#ifdef IDEAL
void check_ssd_user_valid_page(struct ssd *ssd)
{
    struct user_info *u_info = &ssd->u_info;

    for (int i = 0; i < ssd->blk_per_plane; i++) {
        uint32_t valid_page_count = 0;

        for (int pg = 0; pg < PAGES_PER_BLK; pg++) {
            for (int ch = 0; ch < CHS; ch++) {
                for (int lun = 0; lun < LUNS_PER_CH; lun++) {
                    if (u_info->arr_user_blk[i].valid_page_bitmap[lun][ch] & ((uint64_t)0x1 << pg)) {
                        valid_page_count++;
                    }
                }
            }
        }

        ftl_assert(valid_page_count == u_info->arr_user_blk[i].valid_page_count);
    }
}
#endif
#endif