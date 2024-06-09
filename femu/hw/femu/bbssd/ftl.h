#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "../nvme.h"

//#define FEMU_DEBUG_FTL
//#define DEBUG_DRAMLESS_SSD

#define INVALID_BLK     (0xFFFF)
#define INVALID_PPA     (0xFFFFFFFD)
#define WAITING_PPA     (0xFFFFFFFE)
#define INVALID_LPN     (0xFFFFFFFF)
#define UNMAPPED_PPA    (0xFFFFFFFF)
#define WAITING_LATENCY (0xFFFFFFFFFFFFFFFF)

enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,

    NAND_READ_LATENCY =  60000,
    NAND_PROG_LATENCY =  550000,
    NAND_ERASE_LATENCY = 5000000,

    NAND_SLC_READ_LATENCY = 25000,
    NAND_SLC_PROG_LATENCY = 150000,
};

enum {
    USER_IO = 0,
    GC_IO = 1,
    META_IO = 2,
    SEGMENT_READ_FOR_USER_READ = 3,
};

enum {
    SEC_FREE = 0,
    SEC_INVALID = 1,
    SEC_VALID = 2,

    PG_FREE = 0,
    PG_INVALID = 1,
    PG_VALID = 2
};

enum {
    FEMU_ENABLE_GC_DELAY = 1,
    FEMU_DISABLE_GC_DELAY = 2,

    FEMU_ENABLE_DELAY_EMU = 3,
    FEMU_DISABLE_DELAY_EMU = 4,

    FEMU_RESET_ACCT = 5,
    FEMU_ENABLE_LOG = 6,
    FEMU_DISABLE_LOG = 7,
};

#define BLK_BITS                            (17)
#define PG_BITS                             (6)
#define SEC_BITS                            (1)
#define PL_BITS                             (2)
#define LUN_BITS                            (3)
#define CH_BITS                             (3)

#define MAX_GB                              (128)
#define TOTAL_L2P_SEGMENT_IN_TABLE          (256 * MAX_GB)
#define TOTAL_BLK_PER_PLANE                 (74 * MAX_GB) // 10%
#define TOTAL_META_BLK_PER_PLANE            (MAX_GB)

#define PAGES_PER_BLK                       (64)
#define VPB_ENTRY                           (8)
#define LUNS_PER_CH                         (8)
#define CHS                                 (8)

#define PAGES_PER_BLK_LINE                  (4096)
#define TOTAL_MAP_BUF_CNT                   (256+16)
#define TOTAL_REPLAY_BLK_CNT                (64)
#define TOTAL_REPLAY_BLK_CNT_THRESHOLD      (10)
#define TOTAL_REPLAY_BITMAP_CNT             (1024) // MAX 128GB

#define TOTAL_POWER_FAIL_RECOVERY_CNT       (2)
#define TOTAL_SEGMENT_BITMAP_CNT            (1024) // MAX 128GB

#define TOTAL_INTERLEAVING_FLUSH            (256+16)
#define TOTAL_BOOSTING_CNT                  (32)
#define TOTAL_L2P_ENTRY_IN_SEGMENT          (1024) // 4B x 1024 = 4MB

#define TOTAL_TRANSFER_IDX_THRESHOLD        (192)
#define TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER   (1024 + 512)
#define TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_THRESHOLD (1024 + 384)
#define TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_GC_THRESHOLD (768)
#define TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER_HPB_PLUS_THRESHOLD (1024)
#define TOTAL_BUF_CNT                       (384) // 4KB x 384 = 1.5 MB

#define GC_THRESHOLD_1                      (2400)
#define GC_THRESHOLD_2                      (1800)
#define GC_THRESHOLD_3                      (600)

/* describe a physical page addr */
struct ppa {
    union {
        struct {
            uint32_t lun : LUN_BITS;
            uint32_t ch  : CH_BITS;
            uint32_t pg  : PG_BITS;
            uint32_t blk : BLK_BITS;
            uint32_t pl  : PL_BITS;
            uint32_t sec : SEC_BITS;
        } g;

        uint32_t ppa;
    };
};

typedef struct buf_node { 
	int64_t done_ns;
    int32_t segment_idx;
    bool bhost;
	struct buf_node* next;
} buf_node;

