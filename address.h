
#define BLOCK 4096

struct addr_info {
    int method;
    int disk_nums;
    int failedDisk;
    int strip_size;
    addr_type capacity, capacity_total;

    addr_type blocks_partition;
    addr_type strips_partition;
    addr_type data_blocks;
    int blocks_per_strip;
    int stripe_nums;

    int **bibd, * *spd;
    int b, v, r, k, lambda;
    int g;
    int n, m;
    int g2; //S2-RAID

    char *trace_fn;
    int max_stripes;
    int requestsPerSecond;
};

int requestPerSecond = 0;

int **diskArray;           //子阵列对应的 磁盘号
int **offsetArray;         //子阵列对应的 偏移量（分区号）
int **diskRegion;           //每个disk包含的Region号

void makeSubRAID(struct addr_info *ainfo);


void init_parameters(struct addr_info *ainfo) {
    ainfo->blocks_per_strip = ainfo->strip_size / BLOCK;

    ainfo->capacity /= ainfo->strip_size;
    ainfo->capacity *= ainfo->strip_size;
   
    ainfo->stripe_nums = 1;
    
    if (ainfo->method == 0) {   //RAID5
        ainfo->capacity /= ainfo->strip_size;

        ainfo->strips_partition = ainfo->capacity;
        ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

        ainfo->capacity *= ainfo->strip_size;

        ainfo->disk_nums = ainfo->disk_nums / ainfo->k * ainfo->k;
        ainfo->capacity_total = ainfo->capacity / BLOCK * ainfo->disk_nums / ainfo->k * (ainfo->k - 1);
    } else if (ainfo->method == 1) {    //OI-RAID
        ainfo->stripe_nums = ainfo->v * ainfo->g * ainfo->r * (ainfo->g - 1) / ainfo->k;
        
        int align = ainfo->r * (ainfo->g - 1);
        ainfo->max_stripes = (ainfo->max_stripes) / align;
        ainfo->max_stripes *= align;

        ainfo->capacity /= ainfo->strip_size;

        ainfo->capacity /= ainfo->g * ainfo->r;
        ainfo->capacity *= ainfo->g * ainfo->r;

        ainfo->strips_partition = (ainfo->capacity) / (ainfo->g * ainfo->r);
        ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

        ainfo->data_blocks = ainfo->capacity;
        ainfo->data_blocks *= ainfo->blocks_per_strip;

        ainfo->capacity *= ainfo->strip_size;

        ainfo->capacity_total = ainfo->stripe_nums * (ainfo->k - 1) * ainfo->blocks_partition;
    } else if (ainfo->method == 2) {    //RS Code
        ainfo->capacity /= ainfo->strip_size;

        ainfo->strips_partition = ainfo->capacity;
        ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

        ainfo->capacity *= ainfo->strip_size;

        ainfo->disk_nums = ainfo->disk_nums / (ainfo->n + ainfo->m) * (ainfo->n + ainfo->m);
        ainfo->capacity_total = ainfo->capacity / BLOCK * ainfo->disk_nums / (ainfo->n + ainfo->m) * (ainfo->n);
    } else if (ainfo->method == 3) {    //S2-RAID
        ainfo->disk_nums = ainfo->disk_nums / ainfo->k * ainfo->k;
        ainfo->g2 = ainfo->disk_nums / ainfo->k;
        ainfo->stripe_nums = ainfo->g2 * ainfo->g2;
        
        int align = ainfo->g2;
        ainfo->max_stripes /= align;
        ainfo->max_stripes *= align;

        ainfo->capacity /= ainfo->strip_size;

        ainfo->capacity /= align;
        ainfo->capacity *= align;

        ainfo->strips_partition = ainfo->capacity / ainfo->g2;
        ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

        ainfo->data_blocks = ainfo->capacity;
        ainfo->data_blocks *= ainfo->blocks_per_strip;

        ainfo->capacity *= ainfo->strip_size;

        ainfo->capacity_total = ainfo->capacity / BLOCK * ainfo->disk_nums / ainfo->k * (ainfo->k - 1);
        
    } else if (ainfo->method == 4) {    //Parity Declustering
        ainfo->stripe_nums = ainfo->b;
        
        int align = ainfo->r;
        ainfo->max_stripes /= align;
        ainfo->max_stripes *= align;

        ainfo->capacity /= ainfo->strip_size;

        ainfo->capacity /= align;
        ainfo->capacity *= align;

        ainfo->strips_partition = ainfo->capacity / ainfo->r;
        ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

        ainfo->data_blocks = ainfo->capacity;
        ainfo->data_blocks *= ainfo->blocks_per_strip;

        ainfo->capacity *= ainfo->strip_size;

        ainfo->capacity_total = ainfo->capacity / BLOCK * ainfo->disk_nums / ainfo->k * (ainfo->k - 1);

    } else {
        exit(1);
    }

    fprintf(stderr, "capacity %fGB\n", ainfo->capacity * 1.0f / 1024 / 1024 / 1024);
    if (ainfo->method == 1)
        fprintf(stderr, "recover size %fGB\n", ainfo->r * (ainfo->g - 1) * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);
    else if (ainfo->method == 3)
        fprintf(stderr, "recover size %fGB\n", ainfo->g2 * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);
    else if (ainfo->method == 4)
        fprintf(stderr, "recover size %fGB\n", ainfo->r * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);
    else
        fprintf(stderr, "recover size %fGB\n", ainfo->capacity * 1.0f / 1024 / 1024 / 1024);
}

