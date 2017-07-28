#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>

#if !defined(_GNU_SOURCE)
#   define _GNU_SOURCE
#endif

#define ERR_ARGS            1
#define ERR_SYSCALL         2

#define MAX_DEVICE_NUM 128
#define CACHED_STRIPE_NUM 512

#include <getopt.h>

#include "list.h"
// #include "btrecord.h"

#include "recovery.h"
#include "address.h"


/*
 * ========================================================================
 * ==== STRUCTURE DEFINITIONS =============================================
 * ========================================================================
 */



void init_queue(struct wait_queue **wq_ptr, int size) {
    *wq_ptr = (typeof(*wq_ptr)) malloc(sizeof(**wq_ptr));

    struct wait_queue *wq = *wq_ptr;
    wq->queue_len = size;
    wq->queue_size = wq->queue_head = wq->queue_tail = 0;
    wq->data = (typeof(wq->data)) malloc(sizeof(*wq->data) * wq->queue_len);
    wq->data2 = (typeof(wq->data2)) malloc(sizeof(*wq->data2) * wq->queue_len);
}

void destroy_queue(struct wait_queue *wq) {
    free(wq->data);
    free(wq->data2);
    free(wq);
}

void enqueue(struct wait_queue *wq, int disk_num, addr_type offset) {
    if (wq->queue_size == wq->queue_len) {
        fprintf(stderr, "queue overflow!");
        exit(1);
    }
    wq->data[wq->queue_head] = disk_num;
    wq->data2[wq->queue_head] = offset;
    wq->queue_head = (wq->queue_head + 1) % wq->queue_len;
    wq->queue_size++;
}

void dequeue(struct wait_queue *wq, int *disk_num, addr_type *offset) {
    *disk_num = wq->data[wq->queue_tail];
    *offset = wq->data2[wq->queue_tail];
    wq->queue_tail = (wq->queue_tail + 1) % wq->queue_len;
    wq->queue_size--;
}





/*
 * ========================================================================
 * ==== GLOBAL VARIABLES ==================================================
 * ========================================================================
 */

static volatile int signal_done = 0;    // Boolean: Signal'ed, need to quit

static char *ibase = "replay";      // Input base name
static char *idir = ".";        // Input directory base
static int cpus_to_use = -1;        // Number of CPUs to use
static int def_iterations = 1;      // Default number of iterations
static int naios = 256;         // Number of AIOs per thread
static int ncpus = 0;           // Number of CPUs in the system
static int verbose = 0;         // Boolean: Output some extra info
static int write_enabled = 0;       // Boolean: Enable writing
static __u64 genesis = ~0;      // Earliest time seen
static __u64 rgenesis;          // Our start time
static size_t pgsize;           // System Page size
static int nb_sec = 512;        // Number of bytes per sector
static int nfiles = 0;          // Number of files to handle
static int no_stalls = 0;       // Boolean: Disable pre-stalls
static unsigned acc_factor = 1;     // Int: Acceleration factor
static int find_records = 0;        // Boolean: Find record files auto


//default values
static unsigned record_index = 0;
static FILE *output_file = NULL;    //will open out.txt
static FILE *cmd_file = NULL;       //will open cmd.txt
static char *trace_name = NULL;
static char *flag = NULL;
static int type = 0;    //native
static int used_for = 0;    //native mode
static char *cur_trace_name = NULL;
static int def_batch_count = 1;
static __u64 start_clock;
static volatile int iter_done = 0;
static char *device_fn;

/*
 * Variables managed under control of condition variables.
 *
 * n_reclaims_done:     Counts number of reclaim threads that have completed.
 * n_replays_done:  Counts number of replay threads that have completed.
 * n_replays_ready: Counts number of replay threads ready to start.
 * n_iters_done:    Counts number of replay threads done one iteration.
 * iter_start:      Starts an iteration for the replay threads.
 */
static volatile int n_reclaims_done = 0;
static pthread_mutex_t reclaim_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reclaim_done_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_replays_done = 0;
static pthread_mutex_t replay_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replay_done_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_replays_ready = 0;
static pthread_mutex_t replay_ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replay_ready_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_iters_done = 0;
static pthread_mutex_t iter_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t iter_done_cond = PTHREAD_COND_INITIALIZER;

static volatile int iter_start = 0;
static pthread_mutex_t iter_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t iter_start_cond = PTHREAD_COND_INITIALIZER;

