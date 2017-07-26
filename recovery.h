
typedef long long addr_type;

struct wait_queue {
    int *data;
    addr_type *data2;
    int queue_len, queue_size, queue_head, queue_tail;
};

struct buf_space {
    // long start_stripe;
    int left_stripes;
    char *bufs[MAX_DEVICE_NUM][CACHED_STRIPE_NUM];
    addr_type stripe_id[CACHED_STRIPE_NUM];
    int left_nums[CACHED_STRIPE_NUM];
    int disk_dst[CACHED_STRIPE_NUM];
    long long offset_dst[CACHED_STRIPE_NUM];
};

/*
 * Per input file information
 *
 * @head:   Used to link up on input_files
 * @free_iocbs: List of free iocb's available for use
 * @used_iocbs: List of iocb's currently outstanding
 * @mutex:  Mutex used with condition variable to protect volatile values
 * @cond:   Condition variable used when waiting on a volatile value change
 * @naios_out:  Current number of AIOs outstanding on this context
 * @naios_free: Number of AIOs on the free list (short cut for list_len)
 * @send_wait:  Boolean: When true, the sub thread is waiting on free IOCBs
 * @reap_wait:  Boolean: When true, the rec thread is waiting on used IOCBs
 * @send_done:  Boolean: When true, the sub thread has completed work
 * @reap_done:  Boolean: When true, the rec thread has completed work
 * @sub_thread: Thread used to submit IOs.
 * @rec_thread: Thread used to reclaim IOs.
 * @ctx:    IO context
 * @devnm:  Copy of the device name being managed by this thread
 * @file_name:  Full name of the input file
 * @cpu:    CPU this thread is pinned to
 * @ifd:    Input file descriptor
 * @ofd:    Output file descriptor
 * @iterations: Remaining iterations to process
 * @vfp:    For verbose dumping of actions performed
 */
struct thr_info {
    struct list_head head, free_iocbs, used_iocbs;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile long naios_out, naios_free;
    volatile int send_wait, reap_wait, send_done, reap_done, iter_send_done, wait_all_finish, too_many;
    pthread_t sub_thread, rec_thread;
    io_context_t ctx;
    char *devnm, *file_name;
    int cpu;
    int devices[MAX_DEVICE_NUM];
    FILE *vfp;
    struct addr_info *ainfo;
    struct wait_queue *wq;
    struct buf_space *bs;
    addr_type progress, safe;
};

struct request_info {
    int type;
    int disk_num;
    addr_type offset;
    int size;
    addr_type stripe_id;
};


/*
 * Every Asynchronous IO used has one of these (naios per file/device).
 *
 * @iocb:   IOCB sent down via io_submit
 * @head:   Linked onto file_list.free_iocbs or file_list.used_iocbs
 * @tip:    Pointer to per-thread information this IO is associated with
 * @nbytes: Number of bytes in buffer associated with iocb
 */
struct iocb_pkt {
    struct iocb iocb;
    struct list_head head;
    struct thr_info *tip;
    int nbytes;
    char op;

    addr_type stripe_id;
};

int iocbs_map(struct thr_info *tip, struct iocb **list, struct request_info *pkts, int ntodo, int which_thread);

void fatal(const char *errstring, const int exitval,
             const char *fmt, ...);

__u64 gettime(void);