void init_addr_info(struct addr_info *ainfo) {
    char fn[128];
    sprintf(fn, "%d.%d.bd", ainfo->v, ainfo->k);
    FILE *bibd_f = fopen(fn, "r");

    fscanf(bibd_f, "%d %d %d %d %d", &ainfo->b, &ainfo->v, &ainfo->k, &ainfo->r, &ainfo->lambda);

    ainfo->disk_nums = ainfo->v * ainfo->g;

    if (ainfo->method == 4) {
        fclose(bibd_f);

        sprintf(fn, "%d.%d.bd", ainfo->disk_nums, ainfo->k);
        bibd_f = fopen(fn, "r");
        fscanf(bibd_f, "%d %d %d %d %d", &ainfo->b, &ainfo->v, &ainfo->k, &ainfo->r, &ainfo->lambda);

        //fprintf(stderr, "%d %d %d %d %d\n", ainfo->b, ainfo->v, ainfo->k, ainfo->r, ainfo->lambda);
        //exit(1);
    }

    init_parameters(ainfo);

    int i, j;
    int stripe_nums = ainfo->stripe_nums, region_nums = 1;
    
    if (ainfo->method == 1)
        region_nums = ainfo->g * ainfo->r;
    else if (ainfo->method == 3)
        region_nums = ainfo->g2;
    else if (ainfo->method == 4)
        region_nums = ainfo->r;

        diskArray = (typeof(diskArray)) malloc(sizeof(typeof(*diskArray)) * stripe_nums);
        offsetArray = (typeof(offsetArray)) malloc(sizeof(typeof(*offsetArray)) * stripe_nums);

        for (i = 0; i < stripe_nums; i++) {
            diskArray[i] = (typeof(*diskArray)) malloc(sizeof(typeof(**diskArray)) * ainfo->k);
            offsetArray[i] = (typeof(*offsetArray)) malloc(sizeof(typeof(**offsetArray)) * ainfo->k);
        }

        diskRegion = (typeof(diskRegion)) malloc(sizeof(typeof(*diskRegion)) * ainfo->disk_nums);

        for (i = 0; i < ainfo->disk_nums; i++) {
            diskRegion[i] = (typeof(*diskRegion)) malloc(sizeof(typeof(**diskRegion)) * region_nums);
        }

    int **bibd, **spd;
    bibd = (typeof(bibd)) malloc(sizeof(typeof(*bibd)) * ainfo->b);

    for (i = 0; i < ainfo->b; i++) {
        bibd[i] = (typeof(*bibd)) malloc(sizeof(typeof(**bibd)) * ainfo->k);

        for (j = 0; j < ainfo->k; j++) {
            fscanf(bibd_f, "%d", &bibd[i][j]);
        }
    }

    int g = ainfo->method == 3 ? ainfo->g2 : ainfo->g;

    spd = (typeof(spd)) malloc(sizeof(typeof(*spd)) * g * g);

    for (i = 0; i < g * g; i++) {
        spd[i] = (typeof(*spd)) malloc(sizeof(typeof(**spd)) * ainfo->k);

        for (j = 0; j < ainfo->k; j++) {
            int a, b;
            a = i / g;
            b = i % g;
            spd[i][j] = (b + a * j) % g;
        }
    }

    ainfo->bibd = bibd;
    ainfo->spd = spd;

    if (ainfo->method == 1)
        makeSubRAID(ainfo);
    else if (ainfo->method == 3) {
        for(i = 0; i < stripe_nums; i++) {
            for(j = 0; j < ainfo->k; j++) {
                diskArray[i][j] = spd[i][j] + j * ainfo->g2;
                offsetArray[i][j] = i / ainfo->g2;
                diskRegion[diskArray[i][j]][offsetArray[i][j]] = i;
            }
        }
    }
    else if (ainfo->method == 4) {
        int disk[MAX_DEVICE_NUM] = {0};
        for(i = 0; i < stripe_nums; i++) {
            for(j = 0; j < ainfo->k; j++) {
                diskArray[i][j] = bibd[i][j];
                offsetArray[i][j] = disk[diskArray[i][j]];
                disk[diskArray[i][j]]++;

                diskRegion[diskArray[i][j]][offsetArray[i][j]] = i;
            }
        }
    }
}

void destroy_addr_info(struct addr_info *ainfo) {
    int i;
    int stripe_nums = ainfo->stripe_nums;

    for (i = 0; i < stripe_nums; i++) {
        free(diskArray[i]);
        free(offsetArray[i]);
    }

    free(diskArray);
    free(offsetArray);

    for (i = 0; i < ainfo->disk_nums; i++) {
        free(diskRegion[i]);
    }

    free(diskRegion);

    int **bibd = ainfo->bibd;

    for (i = 0; i < ainfo->b; i++) {
        free(bibd[i]);
    }

    free(bibd);

    int **spd = ainfo->spd;
    int g = ainfo->method == 3 ? ainfo->g2 : ainfo->g;

    for (i = 0; i < g * g; i++) {
        free(spd[i]);
    }

    free(spd);

    free(ainfo);
}

void makeSubRAID(struct addr_info *ainfo) {
    int i, j, k;
    int **bibd = ainfo->bibd;
    int **spd = ainfo->spd;
    int *disk = (typeof(disk)) malloc(sizeof(typeof(disk)) * ainfo->g * ainfo->v);
    memset(disk, 0, sizeof(typeof(disk)) * ainfo->g * ainfo->v);

    int stripe_nums = ainfo->v * ainfo->g * ainfo->r * (ainfo->g - 1) / ainfo->k;
    int **bd;
    bd = (typeof(bd)) malloc(sizeof(typeof(*bd)) * stripe_nums);

    for (i = 0; i < stripe_nums; i++) {
        bd[i] = (typeof(*bd)) malloc(sizeof(typeof(**bd)) * ainfo->k);
    }

    for(i = 0; i < ainfo->b; i++) {
        for(j = 0; j < ainfo->g * (ainfo->g - 1); j++) {
            for(k = 0; k < ainfo->k; k++) {
                int a = bibd[i][k];
                int b = spd[j][k];
                bd[i * ainfo->g * (ainfo->g - 1) + j][k] = ainfo->g * a + b;
            }
        }
    }

    for(i = 0; i < stripe_nums; i++) {
        for(j = 0; j < ainfo->k; j++) {
            diskArray[i][j] = bd[i][j];
            offsetArray[i][j] = disk[bd[i][j]];
            diskRegion[bd[i][j]][disk[bd[i][j]]] = i;
            disk[bd[i][j]]++;

            if((disk[bd[i][j]] + 1) % ainfo->g == 0) {
                diskRegion[bd[i][j]][disk[bd[i][j]]] = -1;
                disk[bd[i][j]]++;
            }
        }
    }

    free(disk);

    for (i = 0; i < stripe_nums; i++) {
        free(bd[i]);
    }

    free(bd);
}


void oi_sub_raid_request(struct thr_info *tip, int subRAIDAddr, int disks[] , int offsets[], int reqSize, char op) {
    struct iocb *list[MAX_DEVICE_NUM];
    struct request_info reqs[MAX_DEVICE_NUM];

    struct addr_info *ainfo = tip->ainfo;

    int dataDiskNum = ainfo->k - 1;
    int stripeId;
    int inStripeAddr, inBlockId;       //data的位置，在条带内部
    int diskId[4];     //全磁盘， 可能涉及到4个块，1个data和3个parity
    addr_type blockId[4];
    int reqBlockNum;

    int virDiskId[2]; //虚拟磁盘号：0,1或2

    int groupId, regionId;     //修改的数据或global parity所在的组号
    int inRegionX, inRegionY;

    int localX;   //对应的local parity的相对磁盘号，相对region号都是2

    if(reqSize % BLOCK == 0) {
        reqBlockNum = reqSize / BLOCK;
    } else {
        reqBlockNum = reqSize / BLOCK + 1;
    }

    int i, req_count;

    for(i = 0; i < reqBlockNum; i++) {
        req_count = 0;

        stripeId = subRAIDAddr / ((dataDiskNum + 1) * dataDiskNum);

        inStripeAddr = subRAIDAddr % ((dataDiskNum + 1) * dataDiskNum);
        inBlockId = inStripeAddr / (dataDiskNum + 1);

        virDiskId[0] = inStripeAddr % (dataDiskNum + 1);
        diskId[0] = disks[virDiskId[0]];

        if(virDiskId[0] >= dataDiskNum - inBlockId) { //****这里就完成了轮转
            inBlockId += 1;
        }

        blockId[0] = offsets[virDiskId[0]] * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;


        int ntodo = 0, ndone;
        reqs[req_count].type = 1;
        reqs[req_count].disk_num = diskId[0];
        reqs[req_count].offset = blockId[0] * BLOCK;
        reqs[req_count].size = BLOCK;
        reqs[req_count].stripe_id = -1;
        req_count++;
        ntodo++;

        if(op == 'w' || op == 'W') {
            reqs[req_count].type = 0;
            reqs[req_count].disk_num = diskId[0];
            reqs[req_count].offset = blockId[0] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            // 1.  global parity
            virDiskId[1] = dataDiskNum - inBlockId;
            diskId[1] = disks[virDiskId[1]];
            blockId[1] = offsets[virDiskId[1]] * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;

            reqs[req_count].type = 1;
            reqs[req_count].disk_num = diskId[1];
            reqs[req_count].offset = blockId[1] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            reqs[req_count].type = 0;
            reqs[req_count].disk_num = diskId[1];
            reqs[req_count].offset = blockId[1] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            // 2.  data对应的local parity
            groupId = disks[virDiskId[0]] / ainfo->g;
            regionId = offsets[virDiskId[0]] / ainfo->g;

            inRegionX = disks[virDiskId[0]] % ainfo->g;
            inRegionY = offsets[virDiskId[0]] % ainfo->g;
            localX = ((inRegionX - inRegionY) + ainfo->g - 1) % ainfo->g;

            diskId[2] = groupId * ainfo->g + localX;
            blockId[2] = (regionId * ainfo->g + ainfo->g - 1) * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;

            reqs[req_count].type = 1;
            reqs[req_count].disk_num = diskId[2];
            reqs[req_count].offset = blockId[2] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            reqs[req_count].type = 0;
            reqs[req_count].disk_num = diskId[2];
            reqs[req_count].offset = blockId[2] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            // 3.  global parity对应的local parity
            groupId = disks[virDiskId[1]] / ainfo->g;
            regionId = offsets[virDiskId[1]] / ainfo->g;

            inRegionX = disks[virDiskId[1]] % ainfo->g;
            inRegionY = offsets[virDiskId[1]] % ainfo->g;
            localX = ((inRegionX - inRegionY) + ainfo->g - 1) % ainfo->g;

            diskId[3] = groupId * ainfo->g + localX;
            blockId[3] = (regionId * ainfo->g + ainfo->g - 1) * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;

            reqs[req_count].type = 1;
            reqs[req_count].disk_num = diskId[3];
            reqs[req_count].offset = blockId[3] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            reqs[req_count].type = 0;
            reqs[req_count].disk_num = diskId[3];
            reqs[req_count].offset = blockId[3] * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;
        }

        iocbs_map(tip, list, reqs, ntodo, 0);

        ndone = io_submit(tip->ctx, ntodo, list);

        if (ndone != ntodo) {
            fatal("io_submit", ERR_SYSCALL,
                  "%d: io_submit(%d:%ld) failed (%s)\n",
                  tip->cpu, ntodo, ndone,
                  strerror(labs(ndone)));
            /*NOTREACHED*/
        }

        pthread_mutex_lock(&tip->mutex);
        tip->naios_out += ndone;
        assert(tip->naios_out <= naios);

        if (tip->reap_wait) {
            tip->reap_wait = 0;
            pthread_cond_signal(&tip->cond);
        }
        pthread_mutex_unlock(&tip->mutex);

        subRAIDAddr++;
    }
}

//访问oi-raid
void oi_raid_request(struct thr_info *tip, int logicAddr, int reqSize, char op ) {
    int i;
    int subRAIDId;
    int subRAIDAddr;

    int reqBlockNum;

    int disks[MAX_DEVICE_NUM], offsets[MAX_DEVICE_NUM];
    struct addr_info *ainfo = tip->ainfo;

    subRAIDId = logicAddr / (ainfo->blocks_partition * (ainfo->k - 1));
    subRAIDAddr = logicAddr % (ainfo->blocks_partition * (ainfo->k - 1));

    if(reqSize % BLOCK == 0) {
        reqBlockNum = reqSize / BLOCK;

    } else {
        reqBlockNum = reqSize / BLOCK + 1;
    }

    for(i = 0; i < ainfo->k; i++) {
        disks[i] = diskArray[subRAIDId][i];
        offsets[i] = offsetArray[subRAIDId][i];
    }

    if(subRAIDAddr + reqBlockNum <= (ainfo->blocks_partition * (ainfo->k - 1))) {
        oi_sub_raid_request(tip, subRAIDAddr, disks, offsets, reqSize, op);

    } else {
        int reqSizeFirst, reqSizeLast;
        reqSizeFirst = ((ainfo->blocks_partition * (ainfo->k - 1)) - subRAIDAddr) * BLOCK;
        oi_sub_raid_request(tip, subRAIDAddr, disks, offsets, reqSizeFirst, op);

        for(i = 0; i < ainfo->k; i++) {
            disks[i] = diskArray[subRAIDId + 1][i];
            offsets[i] = offsetArray[subRAIDId + 1][i];
        }

        reqSizeLast = (subRAIDAddr + reqBlockNum - (ainfo->blocks_partition * (ainfo->k - 1))) * BLOCK;
        oi_sub_raid_request(tip, 0, disks, offsets, reqSizeLast, op);
    }
}


//访问21个磁盘的RAID5盘阵，每3个磁盘为一个2+1的RAID5
void raid5_3time7disks_request(struct thr_info *tip, int logicAddr, int reqSize, char op) {
    struct iocb *list[MAX_DEVICE_NUM];
    struct request_info reqs[MAX_DEVICE_NUM];

    struct addr_info *ainfo = tip->ainfo;

    int dataDiskNum = ainfo->k - 1;
    int dataPerStripe = (dataDiskNum + 1) * dataDiskNum;
    int maxOffset, reqBlockNum;
    int stripeId, groupId, inStripeAddr, inBlockId, diskId, sectorId;
    addr_type blockId;

    maxOffset = ainfo->capacity_total;

    if(reqSize % BLOCK == 0) {
        reqBlockNum = reqSize / BLOCK;
    } else {
        reqBlockNum = reqSize / BLOCK + 1;
    }

    int groups = ainfo->disk_nums / ainfo->k;

    int i, req_count;

    for(i = 0; i < reqBlockNum; i++) {
        if (logicAddr < maxOffset) {
            req_count = 0;

            stripeId = logicAddr / (dataPerStripe * groups);
            groupId = (logicAddr % (dataPerStripe * groups)) / dataPerStripe;
            inStripeAddr = logicAddr % dataPerStripe;
            inBlockId = inStripeAddr / (dataDiskNum + 1);

            diskId = inStripeAddr % (dataDiskNum + 1);

            if (diskId >= dataDiskNum - inBlockId) { //****这里就完成了轮转
                inBlockId += 1;
            }

            diskId += groupId * ainfo->k;
            blockId = stripeId * (dataDiskNum + 1) + inBlockId;

            int ntodo = 0, ndone;
            reqs[req_count].type = 1;
            reqs[req_count].disk_num = diskId;
            reqs[req_count].offset = blockId * BLOCK;
            reqs[req_count].size = BLOCK;
            reqs[req_count].stripe_id = -1;
            req_count++;
            ntodo++;

            if (op == 'w' || op == 'W') {
                reqs[req_count].type = 0;
                reqs[req_count].disk_num = diskId;
                reqs[req_count].offset = blockId * BLOCK;
                reqs[req_count].size = BLOCK;
                reqs[req_count].stripe_id = -1;
                req_count++;
                ntodo++;

                reqs[req_count].type = 1;
                reqs[req_count].disk_num = dataDiskNum - inBlockId + groupId * ainfo->k;
                reqs[req_count].offset = blockId * BLOCK;
                reqs[req_count].size = BLOCK;
                reqs[req_count].stripe_id = -1;
                req_count++;
                ntodo++;

                reqs[req_count].type = 0;
                reqs[req_count].disk_num = dataDiskNum - inBlockId + groupId * ainfo->k;
                reqs[req_count].offset = blockId * BLOCK;
                reqs[req_count].size = BLOCK;
                reqs[req_count].stripe_id = -1;
                req_count++;
                ntodo++;
            }

            iocbs_map(tip, list, reqs, ntodo, 0);

            ndone = io_submit(tip->ctx, ntodo, list);

            if (ndone != ntodo) {
                fatal("io_submit", ERR_SYSCALL,
                      "%d: io_submit(%d:%ld) failed (%s)\n",
                      tip->cpu, ntodo, ndone,
                      strerror(labs(ndone)));
                /*NOTREACHED*/
            }

            pthread_mutex_lock(&tip->mutex);
            tip->naios_out += ndone;
            assert(tip->naios_out <= naios);

            if (tip->reap_wait) {
                tip->reap_wait = 0;
                pthread_cond_signal(&tip->cond);
            }

            pthread_mutex_unlock(&tip->mutex);


            logicAddr++;
        }
    }
}

//g=k
// 21个磁盘，部署7组传统2+1 RAID5，假定每个磁盘6个PARTITION
void raid5_online_recover(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();
    long long start_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;


    struct addr_info *ainfo = tip->ainfo;
    int i, j, k, m;

    int groupId, inGroupId; //坏盘所在组，以及在组内的磁盘编号
    int *disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * (ainfo->k - 1));   //对应的2个存活磁盘

    FILE *f = fopen(ainfo->trace_fn, "r");

    int hostName, logicAddr, size;
    char op;
    double timeStamp;

    groupId = ainfo->failedDisk / ainfo->k;
    inGroupId = ainfo->failedDisk % ainfo->k;

    j = 0;

    for(i = 0; i < ainfo->k; i++) {
        if(inGroupId == i)
            continue;

        disks[j] = groupId * ainfo->k + i;
        j++;
    }

    int max = 100;
    int step = (int) (ainfo->strips_partition / 100.0);

    if (step == 0) {
        max = ainfo->strips_partition;
        step = 1;
    }

    tip->bs->left_stripes = ainfo->max_stripes;
    fprintf(stderr, "start recover [raid5], total size %fGB\n", ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);

    for(i = 0; i < 1; i++) {
        for(j = 0; j < ainfo->strips_partition; j++) {
            if ((i * ainfo->strips_partition + j) % step == 0) {
                int cur = (i * ainfo->strips_partition + j) / step;
                fprintf(stderr, "progress %d/%d\n", cur, max);
            }

            if (processed_stripes != 0 && processed_stripes % ainfo->max_stripes == 0) {   //du64_to_sec(gettime() - last_time) >= 10
                printf("wait reclaim\n");
                pthread_mutex_lock(&tip->mutex);
                tip->send_wait = 1;
                tip->wait_all_finish = 1;

                if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                    fatal("pthread_cond_wait", ERR_SYSCALL,
                          "time cond wait failed\n");
                    /*NOTREACHED*/
                }

                last_time = gettime();
                tip->bs->left_stripes = ainfo->max_stripes;
                pthread_mutex_unlock(&tip->mutex);
            }

            int ntodo = ainfo->k - 1, ndone;

            for(k = 0; k < ainfo->k - 1; k++) {
                reqs[k].type = 1;
                reqs[k].disk_num = disks[k];
                reqs[k].offset = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
                reqs[k].size = ainfo->strip_size;
                reqs[k].stripe_id = processed_stripes % ainfo->max_stripes;
            }

            tip->bs->left_nums[processed_stripes % ainfo->max_stripes] = ainfo->k - 1;
            tip->bs->disk_dst[processed_stripes % ainfo->max_stripes] = ainfo->failedDisk;
            tip->bs->offset_dst[processed_stripes % ainfo->max_stripes] = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
            iocbs_map(tip, list, reqs, ntodo, 0);

            ndone = io_submit(tip->ctx, ntodo, list);

            if (ndone != ntodo) {
                fatal("io_submit", ERR_SYSCALL,
                      "%d: io_submit(%d:%ld) failed (%s)\n",
                      tip->cpu, ntodo, ndone,
                      strerror(labs(ndone)));
            }

            pthread_mutex_lock(&tip->mutex);
            tip->naios_out += ndone;
            assert(tip->naios_out <= naios);

            if (tip->reap_wait) {
                tip->reap_wait = 0;
                pthread_cond_signal(&tip->cond);
            }

            pthread_mutex_unlock(&tip->mutex);


            // if ((reqest_count + 1) % 20 == 0)
            //  fprintf(stderr, "has process %d request\n", reqest_count);

            int reqest_count = 0;

            while (reqest_count < ainfo->requestsPerSecond) {
                int retCode;
                retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);

                if (retCode != 5) {
                    fclose(f);
                    f = fopen(ainfo->trace_fn, "r");
                    retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);
                }

                logicAddr = (logicAddr / 8) % ainfo->capacity_total;
                raid5_3time7disks_request(tip, logicAddr, size, op);
                reqest_count++;

                long long cur_time = gettime();
                long long time_diff = (long long) (timeStamp * 1000 * 1000 * 1000) - (cur_time - start_time);

                if (time_diff < 0) {
                    break;
                }
            }

            processed_stripes++;
        }
    }

    free(disks);
}

void rs_online_recover(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();
    long long start_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;


    struct addr_info *ainfo = tip->ainfo;
    int i, j, k, m;

    int groupId, inGroupId; //坏盘所在组，以及在组内的磁盘编号
    int *disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * (ainfo->n));   //对应的2个存活磁盘

    FILE *f = fopen(ainfo->trace_fn, "r");

    int hostName, logicAddr, size;
    char op;
    double timeStamp;

    groupId = ainfo->failedDisk / (ainfo->n + ainfo->m);
    inGroupId = ainfo->failedDisk % (ainfo->n + ainfo->m);

    j = 0;

    for(i = 0; i < ainfo->n + ainfo->m && j < ainfo->n; i++) {
        if(inGroupId == i)
            continue;

        disks[j] = groupId * (ainfo->n + ainfo->m) + i;
        j++;
    }

    int max = 100;
    int step = (int) (ainfo->strips_partition / 100.0);

    if (step == 0) {
        max = ainfo->strips_partition;
        step = 1;
    }

    tip->bs->left_stripes = ainfo->max_stripes;
    fprintf(stderr, "start recover [rs(6,9)], total size %fGB\n", ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);

    for(i = 0; i < 1; i++) {
        for(j = 0; j < ainfo->strips_partition; j++) {
            if ((i * ainfo->strips_partition + j) % step == 0) {
                int cur = (i * ainfo->strips_partition + j) / step;
                fprintf(stderr, "progress %d/%d\n", cur, max);
            }

            if (processed_stripes != 0 && processed_stripes % ainfo->max_stripes == 0) {   //du64_to_sec(gettime() - last_time) >= 10
                printf("wait reclaim\n");
                pthread_mutex_lock(&tip->mutex);
                tip->send_wait = 1;
                tip->wait_all_finish = 1;

                if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                    fatal("pthread_cond_wait", ERR_SYSCALL,
                          "time cond wait failed\n");
                    /*NOTREACHED*/
                }

                last_time = gettime();
                tip->bs->left_stripes = ainfo->max_stripes;
                pthread_mutex_unlock(&tip->mutex);
            }

            int ntodo = ainfo->n, ndone;

            for(k = 0; k < ainfo->n; k++) {
                reqs[k].type = 1;
                reqs[k].disk_num = disks[k];
                reqs[k].offset = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
                reqs[k].size = ainfo->strip_size;
                reqs[k].stripe_id = processed_stripes % ainfo->max_stripes;
            }

            tip->bs->left_nums[processed_stripes % ainfo->max_stripes] = ainfo->n;
            tip->bs->disk_dst[processed_stripes % ainfo->max_stripes] = ainfo->failedDisk;
            tip->bs->offset_dst[processed_stripes % ainfo->max_stripes] = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
            iocbs_map(tip, list, reqs, ntodo, 0);

            ndone = io_submit(tip->ctx, ntodo, list);

            if (ndone != ntodo) {
                fatal("io_submit", ERR_SYSCALL,
                      "%d: io_submit(%d:%ld) failed (%s)\n",
                      tip->cpu, ntodo, ndone,
                      strerror(labs(ndone)));
            }

            pthread_mutex_lock(&tip->mutex);
            tip->naios_out += ndone;
            assert(tip->naios_out <= naios);

            if (tip->reap_wait) {
                tip->reap_wait = 0;
                pthread_cond_signal(&tip->cond);
            }

            pthread_mutex_unlock(&tip->mutex);


            // if ((reqest_count + 1) % 20 == 0)
            //  fprintf(stderr, "has process %d request\n", reqest_count);

            int reqest_count = 0;

            while (reqest_count < ainfo->requestsPerSecond) {
                int retCode;
                retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);

                if (retCode != 5) {
                    fclose(f);
                    f = fopen(ainfo->trace_fn, "r");
                    retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);
                }

                logicAddr = (logicAddr / 8) % ainfo->capacity_total;
                raid5_3time7disks_request(tip, logicAddr, size, op);
                reqest_count++;

                long long cur_time = gettime();
                long long time_diff = (long long) (timeStamp * 1000 * 1000 * 1000) - (cur_time - start_time);

                if (time_diff < 0) {
                    break;
                }
            }

            processed_stripes++;
        }
    }

    free(disks);
}

//oi-raid单盘修复
void oi_raid_online_recover(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();
    long long start_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;

    struct addr_info *ainfo = tip->ainfo;
    int i, j, k, n, m;
    int *subRAID = (typeof(subRAID)) malloc(sizeof(typeof(*subRAID)) * ainfo->r * (ainfo->g - 1));  //需要修复的6个PARTITION

    int **disks, **offsets; //6个PARTITION分别对应的存活磁盘和偏移

    disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * ainfo->r * (ainfo->g - 1));
    offsets = (typeof(offsets)) malloc(sizeof(typeof(*offsets)) * ainfo->r * (ainfo->g - 1));

    for (i = 0; i < ainfo->r * (ainfo->g - 1); i++) {
        disks[i] = (typeof(*disks)) malloc(sizeof(typeof(**disks)) * (ainfo->k - 1));
        offsets[i] = (typeof(*offsets)) malloc(sizeof(typeof(**offsets)) * (ainfo->k - 1));
    }


    int hostName, logicAddr, size;
    char op;
    double timeStamp;

    int spareOffset = 0;

    for(i = 0; i < ainfo->r * (ainfo->g - 1); i++) {
        int ii = i / (ainfo->g - 1);
        ii = ii * ainfo->g + i % (ainfo->g - 1);
        subRAID[i] = diskRegion[ainfo->failedDisk][ii];

        j = 0;

        for(k = 0; k < ainfo->k; k++) {
            if(diskArray[subRAID[i]][k] == ainfo->failedDisk)
                continue;

            disks[i][j] = diskArray[subRAID[i]][k];
            offsets[i][j] = offsetArray[subRAID[i]][k];
            j++;
        }
    }

    int spareDisks[MAX_DEVICE_NUM] = {0};
    int spareMap[MAX_DEVICE_NUM], spareDiskNum = ainfo->disk_nums;
    for (j = 0; j < ainfo->r * (ainfo->g - 1); j++) {
        for (k = 0; k < ainfo->k - 1; k++) {
            spareDisks[disks[j][k]] = 1;
            spareDiskNum--;
        }
    }
    i = 0;
    for (j = 0; j < ainfo->disk_nums; j++) {
        if (spareDisks[j] == 0)
            spareMap[i++] = j;
    }
    if (i != spareDiskNum) {
        fprintf(stderr, "error disk_nums %d, %d\n", i, spareDiskNum);
        exit(1);
    }

    FILE *f = fopen(ainfo->trace_fn, "r");

    int max = 100;
    int step = (int) (ainfo->strips_partition / 100.0);

    if (step == 0) {
        max = ainfo->strips_partition;
        step = 1;
    }

    tip->bs->left_stripes = ainfo->max_stripes;
    fprintf(stderr, "start recover [oi-raid], total size %fGB\n", ainfo->r * (ainfo->g - 1) * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);

    for(i = 0; i < ainfo->strips_partition; i++) {
        if ((i) % step == 0)
            fprintf(stderr, "progress %d/%d\n", (i) / step, max);

        int req_count = 0;

        for(j = 0; j < ainfo->r * (ainfo->g - 1); j++) {
            if (processed_stripes != 0 && processed_stripes % ainfo->max_stripes == 0) {   //du64_to_sec(gettime() - last_time) >= 10
                // printf("time wait\n");
                pthread_mutex_lock(&tip->mutex);
                tip->send_wait = 1;
                tip->wait_all_finish = 1;

                if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                    fatal("pthread_cond_wait", ERR_SYSCALL,
                          "time cond wait failed\n");
                    /*NOTREACHED*/
                }

                last_time = gettime();
                tip->bs->left_stripes = ainfo->max_stripes;
                pthread_mutex_unlock(&tip->mutex);
            }

            for(k = 0; k < ainfo->k - 1; k++) {

                reqs[req_count].type = 1;
                reqs[req_count].disk_num = disks[j][k];
                reqs[req_count].offset = (offsets[j][k] * ainfo->blocks_partition + i * ainfo->blocks_per_strip) * BLOCK;
                reqs[req_count].size = ainfo->strip_size;
                reqs[req_count].stripe_id = processed_stripes % ainfo->max_stripes;
                req_count++;
            }

            tip->bs->left_nums[processed_stripes % ainfo->max_stripes] = ainfo->k - 1;
            tip->bs->disk_dst[processed_stripes % ainfo->max_stripes] = spareMap[spareOffset % spareDiskNum];
            tip->bs->offset_dst[processed_stripes % ainfo->max_stripes] = (ainfo->data_blocks + spareOffset / spareDiskNum * ainfo->blocks_per_strip) * BLOCK;

            spareOffset++;
            processed_stripes++;
        }

        iocbs_map(tip, list, reqs, req_count, 0);

        int ndone = io_submit(tip->ctx, req_count, list);

        if (ndone != req_count) {
            fatal("io_submit", ERR_SYSCALL,
                  "%d: io_submit(%d:%ld) failed (%s)\n",
                  tip->cpu, req_count, ndone,
                  strerror(labs(ndone)));
            /*NOTREACHED*/
        }

        pthread_mutex_lock(&tip->mutex);
        tip->naios_out += ndone;
        assert(tip->naios_out <= naios);

        if (tip->reap_wait) {
            tip->reap_wait = 0;
            pthread_cond_signal(&tip->cond);
        }

        pthread_mutex_unlock(&tip->mutex);

        int reqest_count = 0;

        while (reqest_count < ainfo->requestsPerSecond) {
            int retCode;
            retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);

            if (retCode != 5) {
                fclose(f);
                f = fopen(ainfo->trace_fn, "r");
                retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);
            }

            logicAddr = (logicAddr / 8) % ainfo->capacity_total;
            oi_raid_request(tip, logicAddr, size, op);
            reqest_count++;

            long long cur_time = gettime();
            long long time_diff = (long long) (timeStamp * 1000 * 1000 * 1000) - (cur_time - start_time);

            if (time_diff < 0) {
                break;
            }
        }


    }


    free(subRAID);

    for (i = 0; i < ainfo->r * (ainfo->g - 1); i++) {
        free(disks[i]);
        free(offsets[i]);
    }

    free(disks);
    free(offsets);
}

void s2_raid_online_recover(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();
    long long start_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;

    struct addr_info *ainfo = tip->ainfo;
    int i, j, k, n, m;
    int *subRAID = (typeof(subRAID)) malloc(sizeof(typeof(*subRAID)) * ainfo->g2);  //需要修复的6个PARTITION

    int **disks, **offsets; //6个PARTITION分别对应的存活磁盘和偏移

    disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * ainfo->g2);
    offsets = (typeof(offsets)) malloc(sizeof(typeof(*offsets)) * ainfo->g2);

    for (i = 0; i < ainfo->g2; i++) {
        disks[i] = (typeof(*disks)) malloc(sizeof(typeof(**disks)) * (ainfo->k - 1));
        offsets[i] = (typeof(*offsets)) malloc(sizeof(typeof(**offsets)) * (ainfo->k - 1));
    }


    int hostName, logicAddr, size;
    char op;
    double timeStamp;

    int spareOffset = 0;
    for(i = 0; i < ainfo->g2; i++) {
        subRAID[i] = diskRegion[ainfo->failedDisk][i];

        j = 0;

        for(k = 0; k < ainfo->k; k++) {
            if(diskArray[subRAID[i]][k] == ainfo->failedDisk)
                continue;

            disks[i][j] = diskArray[subRAID[i]][k];
            offsets[i][j] = offsetArray[subRAID[i]][k];
            j++;

        }
    }

    int spareDisks[MAX_DEVICE_NUM] = {0};
    int spareMap[MAX_DEVICE_NUM], spareDiskNum = ainfo->disk_nums;
    for (j = 0; j < ainfo->g2; j++) {
        for (k = 0; k < ainfo->k - 1; k++) {
            spareDisks[disks[j][k]] = 1;
            spareDiskNum--;
        }
    }
    i = 0;
    for (j = 0; j < ainfo->disk_nums; j++) {
        if (spareDisks[j] == 0)
            spareMap[i++] = j;
    }
    if (i != spareDiskNum) {
        fprintf(stderr, "error disk_nums %d, %d\n", i, spareDiskNum);
        exit(1);
    }

    FILE *f = fopen(ainfo->trace_fn, "r");

    int max = 100;
    int step = (int) (ainfo->strips_partition / 100.0);

    if (step == 0) {
        max = ainfo->strips_partition;
        step = 1;
    }

    tip->bs->left_stripes = ainfo->max_stripes;
    fprintf(stderr, "start recover [s2-raid], total size %fGB, strips_partition %lld\n", ainfo->g2 * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024, ainfo->strips_partition);

    for(i = 0; i < ainfo->strips_partition; i++) {
        if ((i) % step == 0)
            fprintf(stderr, "progress %d/%d\n", (i) / step, max);

        int req_count = 0;

        for(j = 0; j < ainfo->g2; j++) {
            if (processed_stripes != 0 && processed_stripes % ainfo->max_stripes == 0) {   //du64_to_sec(gettime() - last_time) >= 10
                // printf("time wait\n");
                pthread_mutex_lock(&tip->mutex);
                tip->send_wait = 1;
                tip->wait_all_finish = 1;

                if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                    fatal("pthread_cond_wait", ERR_SYSCALL,
                          "time cond wait failed\n");
                    /*NOTREACHED*/
                }

                last_time = gettime();
                tip->bs->left_stripes = ainfo->max_stripes;
                pthread_mutex_unlock(&tip->mutex);
            }

            for(k = 0; k < ainfo->k - 1; k++) {

                reqs[req_count].type = 1;
                reqs[req_count].disk_num = disks[j][k];
                reqs[req_count].offset = (offsets[j][k] * ainfo->blocks_partition + i * ainfo->blocks_per_strip) * BLOCK;
                reqs[req_count].size = ainfo->strip_size;
                reqs[req_count].stripe_id = processed_stripes % ainfo->max_stripes;
                req_count++;
            }

            tip->bs->left_nums[processed_stripes % ainfo->max_stripes] = ainfo->k - 1;
            tip->bs->disk_dst[processed_stripes % ainfo->max_stripes] = spareMap[spareOffset % spareDiskNum];
            tip->bs->offset_dst[processed_stripes % ainfo->max_stripes] = (ainfo->data_blocks + spareOffset / spareDiskNum * ainfo->blocks_per_strip) * BLOCK;

            spareOffset++;
            processed_stripes++;
        }

        iocbs_map(tip, list, reqs, req_count, 0);

        int ndone = io_submit(tip->ctx, req_count, list);

        if (ndone != req_count) {
            fatal("io_submit", ERR_SYSCALL,
                  "%d: io_submit(%d:%ld) failed (%s)\n",
                  tip->cpu, req_count, ndone,
                  strerror(labs(ndone)));
            /*NOTREACHED*/
        }

        pthread_mutex_lock(&tip->mutex);
        tip->naios_out += ndone;
        assert(tip->naios_out <= naios);

        if (tip->reap_wait) {
            tip->reap_wait = 0;
            pthread_cond_signal(&tip->cond);
        }

        pthread_mutex_unlock(&tip->mutex);

        int reqest_count = 0;

        while (reqest_count < ainfo->requestsPerSecond) {
            int retCode;
            retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);

            if (retCode != 5) {
                fclose(f);
                f = fopen(ainfo->trace_fn, "r");
                retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);
            }

            logicAddr = (logicAddr / 8) % ainfo->capacity_total;
            oi_raid_request(tip, logicAddr, size, op);
            reqest_count++;

            long long cur_time = gettime();
            long long time_diff = (long long) (timeStamp * 1000 * 1000 * 1000) - (cur_time - start_time);

            if (time_diff < 0) {
                break;
            }
        }


    }


    free(subRAID);

    for (i = 0; i < ainfo->g2; i++) {
        free(disks[i]);
        free(offsets[i]);
    }

    free(disks);
    free(offsets);
}

void parity_declustering_online_recover(struct thr_info *tip) {
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();
    long long start_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;

    struct addr_info *ainfo = tip->ainfo;
    int i, j, k, n, m;
    int *subRAID = (typeof(subRAID)) malloc(sizeof(typeof(*subRAID)) * ainfo->r);  //需要修复的6个PARTITION

    int **disks, **offsets; //6个PARTITION分别对应的存活磁盘和偏移

    disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * ainfo->r);
    offsets = (typeof(offsets)) malloc(sizeof(typeof(*offsets)) * ainfo->r);

    for (i = 0; i < ainfo->r; i++) {
        disks[i] = (typeof(*disks)) malloc(sizeof(typeof(**disks)) * (ainfo->k - 1));
        offsets[i] = (typeof(*offsets)) malloc(sizeof(typeof(**offsets)) * (ainfo->k - 1));
    }


    int hostName, logicAddr, size;
    char op;
    double timeStamp;

    int spareOffset = 0;
    for(i = 0; i < ainfo->r; i++) {
        subRAID[i] = diskRegion[ainfo->failedDisk][i];

        j = 0;

        for(k = 0; k < ainfo->k; k++) {
            if(diskArray[subRAID[i]][k] == ainfo->failedDisk)
                continue;

            disks[i][j] = diskArray[subRAID[i]][k];
            offsets[i][j] = offsetArray[subRAID[i]][k];
            j++;

        }
    }

    int spareDisks[MAX_DEVICE_NUM] = {0};
    int spareMap[MAX_DEVICE_NUM], spareDiskNum = ainfo->disk_nums;
    for (j = 0; j < ainfo->r; j++) {
        for (k = 0; k < ainfo->k - 1; k++) {
            spareDisks[disks[j][k]] = 1;
            spareDiskNum--;
        }
    }
    i = 0;
    spareDiskNum = ainfo->disk_nums;
    for (j = 0; j < ainfo->disk_nums; j++) {
//        if (spareDisks[j] == 0)
            spareMap[i++] = j;
    }
 //   if (i != spareDiskNum) {
 //       fprintf(stderr, "error disk_nums %d, %d\n", i, spareDiskNum);
 //       exit(1);
 //   }

    FILE *f = fopen(ainfo->trace_fn, "r");

    int max = 100;
    int step = (int) (ainfo->strips_partition / 100.0);

    if (step == 0) {
        max = ainfo->strips_partition;
        step = 1;
    }

    tip->bs->left_stripes = ainfo->max_stripes;
    fprintf(stderr, "start recover [parity-declustering], total size %fGB, strips_partition %lld\n", ainfo->r * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024, ainfo->strips_partition);

    for(i = 0; i < ainfo->strips_partition; i++) {
        if ((i) % step == 0)
            fprintf(stderr, "progress %d/%d\n", (i) / step, max);

        int req_count = 0;

        for(j = 0; j < ainfo->r; j++) {
            if (processed_stripes != 0 && processed_stripes % ainfo->max_stripes == 0) {   //du64_to_sec(gettime() - last_time) >= 10
                // printf("time wait\n");
                pthread_mutex_lock(&tip->mutex);
                tip->send_wait = 1;
                tip->wait_all_finish = 1;

                if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                    fatal("pthread_cond_wait", ERR_SYSCALL,
                          "time cond wait failed\n");
                    /*NOTREACHED*/
                }

                last_time = gettime();
                tip->bs->left_stripes = ainfo->max_stripes;
                pthread_mutex_unlock(&tip->mutex);
            }

            for(k = 0; k < ainfo->k - 1; k++) {

                reqs[req_count].type = 1;
                reqs[req_count].disk_num = disks[j][k];
                reqs[req_count].offset = (offsets[j][k] * ainfo->blocks_partition + i * ainfo->blocks_per_strip) * BLOCK;
                reqs[req_count].size = ainfo->strip_size;
                reqs[req_count].stripe_id = processed_stripes % ainfo->max_stripes;
                req_count++;
            }

            tip->bs->left_nums[processed_stripes % ainfo->max_stripes] = ainfo->k - 1;
            tip->bs->disk_dst[processed_stripes % ainfo->max_stripes] = spareMap[spareOffset % spareDiskNum];
            tip->bs->offset_dst[processed_stripes % ainfo->max_stripes] = (ainfo->data_blocks + spareOffset / spareDiskNum * ainfo->blocks_per_strip) * BLOCK;

            spareOffset++;
            processed_stripes++;
        }

        iocbs_map(tip, list, reqs, req_count, 0);

        int ndone = io_submit(tip->ctx, req_count, list);

        if (ndone != req_count) {
            fatal("io_submit", ERR_SYSCALL,
                  "%d: io_submit(%d:%ld) failed (%s)\n",
                  tip->cpu, req_count, ndone,
                  strerror(labs(ndone)));
            /*NOTREACHED*/
        }

        pthread_mutex_lock(&tip->mutex);
        tip->naios_out += ndone;
        assert(tip->naios_out <= naios);

        if (tip->reap_wait) {
            tip->reap_wait = 0;
            pthread_cond_signal(&tip->cond);
        }

        pthread_mutex_unlock(&tip->mutex);

        int reqest_count = 0;

        while (reqest_count < ainfo->requestsPerSecond) {
            int retCode;
            retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);

            if (retCode != 5) {
                fclose(f);
                f = fopen(ainfo->trace_fn, "r");
                retCode = fscanf(f, "%d,%d,%d,%c,%lf", &hostName, &logicAddr, &size, &op, &timeStamp);
            }

            logicAddr = (logicAddr / 8) % ainfo->capacity_total;
            oi_raid_request(tip, logicAddr, size, op);
            reqest_count++;

            long long cur_time = gettime();
            long long time_diff = (long long) (timeStamp * 1000 * 1000 * 1000) - (cur_time - start_time);

            if (time_diff < 0) {
                break;
            }
        }


    }


    free(subRAID);

    for (i = 0; i < ainfo->r; i++) {
        free(disks[i]);
        free(offsets[i]);
    }

    free(disks);
    free(offsets);
}