static volatile int batch_start = 0;
static pthread_mutex_t batch_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t batch_start_cond = PTHREAD_COND_INITIALIZER;

// static volatile int op_run = 0;
// static pthread_mutex_t op_run_mutex = PTHREAD_MUTEX_INITIALIZER;
// static pthread_cond_t op_run_cond = PTHREAD_COND_INITIALIZER;

// static volatile int op_complete = 0;
// static pthread_mutex_t op_complete_mutex = PTHREAD_MUTEX_INITIALIZER;
// static pthread_cond_t op_complete_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_replays_start = 0;
static pthread_mutex_t replay_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replay_start_cond = PTHREAD_COND_INITIALIZER;

/*
 * ========================================================================
 * ==== FORWARD REFERENECES ===============================================
 * ========================================================================
 */

static void *replay_sub(void *arg);
static void *replay_rec(void *arg);
static char usage_str[];

/*
 * ========================================================================
 * ==== INLINE ROUTINES ===================================================
 * ========================================================================
 */

/*
 * The 'fatal' macro will output a perror message (if errstring is !NULL)
 * and display a string (with variable arguments) and then exit with the
 * specified exit value.
 */

void fatal(const char *errstring, const int exitval,
           const char *fmt, ...) {
    va_list ap;

    if (errstring)
        perror(errstring);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(exitval);
    /*NOTREACHED*/
}

static inline long long unsigned du64_to_sec(__u64 du64) {
    return (long long unsigned)du64 / (1000 * 1000 * 1000);
}

static inline long long unsigned du64_to_nsec(__u64 du64) {
    return llabs((long long)du64) % (1000 * 1000 * 1000);
}

/**
 * min - Return minimum of two integers
 */
static inline int min(int a, int b) {
    return a < b ? a : b;
}

/**
 * minl - Return minimum of two longs
 */
static inline long minl(long a, long b) {
    return a < b ? a : b;
}


/**
 * is_send_done - Returns true if sender should quit early
 * @tip: Per-thread information
 */
static inline int is_send_done(struct thr_info *tip) {
    return signal_done || tip->send_done;
}

/**
 * is_reap_done - Returns true if reaper should quit early
 * @tip: Per-thread information
 */
static inline int is_reap_done(struct thr_info *tip) {
    return signal_done || (tip->iter_send_done && tip->naios_out == 0);
}

/**
 * ts2ns - Convert timespec values to a nanosecond value
 */
#define NS_TICKS        ((__u64)1000 * (__u64)1000 * (__u64)1000)
static inline __u64 ts2ns(struct timespec *ts) {
    return ((__u64)(ts->tv_sec) * NS_TICKS) + (__u64)(ts->tv_nsec);
}

/**
 * ts2ns - Convert timeval values to a nanosecond value
 */
static inline __u64 tv2ns(struct timeval *tp) {
    return ((__u64)(tp->tv_sec)) + ((__u64)(tp->tv_usec) * (__u64)1000);
}

/**
 * touch_memory - Force physical memory to be allocating it
 *
 * For malloc()ed memory we need to /touch/ it to make it really
 * exist. Otherwise, for write's (to storage) things may not work
 * as planned - we see Linux just use a single area to /read/ from
 * (as there isn't any memory that has been associated with the
 * allocated virtual addresses yet).
 */
static inline void touch_memory(char *buf, size_t bsize) {
#if defined(PREP_BUFS)
    memset(buf, 0, bsize);
#else
    size_t i;

    for (i = 0; i < bsize; i += pgsize)
        buf[i] = 0;

#endif
}

/**
 * buf_alloc - Returns a page-aligned buffer of the specified size
 * @nbytes: Number of bytes to allocate
 */
static inline void *buf_alloc(size_t nbytes) {
    void *buf;

    if (posix_memalign(&buf, pgsize, nbytes)) {
        fatal("posix_memalign", ERR_SYSCALL, "Allocation failed\n");
        /*NOTREACHED*/
    }

    return buf;
}

/**
 * gettime - Returns current time
 */
inline __u64 gettime(void) {
    static int use_clock_gettime = -1;      // Which clock to use

    if (use_clock_gettime < 0) {
        use_clock_gettime = clock_getres(CLOCK_MONOTONIC, NULL) == 0;

        if (use_clock_gettime) {
            struct timespec ts = {
                .tv_sec = 0,
                .tv_nsec = 0
            };
            clock_settime(CLOCK_MONOTONIC, &ts);
        }
    }

    if (use_clock_gettime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts2ns(&ts);

    } else {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        return tv2ns(&tp);
    }
}

/**
 * setup_signal - Set up a signal handler for the specified signum
 */
static inline void setup_signal(int signum, sighandler_t handler) {
    if (signal(signum, handler) == SIG_ERR) {
        fatal("signal", ERR_SYSCALL, "Failed to set signal %d\n",
              signum);
        /*NOTREACHED*/
    }
}

/*
 * ========================================================================
 * ==== CONDITION VARIABLE ROUTINES =======================================
 * ========================================================================
 */

/**
 * __set_cv - Increments a variable under condition variable control.
 * @pmp:    Pointer to the associated mutex
 * @pcp:    Pointer to the associated condition variable
 * @vp:     Pointer to the variable being incremented
 * @mxv:    Max value for variable (Used only when ASSERTS are on)
 */
static inline void __set_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                            volatile int *vp,
                            __attribute__((__unused__))int mxv) {
    pthread_mutex_lock(pmp);
    assert(*vp < mxv);
    *vp += 1;
    pthread_cond_signal(pcp);
    pthread_mutex_unlock(pmp);
}

/**
 * __wait_cv - Waits for a variable under cond var control to hit a value
 * @pmp:    Pointer to the associated mutex
 * @pcp:    Pointer to the associated condition variable
 * @vp:     Pointer to the variable being incremented
 * @mxv:    Value to wait for
 */
static inline void __wait_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                             volatile int *vp, int mxv) {
    pthread_mutex_lock(pmp);

    while (*vp < mxv)
        pthread_cond_wait(pcp, pmp);

    *vp = 0;
    pthread_mutex_unlock(pmp);
}

static inline void set_reclaim_done(void) {
    __set_cv(&reclaim_done_mutex, &reclaim_done_cond, &n_reclaims_done,
             nfiles);
}

static inline void wait_reclaims_done(void) {
    __wait_cv(&reclaim_done_mutex, &reclaim_done_cond, &n_reclaims_done,
              nfiles);
}

static inline void set_replay_ready(void) {
    __set_cv(&replay_ready_mutex, &replay_ready_cond, &n_replays_ready,
             nfiles);
}

static inline void wait_replays_ready(void) {
    __wait_cv(&replay_ready_mutex, &replay_ready_cond, &n_replays_ready,
              nfiles);
}

static inline void set_replay_done(void) {
    __set_cv(&replay_done_mutex, &replay_done_cond, &n_replays_done,
             nfiles);
}

static inline void wait_replays_done(void) {
    __wait_cv(&replay_done_mutex, &replay_done_cond, &n_replays_done,
              nfiles);
}

static inline void set_iter_done(void) {
    __set_cv(&iter_done_mutex, &iter_done_cond, &n_iters_done,
             nfiles);
}

static inline void wait_iters_done(void) {
    __wait_cv(&iter_done_mutex, &iter_done_cond, &n_iters_done,
              nfiles);
}

static inline void set_replay_start(void) {
    __set_cv(&replay_start_mutex, &replay_start_cond, &n_replays_start,
             nfiles);
}

static inline void wait_replays_start(void) {
    __wait_cv(&replay_start_mutex, &replay_start_cond, &n_replays_start,
              nfiles);
}

/**
 * wait_iter_start - Wait for an iteration to start
 *
 * This is /slightly/ different: we are waiting for a value to become
 * non-zero, and then we decrement it and go on.
 */
static inline void wait_iter_start(void) {
    pthread_mutex_lock(&iter_start_mutex);

    while (iter_start == 0)
        pthread_cond_wait(&iter_start_cond, &iter_start_mutex);

    assert(1 <= iter_start && iter_start <= nfiles);

    if (verbose > 1)
        fprintf(stderr, "\nleft %d file(s) to process\n", iter_start);

    iter_start--;
    pthread_mutex_unlock(&iter_start_mutex);
}

/**
 * start_iter - Start an iteration at the replay thread level
 */
static inline void start_iter(void) {
    pthread_mutex_lock(&iter_start_mutex);
    assert(iter_start == 0);
    iter_start = nfiles;
    pthread_cond_broadcast(&iter_start_cond);
    pthread_mutex_unlock(&iter_start_mutex);
}


static inline void wait_batch_start(void) {
    pthread_mutex_lock(&batch_start_mutex);

    while (batch_start == 0)
        pthread_cond_wait(&batch_start_cond, &batch_start_mutex);

    assert(1 <= batch_start && batch_start <= nfiles);

    if (verbose > 1)
        fprintf(stderr, "\nleft %d file(s) to process\n", batch_start);

    batch_start--;
    pthread_mutex_unlock(&batch_start_mutex);
}

/**
 * start_batch - Start an batch at the replay thread level
 */
static inline void start_batch(void) {
    pthread_mutex_lock(&batch_start_mutex);
    assert(batch_start == 0);
    batch_start = nfiles;
    pthread_cond_broadcast(&batch_start_cond);
    pthread_mutex_unlock(&batch_start_mutex);
}


/*
 * ========================================================================
 * ==== CPU RELATED ROUTINES ==============================================
 * ========================================================================
 */

/**
 * get_ncpus - Sets up the global 'ncpus' value
 */
static void get_ncpus(void) {
#ifdef _SC_NPROCESSORS_ONLN
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
#else
    int nrcpus = 4096;
    cpu_set_t *cpus;

realloc:
    cpus = CPU_ALLOC(nrcpus);
    size = CPU_ALLOC_SIZE(nrcpus);
    CPU_ZERO_S(size, cpus);

    if (sched_getaffinity(0, size, cpus)) {
        if( errno == EINVAL && nrcpus < (4096 << 4) ) {
            CPU_FREE(cpus);
            nrcpus <<= 1;
            goto realloc;
        }

        fatal("sched_getaffinity", ERR_SYSCALL, "Can't get CPU info\n");
        /*NOTREACHED*/
    }

    ncpus = -1;

    for (last_cpu = 0; last_cpu < CPU_SETSIZE && CPU_ISSET(last_cpu, &cpus); last_cpu++)
        if (CPU_ISSET( last_cpu, &cpus) )
            ncpus = last_cpu;

    ncpus++;
    CPU_FREE(cpus);
#endif

    if (ncpus == 0) {
        fatal(NULL, ERR_SYSCALL, "Insufficient number of CPUs\n");
        /*NOTREACHED*/
    }
}

/**
 * pin_to_cpu - Pin this thread to a specific CPU
 * @tip: Thread information
 */
static void pin_to_cpu(struct thr_info *tip) {
    cpu_set_t *cpus;
    size_t size;

    cpus = CPU_ALLOC(ncpus);
    size = CPU_ALLOC_SIZE(ncpus);

    assert(0 <= tip->cpu && tip->cpu < ncpus);

    CPU_ZERO_S(size, cpus);
    CPU_SET_S(tip->cpu, size, cpus);

    if (sched_setaffinity(0, size, cpus)) {
        fatal("sched_setaffinity", ERR_SYSCALL, "Failed to pin CPU\n");
        /*NOTREACHED*/
    }

    assert(tip->cpu == sched_getcpu());

    if (verbose > 1) {
        int i;
        cpu_set_t *now = CPU_ALLOC(ncpus);

        (void)sched_getaffinity(0, size, now);
        fprintf(tip->vfp, "Pinned to CPU %02d ", tip->cpu);

        for (i = 0; i < ncpus; i++)
            fprintf(tip->vfp, "%1d", CPU_ISSET_S(i, size, now));

        fprintf(tip->vfp, "\n");
    }
}


/*
 * ========================================================================
 * ==== IOCB MANAGEMENT ROUTINES ==========================================
 * ========================================================================
 */

/**
 * iocb_init - Initialize the fields of an IOCB
 * @tip: Per-thread information
 * iocbp: IOCB pointer to update
 */
static void iocb_init(struct thr_info *tip, struct iocb_pkt *iocbp) {
    iocbp->tip = tip;
    iocbp->nbytes = 0;
    iocbp->iocb.u.c.buf = NULL;
}

/**
 * iocb_setup - Set up an iocb with this AIOs information
 * @iocbp: IOCB pointer to update
 * @rw: Direction (0 == write, 1 == read)
 * @n: Number of bytes to transfer
 * @off: Offset (in bytes)
 */
static void iocb_setup(struct iocb_pkt *iocbp, int device, int rw, int n, long long off) {
    char *buf;
    struct iocb *iop = &iocbp->iocb;

    assert(rw == 0 || rw == 1);
    assert(0 < n && (n % nb_sec) == 0);
    assert(0 <= off);

    if (iocbp->nbytes) {
        if (iocbp->nbytes >= n) {
            buf = iop->u.c.buf;
            goto prep;
        }

        assert(iop->u.c.buf);
        free(iop->u.c.buf);
    }

    buf = buf_alloc(n);
    iocbp->nbytes = n;
    // printf("op: %d, device: %d, size: %d, off: %ld\n", iop, device, n, off);
prep:

    if (rw)
        io_prep_pread(iop, device, buf, n, off);

    else {
        assert(write_enabled);
        io_prep_pwrite(iop, device, buf, n, off);
        touch_memory(buf, n);
    }

    iop->data = iocbp;
}