struct shared_buf {
    int free_buf_cnt;
    int io_buf_cnt;

    int max_map_buf_cnt;
    int map_buf_cnt;

    buf_node* head;
    buf_node* tail;

    buf_node* segment_head;
    buf_node* segment_tail;
};

struct nand_l2p_entry {
    struct ppa ppn;
};

struct nand_l2p_segment {
    struct nand_l2p_entry arr_entry[TOTAL_L2P_ENTRY_IN_SEGMENT];
};

struct nand_p2l_segment_idx {
    uint32_t arr_p2l_segment_idx[PAGES_PER_BLK][LUNS_PER_CH][CHS];
};

#ifdef HPB
struct key_info {
    uint32_t arr_key_value[TOTAL_L2P_SEGMENT_IN_TABLE];
    bool arr_valid[TOTAL_L2P_SEGMENT_IN_TABLE];
};
#endif

struct l2p_table {
    struct nand_l2p_segment* arr_segment[TOTAL_L2P_SEGMENT_IN_TABLE];
    struct ppa arr_segment_ppn[TOTAL_L2P_SEGMENT_IN_TABLE];
    uint32_t arr_segment_sequence_num[TOTAL_L2P_SEGMENT_IN_TABLE];
    bool arr_segment_write_request[TOTAL_L2P_SEGMENT_IN_TABLE];

#ifdef HPB
    struct key_info k_info;
    uint32_t hpb_hit_cnt;
#endif
};

struct meta_blk {
    struct nand_p2l_segment_idx segment_idx;
    uint64_t valid_page_bitmap[LUNS_PER_CH][CHS];
    uint32_t valid_page_count;
    uint32_t blk_idx;
    bool bfree;
};

struct meta_info {
    struct meta_blk arr_meta_blk[TOTAL_META_BLK_PER_PLANE];
    uint32_t free_blk_cnt;
    uint32_t map_invalid_cnt;
    uint32_t map_hit_cnt;
    uint32_t map_read_cnt;
    uint32_t host_write_cnt;
    uint32_t write_cnt;
    uint32_t gc_cnt;

    uint32_t read_l2p_segment_cnt;
    uint32_t transfer_l2p_segment_cnt;

    uint32_t blk_idx;
    uint32_t meta_pointer;
};

struct nand_lpn {
    uint32_t arr_lpn[PAGES_PER_BLK][LUNS_PER_CH][CHS];
};

struct nand_bitmap {
    uint32_t segment_bitmap[TOTAL_SEGMENT_BITMAP_CNT];
};

struct user_blk {
    struct nand_lpn lpn;
    struct nand_bitmap bitmap;
    uint64_t valid_page_bitmap[LUNS_PER_CH][CHS];
    uint32_t valid_page_count;
    uint32_t sequence_num;
    uint32_t free_age;
    uint32_t blk_idx;
    bool bfree;
    bool bdogc;
};

struct valid_page_bitmap {
	uint64_t bitmap[VPB_ENTRY][LUNS_PER_CH][CHS];
};

struct gc_blk_info {
    struct valid_page_bitmap* user_bitmap;
    bool gc_vpb_request;
    int total_buf_cnt;
    int searched_cnt;
    int write_cnt;
    int scan_cnt;
    int scan_idx;

    int next_blk_idx;
    int blk_idx;

    uint64_t gc_search_latency;
    uint64_t gc_latency;
};

struct user_info {
    struct gc_blk_info gc_info;
    struct user_blk arr_user_blk[TOTAL_BLK_PER_PLANE];
    uint32_t bitmap_ppn[TOTAL_BLK_PER_PLANE];

    uint32_t replay_bitmap[TOTAL_REPLAY_BLK_CNT][TOTAL_REPLAY_BITMAP_CNT];
    uint32_t replay_blk[TOTAL_REPLAY_BLK_CNT];
    uint32_t replay_bitmap_idx;
    uint32_t replay_valid_cnt;
    uint32_t replay_cur_idx;
    uint32_t replay_push_idx;
    uint32_t replay_pop_idx;

    uint32_t free_blk_cnt;
    uint32_t read_cmd_cnt;
    uint32_t read_cnt;
    uint32_t write_cnt;
    uint32_t free_age;
    uint32_t gc_cnt;