/*
 * ========================================================================
 * ==== PER-THREAD SET UP & TEAR DOWN =====================================
 * ========================================================================
 */

void open_devices(struct thr_info *tip, char *device_fn) {
    FILE *fin = fopen(device_fn, "r");
    char s[128];
    int i = 0;

    while (fgets(s, 128, fin) != NULL) {
        if (strlen(s) != 1) {
            s[strlen(s) - 1] = '\0';

            struct stat rawst;
            int rst0 = stat(s, &rawst);

            if ((rst0 == 0) && S_ISBLK(rawst.st_mode)) {    // blk device
                int blk_flags = O_RDWR | O_LARGEFILE | O_SYNC | O_DIRECT;
                tip->devices[i] = open(s, blk_flags);

                if (tip->devices[i] < 0) {
                    fprintf(stderr, "open '%s' error !\n", s);
                    exit(1);
                }

            } else { // is a normal file anyway
                int blk_flags = O_CREAT | O_RDWR | O_LARGEFILE;
                tip->devices[i] = open(s, blk_flags);

                if (tip->devices[i] < 0) {
                    fprintf(stderr, "open '%s' error !\n", s);
                    exit(1);
                }
            }

            i++;
        }
    }

    fclose(fin);
}

void close_devices(struct thr_info *tip, char *device_fn) {
    FILE *fin = fopen(device_fn, "r");
    char s[128];
    int i = 0;

    while (fgets(s, 128, fin) != NULL) {
        if (strlen(s) != 1) {
            close(tip->devices[i++]);
        }
    }

    fclose(fin);
}

void init_buf_space(struct buf_space **bs_ptr) {
    *bs_ptr = (typeof(*bs_ptr)) malloc(sizeof(**bs_ptr));
    struct buf_space *bs = *bs_ptr;

}

/**
 * tip_init - Per thread initialization function
 */
static void tip_init(struct thr_info *tip) {
    int i;

    INIT_LIST_HEAD(&tip->free_iocbs);
    INIT_LIST_HEAD(&tip->used_iocbs);

    pthread_mutex_init(&tip->mutex, NULL);
    pthread_cond_init(&tip->cond, NULL);

    if (io_setup(naios, &tip->ctx)) {
        fatal("io_setup", ERR_SYSCALL, "io_setup failed\n");
        /*NOTREACHED*/
    }

    tip->cpu = 0;
    tip->naios_out = 0;
    tip->send_done = tip->reap_done = tip->iter_send_done = 0;
    tip->send_wait = tip->reap_wait = tip->wait_all_finish = tip->too_many = 0;

    memset(&tip->sub_thread, 0, sizeof(tip->sub_thread));
    memset(&tip->rec_thread, 0, sizeof(tip->rec_thread));

    for (i = 0; i < naios; i++) {
        struct iocb_pkt *iocbp = buf_alloc(sizeof(*iocbp));

        iocb_init(tip, iocbp);
        list_add_tail(&iocbp->head, &tip->free_iocbs);
    }

    tip->naios_free = naios;

    open_devices(tip, device_fn);
    init_addr_info(tip->ainfo);
    init_queue(&tip->wq, 10240);
    init_buf_space(&tip->bs);

    if (pthread_create(&tip->sub_thread, NULL, replay_sub, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
              "thread create failed\n");
        /*NOTREACHED*/
    }

    if (pthread_create(&tip->rec_thread, NULL, replay_rec, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
              "thread create failed\n");
        /*NOTREACHED*/
    }
}

/**
 * tip_release - Release resources associated with this thread
 */
static void tip_release(struct thr_info *tip) {
    struct list_head *p, *q;

    assert(tip->send_done);
    assert(tip->reap_done);
    assert(list_len(&tip->used_iocbs) == 0);
    assert(tip->naios_free == naios);

    if (pthread_join(tip->sub_thread, NULL)) {
        fatal("pthread_join", ERR_SYSCALL, "pthread sub join failed\n");
        /*NOTREACHED*/
    }

    if (pthread_join(tip->rec_thread, NULL)) {
        fatal("pthread_join", ERR_SYSCALL, "pthread rec join failed\n");
        /*NOTREACHED*/
    }

    io_destroy(tip->ctx);

    list_splice(&tip->used_iocbs, &tip->free_iocbs);
    list_for_each_safe(p, q, &tip->free_iocbs) {
        struct iocb_pkt *iocbp = list_entry(p, struct iocb_pkt, head);

        list_del(&iocbp->head);

        if (iocbp->nbytes)
            free(iocbp->iocb.u.c.buf);

        free(iocbp);
    }

    destroy_addr_info(tip->ainfo);
    close_devices(tip, device_fn);
    destroy_queue(tip->wq);

    pthread_cond_destroy(&tip->cond);
    pthread_mutex_destroy(&tip->mutex);
}


/**
 * rem_input_file - Release resources associated with an input file
 * @tip: Per-input file information
 */
static void rem_input_file(struct thr_info *tip) {
    tip_release(tip);
}



/*
 * ========================================================================
 * ==== RECLAIM ROUTINES ==================================================
 * ========================================================================
 */

/**
 * reap_wait_aios - Wait for and return number of outstanding AIOs
 *
 * Will return 0 if we are done
 */
static int reap_wait_aios(struct thr_info *tip) {
    int naios = 0;

    if (!is_reap_done(tip)) {
        pthread_mutex_lock(&tip->mutex);

        while (tip->naios_out == 0 && !tip->iter_send_done && !tip->send_done && tip->wq->queue_size == 0) {
            tip->reap_wait = 1;
            // fprintf(stderr, "in reap_wait_aios: before waiting, send_done %d\n", tip->send_done);

            if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                fatal("pthread_cond_wait", ERR_SYSCALL,
                      "nfree_current cond wait failed\n");
                /*NOTREACHED*/
            }
        }

        if (tip->wq->queue_size > 0) {
            pthread_mutex_unlock(&tip->mutex);
            write_process(tip);
            pthread_mutex_lock(&tip->mutex);
        }

        naios = tip->naios_out;
        pthread_mutex_unlock(&tip->mutex);
    }

    assert(is_reap_done(tip) || naios > 0);

    return is_reap_done(tip) ? 0 : naios;
}

void write_process(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    struct request_info reqs[MAX_DEVICE_NUM];


    while (tip->wq->queue_size > 0) {
        int ntodo = 1;
        int disk_num;
        addr_type offset;
        dequeue(tip->wq, &disk_num, &offset);

        reqs[0].type = 0;
        reqs[0].disk_num = disk_num;
        reqs[0].offset = offset;
        reqs[0].size = tip->ainfo->strip_size;
        reqs[0].stripe_id = 0;

        int ok = iocbs_map(tip, list, reqs, ntodo, 1);

        if (!ok) {
            enqueue(tip->wq, disk_num, offset);
            break;
        }

        int ndone = io_submit(tip->ctx, ntodo, list);

        if (ndone != (long)ntodo) {
            fatal("io_submit", ERR_SYSCALL,
                  "%d: io_submit(%d:%ld) failed in write (%s)\n",
                  tip->cpu, ntodo, ndone,
                  strerror(labs(ndone)));
            /*NOTREACHED*/
        }

        pthread_mutex_lock(&tip->mutex);
        tip->naios_out += ndone;
        pthread_mutex_unlock(&tip->mutex);
    }

}

/**
 * reclaim_ios - Reclaim AIOs completed, recycle IOCBs
 * @tip: Per-thread information
 * @naios_out: Number of AIOs we have outstanding (min)
 */
static void reclaim_ios(struct thr_info *tip, long naios_out) {
    long i, ndone;
    struct io_event *evp, events[naios_out];

again:
    assert(naios > 0);

    for (;;) {
        ndone = io_getevents(tip->ctx, 1, naios_out, events, NULL);

        if (ndone > 0)
            break;

        if (errno && errno != EINTR) {
            fatal("io_getevents", ERR_SYSCALL,
                  "io_getevents failed\n");
            /*NOTREACHED*/
        }
    }

    for (i = 0, evp = events; i < ndone; i++, evp++) {
        struct iocb_pkt *iocbp = evp->data;

        if (evp->res != iocbp->iocb.u.c.nbytes) {
            fatal(NULL, ERR_SYSCALL,
                  "Event failure %ld/%ld\t(%ld + %ld)\n",
                  (long)evp->res, (long)evp->res2,
                  (long)iocbp->iocb.u.c.offset / nb_sec,
                  (long)iocbp->iocb.u.c.nbytes / nb_sec);
            /*NOTREACHED*/

        } else if (iocbp->op == 'R') {
            if (iocbp->stripe_id != -1) {
                tip->bs->left_nums[iocbp->stripe_id]--;

                if (tip->bs->left_nums[iocbp->stripe_id] == 0) {
                    if (tip->wq->queue_size == tip->wq->queue_len) {
                        fprintf(stderr, "group_wait_queue overflow!\n");
                        exit(1);
                    }

                    enqueue(tip->wq, tip->bs->disk_dst[iocbp->stripe_id], tip->bs->offset_dst[iocbp->stripe_id]);
                    write_process(tip);
                }
            }

        } else {
            if (iocbp->stripe_id != -1) {
                tip->bs->left_stripes--;
            }
        }

        pthread_mutex_lock(&tip->mutex);
        list_move_tail(&iocbp->head, &tip->free_iocbs);
        tip->naios_free++;
        tip->naios_out--;
        tip->rcount++;
        pthread_mutex_unlock(&tip->mutex);
    }

    pthread_mutex_lock(&tip->mutex);
    // tip->naios_free += ndone;
    // tip->naios_out -= ndone;
    naios_out = minl(naios_out, tip->naios_out);

    if (tip->send_wait && tip->wait_all_finish == 0) {
        tip->send_wait = 0;
        pthread_cond_signal(&tip->cond);

    } else if (tip->send_wait && tip->bs->left_stripes == 0) {
        fprintf(stderr, "wakeup, next region\n");
        tip->wait_all_finish = 0;
        tip->send_wait = 0;
        pthread_cond_signal(&tip->cond);
    }

    pthread_mutex_unlock(&tip->mutex);

    /*
     * Short cut: If we /know/ there are some more AIOs, go handle them
     */
    if (naios_out)
        goto again;
}

/**
 * replay_rec - Worker thread to reclaim AIOs
 * @arg: Pointer to thread information
 */
static void *replay_rec(void *arg) {
    long naios_out;
    struct thr_info *tip = arg;

    while (!is_send_done(tip)) {

        while ((naios_out = reap_wait_aios(tip)) > 0)
            reclaim_ios(tip, naios_out);

        set_iter_done();
    }

    assert(tip->send_done);
    tip->reap_done = 1;
    set_reclaim_done();

    return NULL;
}

/*
 * ========================================================================
 * ==== REPLAY ROUTINES ===================================================
 * ========================================================================
 */


/**
 * nfree_current - Returns current number of AIOs that are free
 *
 * Will wait for available ones...
 *
 * Returns 0 if we have some condition that causes us to exit
 */
static int nfree_current(struct thr_info *tip) {
    int nfree = 0;

    pthread_mutex_lock(&tip->mutex);

    while (!is_send_done(tip) && ((nfree = tip->naios_free) < 1)) {
        tip->send_wait = 1;

        // fprintf(stderr, "error? send thread wait now!\n");
        if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
            fatal("pthread_cond_wait", ERR_SYSCALL,
                  "nfree_current cond wait failed\n");
            /*NOTREACHED*/
        }
    }

    // tip->naios_free--;
    pthread_mutex_unlock(&tip->mutex);

    return nfree;
}



/**
 * iocbs_map - Map a set of AIOs onto a set of IOCBs
 * @tip: Per-thread information
 * @list: List of AIOs created
 * @pkts: AIOs to map
 * @ntodo: Number of AIOs to map
 */
int iocbs_map(struct thr_info *tip, struct iocb **list,
              struct request_info *pkts, int ntodo, int which_thread) {
    int i;
    struct request_info *pkt;

    assert(0 < ntodo && ntodo <= naios);

    pthread_mutex_lock(&tip->mutex);

    if (which_thread == 1 && tip->naios_free < ntodo) {
        pthread_mutex_unlock(&tip->mutex);
        return 0;
    }

    for (i = 0, pkt = pkts; i < ntodo; i++, pkt++) {
        while (tip->naios_free < 1) {
            tip->send_wait = 1;

            // fprintf(stderr, "error? send thread wait now!\n");
            if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                fatal("pthread_cond_wait", ERR_SYSCALL,
                      "nfree_current cond wait failed\n");
                /*NOTREACHED*/
            }
        }

        tip->naios_free--;

        int rw = pkt->type;
        struct iocb_pkt *iocbp;

        iocbp = list_entry(tip->free_iocbs.next, struct iocb_pkt, head);
        iocb_setup(iocbp, tip->devices[pkt->disk_num], rw, pkt->size, pkt->offset);

        iocbp->op = rw ? 'R' : 'W';
        iocbp->stripe_id = pkt->stripe_id;

        if (iocbp->stripe_id != -1 && rw == 0)
            tip->scount++;

        list_move_tail(&iocbp->head, &tip->used_iocbs);
        list[i] = &iocbp->iocb;
    }

    pthread_mutex_unlock(&tip->mutex);
    return 1;
}




/**
 * replay_sub - Worker thread to submit AIOs that are being replayed
 */
static void *replay_sub(void *arg) {
    unsigned int i;
    char *mdev;
    char path[MAXPATHLEN];
    struct thr_info *tip = arg;

    pin_to_cpu(tip);

    set_replay_ready();

    record_index = 0;
    wait_iter_start();

    if (tip->ainfo->method == 0)
        raid5_online_recover(tip);

    else
        oi_raid_online_recover(tip);

    pthread_mutex_lock(&tip->mutex);
    tip->iter_send_done = 1;

    if (tip->reap_wait == 1) {
        tip->reap_wait = 0;
        pthread_cond_signal(&tip->cond);
    }

    pthread_mutex_unlock(&tip->mutex);

    pthread_mutex_lock(&tip->mutex);
    tip->send_done = 1;

    if (tip->reap_wait == 1) {
        tip->reap_wait = 0;
        pthread_cond_signal(&tip->cond);
    }

    pthread_mutex_unlock(&tip->mutex);

    set_replay_done();

    return NULL;
}



/**
 * handle_args: Parse passed in argument list
 * @argc: Number of arguments in argv
 * @argv: Arguments passed in
 *
 * Does rudimentary parameter verification as well.
 */
static void handle_args(struct thr_info *tip, int argc, char *argv[]) {
    if (argc != 10) {
        fprintf(stderr, "%s: method v k g chunk_size capacity devices_file trace_file requests_per_second\n", argv[0]);
        exit(1);
    }

    struct addr_info *ainfo = (struct addr_info *) malloc(sizeof(struct addr_info));

    ainfo->method = atoi(argv[1]);

    ainfo->v = atoi(argv[2]);

    ainfo->k = atoi(argv[3]);

    ainfo->g = atoi(argv[4]);

    ainfo->failedDisk = 9;

    ainfo->strip_size = atoi(argv[5]);  //KB

    ainfo->strip_size *= 1024;

    ainfo->capacity = atoi(argv[6]);    //MB

    ainfo->capacity *= 1024 * 1024;

    ainfo->max_stripes = CACHED_STRIPE_NUM;

    device_fn = argv[7];

    ainfo->trace_fn = argv[8];

    ainfo->requestsPerSecond = atoi(argv[9]);

    tip->scount = tip->rcount = 0;

    tip->ainfo = ainfo;
}

/*
 * ========================================================================
 * ==== MAIN ROUTINE ======================================================
 * ========================================================================
 */

/**
 * set_signal_done - Signal handler, catches signals & sets signal_done
 */
static void set_signal_done(__attribute__((__unused__))int signum) {
    signal_done = 1;
}


/**
 * main -
 * @argc: Number of arguments
 * @argv: Array of arguments
 */
int main(int argc, char *argv[]) {
    int i;

    pgsize = getpagesize();
    assert(pgsize > 0);

    setup_signal(SIGINT, set_signal_done);
    setup_signal(SIGTERM, set_signal_done);

    struct thr_info tip;
    memset(&tip, 0, sizeof(tip));

    get_ncpus();
    handle_args(&tip, argc, argv);

    nfiles = 1;
    tip_init(&tip);

    wait_replays_ready();

    long long start_time = gettime();

    for (i = 0; i < def_iterations; i++) {
        rgenesis = gettime();
        start_iter();

        if (verbose)
            fprintf(stderr, "I");

        wait_iters_done();
    }


    wait_replays_done();

    wait_reclaims_done();

    if (verbose)
        fprintf(stderr, "\n");

    rem_input_file(&tip);

    long long clock_diff = gettime() - start_time;

    FILE *f = fopen("running-time.txt", "a+");
    fprintf(f, "%lld.%09lld\n", du64_to_sec(clock_diff), du64_to_nsec(clock_diff));
    fclose(f);

    return 0;
}