    uint32_t nand_write_cnt;
    uint32_t nand_read_cnt;

    uint32_t blk_idx;
    uint32_t user_pointer;
};

struct l2p_entry_in_log {
    uint32_t lpn;
    struct ppa ppn;
    uint32_t sequence_num;
    uint16_t cnt;
    uint16_t src_blk;
};

struct l2p_entry_in_log_buf {
    struct l2p_entry_in_log arr_entry[TOTAL_L2P_ENTRY_IN_L2P_LOG_BUFFER];
};

struct l2p_log_buf {
    struct l2p_entry_in_log_buf* log_entry_buf;
    bool l2p_table_bit[TOTAL_L2P_SEGMENT_IN_TABLE];
    uint32_t sequence_num;
    uint32_t cur_index;
    uint32_t update_cnt;

    uint32_t transfer_index;
    uint32_t complete_index;
    uint32_t multi_transfer_cnt;
    uint32_t multi_transfer_complete_index;
    uint64_t map_write_latency;
    bool multi_update_l2p_request;

    uint32_t log_flush_cnt;
    uint32_t log_flush_segment_cnt;
};

struct nand_lun {
    int npls;
    uint64_t next_lun_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssd_channel {
    struct nand_lun *lun;
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssdparams {
    int secsz;        /* sector size in bytes */
    int secs_per_pg;  /* # of sectors per page */
    int pgs_per_blk;  /* # of NAND pages per block */
    int blks_per_pl;  /* # of blocks per plane */
    int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */

    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith
                       */
    bool enable_gc_delay;

    /* below are all calculated values */
    int secs_per_blk; /* # of sectors per block */
    int secs_per_pl;  /* # of sectors per plane */
    int secs_per_lun; /* # of sectors per LUN */
    int secs_per_ch;  /* # of sectors per channel */
    int tt_secs;      /* # of sectors in the SSD */

    int pgs_per_pl;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */

    int blks_per_lun; /* # of blocks per LUN */
    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int secs_per_line;
    int pgs_per_line;
    int blks_per_line;
    int tt_lines;
    int pls_per_ch;   /* # of planes per channel */
    int tt_pls;       /* total # of planes in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */

    struct ppa read_ppa;
    struct ppa meta_write_ppa;
    struct ppa user_write_ppa;
};

struct nand_cmd {
    int type;
    int cmd;
    bool fua;
    uint32_t segment_idx;
    int64_t stime; /* Coperd: request arrival time */
    int32_t cnt;
};

struct ssd {
    char *ssdname;

    uint32_t blk_per_plane;
    uint32_t meta_blk_per_plane;
    uint32_t total_l2p_segment_in_table;
    uint8_t array[131072];
    uint32_t arraycnt;
    uint64_t arraytime;

    struct ssdparams sp;
    struct ssd_channel *ch;

    struct user_info u_info;
    struct meta_info m_info;
    struct l2p_table l2p_table;
    struct shared_buf buf;
    struct l2p_log_buf log_buf;

    /* lockless ring for communication with NVMe IO thread */
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    struct rte_ring **to_hpb;
    struct rte_ring **to_hpb_poller;
    bool *dataplane_started_ptr;
    QemuThread ftl_thread;
};

bool should_gc(struct ssd *ssd);
void increase_gc_cnt(struct ssd *ssd);
void decrease_gc_cnt(struct ssd *ssd);
void ssd_init(FemuCtrl *n);
void gc_read_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t searched_cnt);
void gc_write_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t searched_cnt);
#ifndef HPB_PLUS
uint32_t gc_search_page(struct ssd *ssd, struct ppa *ppa, uint32_t *lpn, uint32_t src_blk, uint32_t total_buf_cnt);
void check_user_free_blk(struct ssd *ssd);
void gc_select_blk(struct ssd *ssd);
#endif
void do_gc(struct ssd *ssd);
uint64_t ssd_read(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_write(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_unmap(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_fsync(struct ssd *ssd, NvmeRequest *req);
void ssd_recovery(struct ssd *ssd);
uint64_t normal_io(void *arg, uint64_t base_lat);
void ssd_init_buf(struct ssd *ssd);
void ssd_init_meta_info(struct ssd *ssd);
void ssd_init_user_info(FemuCtrl *n, struct ssd *ssd);
void ssd_init_l2p_table(FemuCtrl *n, struct ssd *ssd);
void ssd_init_log_buffer(FemuCtrl *n, struct ssd *ssd);
uint32_t get_l2p_segment_idx(uint32_t lpn);
uint32_t get_l2p_entry_idx_in_segment(uint32_t lpn);
struct ppa get_meta_ppn(struct ssd *ssd, uint32_t segment_idx);
struct ppa get_new_meta_ppn(struct ssd *ssd, bool init);
struct ppa get_new_user_ppn(struct ssd *ssd);
void do_sync(struct ssd *ssd);
void do_sync_erase(struct ssd *ssd, bool init);
void insert_buf(struct ssd *ssd, int64_t cur_ns, uint32_t segment_idx, int type, bool bhost);
void delete_io_buf(struct ssd *ssd);
void delete_map_buf(struct ssd *ssd);
void change_map_buf(struct ssd *ssd, uint32_t lpn);
uint32_t search_map_buf(struct ssd *ssd, uint32_t lpn);
void gc_meta_blk(struct ssd *ssd);
void do_gc_meta_blk(struct ssd *ssd);
void alloc_meta_blk(struct ssd *ssd, bool init);
void alloc_user_blk(struct ssd *ssd);
void insert_log_buffer(struct ssd *ssd, uint64_t lpn, struct ppa *ppa, uint16_t src_blk, uint32_t cnt);
uint32_t search_log_buffer(struct ssd *ssd, uint32_t lpn);
void update_meta_info(struct ssd *ssd, struct ppa src_ppn, struct ppa dst_ppn, uint32_t segment_idx);
void update_user_info(struct ssd *ssd, struct ppa src_ppn, uint32_t dst_ppa, uint32_t lpn);
void do_log_flush(struct ssd *ssd);

#ifdef HPB
void ssd_init_hpb(struct ssd *ssd);
void ssd_update_hpb_key_info(struct ssd *ssd, uint32_t segment_idx);
uint64_t ssd_read_l2p_segment(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_transfer_l2p_segment_for_read(struct ssd *ssd, NvmeRequest *req);
void hpb_io(void *arg);
void ssd_check_hpb_l2p_entry(struct ssd *ssd, NvmeRequest *req, struct ppa *ppa, uint32_t lpn, uint32_t idx);
#endif

#ifdef HPB_PLUS
void ssd_check_hpb_free_blk_idx(struct ssd *ssd, NvmeRequest *req);
void reset_log_buffer(struct ssd *ssd);
void ssd_write_segment_request(FemuCtrl *n, NvmeRequest *req);
void ssd_complete_sequence_number(FemuCtrl *n, NvmeRequest *req, NvmeRwCmd *rw);
void ssd_transfer_l2p_entry(FemuCtrl *n, NvmeRequest *req);
void ssd_transfer_gc_vpb_request(FemuCtrl *n, NvmeRequest *req);
void ssd_multi_update_set_queue_entry(FemuCtrl *n, NvmeRequest *req);
void ssd_multi_update_l2p_request(FemuCtrl *n, NvmeRequest *req);
uint64_t ssd_multi_update_l2p_entry(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_write_l2p_segment(struct ssd *ssd, NvmeRequest *req);
uint64_t ssd_transfer_gc_vpb(struct ssd *ssd, NvmeRequest *req);
#endif

#ifdef DEBUG_DRAMLESS_SSD
void check_list(struct ssd *ssd);
void check_free_valid_cnt(struct ssd *ssd, uint32_t blk_idx);
#ifndef HPB_PLUS
void check_user_valid_page(struct ssd *ssd);
#endif
void check_valid_page(struct ssd *ssd);
#ifdef IDEAL
void check_ssd_user_valid_page(struct ssd *ssd);
#endif
#endif



#ifdef FEMU_DEBUG_FTL
#define ftl_debug(fmt, ...) \
    do { printf("[FEMU] FTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] FTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[FEMU] FTL-Log: " fmt, ## __VA_ARGS__); } while (0)


/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif