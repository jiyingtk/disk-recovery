
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

	int **bibd, **spd;
	int b, v, r, k, lambda;
	int g;

	FILE *trace_f;
};

int requestPerSecond = 0;

int **diskArray;           //子阵列对应的 磁盘号
int **offsetArray;         //子阵列对应的 偏移量（分区号）
int **diskRegion;			//每个disk包含的Region号

void makeSubRAID(struct addr_info *ainfo);


void init_parameters(struct addr_info *ainfo) {
	ainfo->disk_nums = ainfo->v * ainfo->g;
	ainfo->blocks_per_strip = ainfo->strip_size / BLOCK;
	ainfo->stripe_nums = ainfo->v * ainfo->g * ainfo->r * (ainfo->g - 1) / ainfo->k;

	ainfo->capacity /= ainfo->strip_size;

	addr_type spareSize = (ainfo->capacity + ainfo->disk_nums - 1) / ainfo->disk_nums;
	ainfo->strips_partition = (ainfo->capacity - spareSize) / (ainfo->g * ainfo->r);
	ainfo->blocks_partition = ainfo->strips_partition * ainfo->blocks_per_strip;

	ainfo->data_blocks = ainfo->capacity - spareSize;
	ainfo->data_blocks *= ainfo->blocks_per_strip;

	spareSize *= ainfo->strip_size;
	ainfo->capacity *= ainfo->strip_size;

	fprintf(stderr, "spareSize %fGB, capacity %fGB\n", spareSize * 1.0f / 1024 / 1024 / 1024, ainfo->capacity * 1.0f / 1024 / 1024 / 1024);

	if (ainfo->method == 0) {	//RAID5
		ainfo->capacity_total = ainfo->capacity / BLOCK * (ainfo->g - 1) * ainfo->v;
	}
	else {
		ainfo->capacity_total = ainfo->stripe_nums * (ainfo->k - 1) * ainfo->blocks_partition;
	}

}

void init_addr_info(struct addr_info *ainfo) {
	char fn[128];
	sprintf(fn, "%d_%d.bibd", ainfo->v, ainfo->k);
	FILE *bibd_f = fopen(fn, "r");

	fscanf(bibd_f, "%d %d %d %d %d", &ainfo->b, &ainfo->v, &ainfo->r, &ainfo->k, &ainfo->lambda);

	init_parameters(ainfo);

	int i, j;
	int stripe_nums = ainfo->stripe_nums;
	
	diskArray = (typeof(diskArray)) malloc(sizeof(typeof(*diskArray)) * stripe_nums);
	offsetArray = (typeof(offsetArray)) malloc(sizeof(typeof(*offsetArray)) * stripe_nums);
	for (i = 0; i < stripe_nums; i++) {
		diskArray[i] = (typeof(*diskArray)) malloc(sizeof(typeof(**diskArray)) * ainfo->k);
		offsetArray[i] = (typeof(*offsetArray)) malloc(sizeof(typeof(**offsetArray)) * ainfo->k);
	}
	
	diskRegion = (typeof(diskRegion)) malloc(sizeof(typeof(*diskRegion)) * ainfo->v * ainfo->g);
	for (i = 0; i < ainfo->v * ainfo->g; i++) {
		diskRegion[i] = (typeof(*diskRegion)) malloc(sizeof(typeof(**diskRegion)) * ainfo->g * ainfo->r);
	}

	int **bibd, **spd;
	bibd = (typeof(bibd)) malloc(sizeof(typeof(*bibd)) * ainfo->b);
	for (i = 0; i < ainfo->b; i++) {
		bibd[i] = (typeof(*bibd)) malloc(sizeof(typeof(**bibd)) * ainfo->k);
		for (j = 0; j < ainfo->k; j++) {
			fscanf(bibd_f, "%d", &bibd[i][j]);
		}
	}

	spd = (typeof(spd)) malloc(sizeof(typeof(*spd)) * ainfo->g * (ainfo->g - 1));
	for (i = 0; i < ainfo->g * (ainfo->g - 1); i++) {
		spd[i] = (typeof(*spd)) malloc(sizeof(typeof(**spd)) * ainfo->k);
		for (j = 0; j < ainfo->k; j++) {
			int a, b;
			a = i / ainfo->g;
			b = i % ainfo->g;
			spd[i][j] = (b + a * j) % ainfo->g;
		}
	}

	ainfo->bibd = bibd;
	ainfo->spd = spd;

	makeSubRAID(ainfo);
}

void destroy_addr_info(struct addr_info *ainfo) {
	int i;
	int stripe_nums = ainfo->v * ainfo->g * ainfo->r * (ainfo->g - 1) / ainfo->k;
	
	for (i = 0; i < stripe_nums; i++) {
		free(diskArray[i]);
		free(offsetArray[i]);
	}
	free(diskArray);
	free(offsetArray);
	
	for (i = 0; i < ainfo->v * ainfo->g; i++) {
		free(diskRegion[i]);
	}
	free(diskRegion);

	int **bibd = ainfo->bibd;
	for (i = 0; i < ainfo->b; i++) {
		free(bibd[i]);
	}
	free(bibd);

	int **spd = ainfo->spd;
	for (i = 0; i < ainfo->g * (ainfo->g - 1); i++) {
		free(spd[i]);
	}
	free(spd);

	free(ainfo);
}

void makeSubRAID(struct addr_info *ainfo) {
	int i,j,k;
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

	for(i = 0; i < ainfo->b; i++){
		for(j = 0; j < ainfo->g * (ainfo->g - 1); j++){
			for(k = 0; k < ainfo->k; k++){
				int a = bibd[i][k];
				int b = spd[j][k];
				bd[i * ainfo->g * (ainfo->g - 1) + j][k] = ainfo->g * a + b;
			}
		}
	}

	for(i = 0; i < stripe_nums; i++){
		for(j = 0; j < ainfo->k; j++){
			diskArray[i][j] = bd[i][j];
			offsetArray[i][j] = disk[bd[i][j]];
			diskRegion[bd[i][j]][disk[bd[i][j]]] = i;
			disk[bd[i][j]]++;

			if((disk[bd[i][j]] + 1) % ainfo->g == 0){
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


void oi_sub_raid_request(struct addr_info *ainfo, int subRAIDAddr, int disks[] , int offsets[], int reqSize, char op){
	int dataDiskNum = 2;

	int stripeId;
	int inStripeAddr, inBlockId;       //data的位置，在条带内部
	int blockId[4], diskId[4], sectorId[4];     //全磁盘， 可能涉及到4个块，1个data和3个parity
	int reqBlockNum;

	int virDiskId[2]; //虚拟磁盘号：0,1或2

	int groupId, regionId;     //修改的数据或global parity所在的组号
	int inRegionX, inRegionY;

	int localX;   //对应的local parity的相对磁盘号，相对region号都是2

	// struct disksim_request dataReq;
	// struct disksim_request parityReq[3];

	if(reqSize % BLOCK == 0){
		reqBlockNum = reqSize / BLOCK;
	} else {
		reqBlockNum = reqSize / BLOCK + 1;
	}

	int i;
	for(i = 0; i < reqBlockNum; i++){
		stripeId = subRAIDAddr / ((dataDiskNum + 1) * dataDiskNum);

		inStripeAddr = subRAIDAddr % ((dataDiskNum + 1) * dataDiskNum);
		inBlockId = inStripeAddr / (dataDiskNum + 1);


	    virDiskId[0] = inStripeAddr % (dataDiskNum + 1);
	    diskId[0] = disks[virDiskId[0]];
		if(virDiskId[0] >= dataDiskNum - inBlockId){  //****这里就完成了轮转
			inBlockId += 1;
		}
		blockId[0] = offsets[virDiskId[0]] * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;

		// sectorId[0] = blockId[0] * BLOCK2SECTOR;

		fprintf(stderr, "data->disk:%d,block:%d\n", diskId[0], blockId[0]);
		// dataReq.start = t;
		// dataReq.devno = diskId[0];
		// dataReq.blkno = sectorId[0];
		// dataReq.bytecount = BLOCK;

		// if(op == 'r' || op == 'R'){
		// 	dataReq.flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);
		// }

		// if(op == 'w' || op == 'W'){
		// 	dataReq.flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);
		// 	dataReq.flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);

		// 	//更新parity

		// 	// 1.  global parity
		// 	virDiskId[1] = dataDiskNum - inBlockId;
		// 	diskId[1] = disks[virDiskId[1]];
		// 	blockId[1] = offsets[virDiskId[1]] * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;
		// 	sectorId[1] = blockId[1] * BLOCK2SECTOR;

		// 	//fprintf(stderr, "global parity->disk:%d,block:%d\n", diskId[1], blockId[1]);
		// 	parityReq[0].start = t;
		// 	parityReq[0].devno = diskId[1];
		// 	parityReq[0].blkno = sectorId[1];
		// 	parityReq[0].bytecount = BLOCK;

		// 	parityReq[0].flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[0]);
		// 	parityReq[0].flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[0]);

		// 	// 2.  data对应的local parity
		// 	groupId = disks[virDiskId[0]] / 3;
		// 	regionId = offsets[virDiskId[0]] / 3;

		// 	inRegionX = disks[virDiskId[0]] % 3;
		// 	inRegionY = offsets[virDiskId[0]] % 3;

		// 	localX = ((inRegionX - inRegionY) + 2) % 3;

		// 	diskId[2] = groupId * 3 + localX;
		// 	blockId[2] = (regionId * 3 + 2) * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;
		// 	sectorId[2] = blockId[2] * BLOCK2SECTOR;

		// 	//fprintf(stderr, "data local parity->disk:%d,block:%d\n", diskId[2], blockId[2]);
		// 	parityReq[1].start = t;
		// 	parityReq[1].devno = diskId[2];
		// 	parityReq[1].blkno = sectorId[2];
		// 	parityReq[1].bytecount = BLOCK;

		// 	parityReq[1].flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[1]);
		// 	parityReq[1].flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[1]);

		// 	// 3.  global parity对应的local parity

		// 	groupId = disks[virDiskId[1]] / 3;
		// 	regionId = offsets[virDiskId[1]] / 3;

		// 	inRegionX = disks[virDiskId[1]] % 3;
		// 	inRegionY = offsets[virDiskId[1]] % 3;

		// 	localX = ((inRegionX - inRegionY) + 2) % 3;

		// 	diskId[3] = groupId * 3 + localX;
		// 	blockId[3] = (regionId * 3 + 2) * ainfo->blocks_partition + stripeId * (dataDiskNum + 1) + inBlockId;
		// 	sectorId[3] = blockId[3] * BLOCK2SECTOR;

		// 	//fprintf(stderr, "parity local parity->disk:%d,block:%d\n", diskId[3], blockId[3]);
		// 	parityReq[2].start = t;
		// 	parityReq[2].devno = diskId[3];
		// 	parityReq[2].blkno = sectorId[3];
		// 	parityReq[2].bytecount = BLOCK;

		// 	parityReq[2].flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[2]);
		// 	parityReq[2].flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq[2]);
		// }
		subRAIDAddr++;
	}
}

//访问oi-raid
void oi_raid_request(struct addr_info *ainfo, int logicAddr, int reqSize, char op ){
	int i;
	int subRAIDId;
	int subRAIDAddr;

	int reqBlockNum;

	int disks[3], offsets[3];

	subRAIDId = logicAddr / (ainfo->blocks_partition * (ainfo->k - 1));
	subRAIDAddr = logicAddr % (ainfo->blocks_partition * (ainfo->k - 1));

	if(reqSize % BLOCK == 0){
		reqBlockNum = reqSize / BLOCK;
	} else {
		reqBlockNum = reqSize / BLOCK + 1;
	}

	for(i = 0; i < 3; i++){
		disks[i] = diskArray[subRAIDId][i];
		offsets[i] = offsetArray[subRAIDId][i];
	}

	if(subRAIDAddr + reqBlockNum <= (ainfo->blocks_partition * (ainfo->k - 1))){
		oi_sub_raid_request(ainfo, subRAIDAddr, disks, offsets, reqSize, op);
	} else {
		int reqSizeFirst, reqSizeLast;
		reqSizeFirst = ((ainfo->blocks_partition * (ainfo->k - 1)) - subRAIDAddr) * BLOCK;
		oi_sub_raid_request(ainfo, subRAIDAddr, disks, offsets, reqSizeFirst, op);

		for(i = 0; i < 3; i++){
			disks[i] = diskArray[subRAIDId + 1][i];
			offsets[i] = offsetArray[subRAIDId + 1][i];
		}
		reqSizeLast = (subRAIDAddr + reqBlockNum - (ainfo->blocks_partition * (ainfo->k - 1))) * BLOCK;
		oi_sub_raid_request(ainfo, subRAIDAddr, disks, offsets, reqSizeLast, op);
	}
}


// 访问标准RAID5，参数：disksim句柄，数据盘数，block偏移，request大小，时间，操作。
void raid5_std(int dataDiskNum, int logicAddr, int reqSize, char op ){
	int stripeId;				     //所在条带号
	int inStripeAddr, inBlockId;     //在条带内部
	int blockId, diskId, sectorId;             //全磁盘
	int reqBlockNum;

	// struct disksim_request dataReq;
	// struct disksim_request parityReq;

	if(reqSize % BLOCK == 0){
		reqBlockNum = reqSize / BLOCK;
	} else {
		reqBlockNum = reqSize / BLOCK + 1;
	}

	int i;
	for(i = 0; i < reqBlockNum; i++){
		stripeId = logicAddr / ((dataDiskNum + 1) * dataDiskNum);

		inStripeAddr = logicAddr % ((dataDiskNum + 1) * dataDiskNum);
		inBlockId = inStripeAddr / (dataDiskNum + 1);

		diskId = inStripeAddr % (dataDiskNum + 1);
		if(diskId >= dataDiskNum - inBlockId){  //****这里就完成了轮转
			inBlockId += 1;
		}
		blockId = stripeId * (dataDiskNum + 1) + inBlockId;

		// sectorId = blockId * BLOCK2SECTOR;

		// dataReq.start = t;
		// dataReq.devno = diskId;
		// dataReq.blkno = sectorId;
		// dataReq.bytecount = BLOCK;

		// if(op == 'r' || op == 'R'){
		// 	dataReq.flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);
		// }

		// if(op == 'w' || op == 'W'){
		// 	dataReq.flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);
		// 	dataReq.flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &dataReq);

		// 	//更新parity
		// 	parityReq.start = t;
		// 	parityReq.blkno = sectorId;
		// 	parityReq.devno = dataDiskNum - inBlockId;
		// 	parityReq.bytecount = BLOCK;

		// 	parityReq.flags = DISKSIM_READ;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq);
		// 	parityReq.flags = DISKSIM_WRITE;
		// 	disksim_interface_request_arrive(disksim, t, &parityReq);
		// }
		logicAddr++;
	}
}

//访问21个磁盘的RAID5盘阵，每3个磁盘为一个2+1的RAID5
void raid5_3time7disks_request(struct addr_info *ainfo, int logicAddr, int reqSize, char op )
{
	int dataDiskNum = 2;
	int dataPerStripe = (dataDiskNum + 1) * dataDiskNum;
	int maxOffset, reqBlockNum;
	int stripeId, groupId, inStripeAddr, inBlockId, diskId, blockId, sectorId; 

	maxOffset = ainfo->capacity_total;

 //    struct disksim_request dataReq;
	// struct disksim_request parityReq;

	if(reqSize % BLOCK == 0){
		reqBlockNum = reqSize / BLOCK;
	} else {
		reqBlockNum = reqSize / BLOCK + 1;
	}

	int i;
	for(i = 0; i < reqBlockNum; i++){
		if (logicAddr < maxOffset){
			stripeId = logicAddr / (dataPerStripe * 7);
			groupId = (logicAddr % (dataPerStripe *7)) / dataPerStripe;
			inStripeAddr = logicAddr % dataPerStripe;
			inBlockId = inStripeAddr / (dataDiskNum + 1);

			diskId = inStripeAddr % (dataDiskNum + 1);
			if (diskId >= dataDiskNum - inBlockId){  //****这里就完成了轮转
				inBlockId += 1;
			}
			diskId += groupId * 3;
			blockId = stripeId * (dataDiskNum + 1) + inBlockId;
			// sectorId = blockId * BLOCK2SECTOR;

			// dataReq.start = t;
			// dataReq.devno = diskId;
			// dataReq.blkno = sectorId;
			// dataReq.bytecount = BLOCK;

			// //fprintf(stderr, "data disk:%d\n", diskId);

			// if (op == 'r' || op == 'R'){
			// 	dataReq.flags = DISKSIM_READ;
			// 	disksim_interface_request_arrive(disksim, t, &dataReq);
			// }

			// if (op == 'w' || op == 'W'){
			// 	dataReq.flags = DISKSIM_READ;
			// 	disksim_interface_request_arrive(disksim, t, &dataReq);
			// 	dataReq.flags = DISKSIM_WRITE;
			// 	disksim_interface_request_arrive(disksim, t, &dataReq);

			// 	//更新parity
			// 	parityReq.start = t;
			// 	parityReq.blkno = sectorId;
			// 	parityReq.devno = dataDiskNum - inBlockId + groupId * 3;
			// 	parityReq.bytecount = BLOCK;

			// 	//fprintf(stderr, "parity %d\n", parityReq.devno);

			// 	parityReq.flags = DISKSIM_READ;
			// 	disksim_interface_request_arrive(disksim, t, &parityReq);
			// 	parityReq.flags = DISKSIM_WRITE;
			// 	disksim_interface_request_arrive(disksim, t, &parityReq);
			// }
			logicAddr++;
		}
	}
}

//g=k
// 21个磁盘，部署7组传统2+1 RAID5，假定每个磁盘6个PARTITION
void raid5_online_recover(struct thr_info *tip){
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();

    struct request_info reqs[MAX_DEVICE_NUM];

    long long processed_stripes = 0;


	struct addr_info *ainfo = tip->ainfo;
	int i, j, k, m;

	int groupId, inGroupId; //坏盘所在组，以及在组内的磁盘编号
	int *disks = (typeof(disks)) malloc(sizeof(typeof(*disks)) * (ainfo->g - 1));   //对应的2个存活磁盘

	int hostName, logicAddr, size;
	char op;
	float timeStamp;

	groupId = ainfo->failedDisk / ainfo->g;
	inGroupId = ainfo->failedDisk % ainfo->g;

	j = 0;
	for(i = 0; i < ainfo->g; i++){
		if(inGroupId == i)
			continue;
		disks[j] = groupId * ainfo->g + i;
		j++;
	}

	int max = 100;
	int step = (int) (ainfo->r * (ainfo->g - 1) * ainfo->strips_partition / 100.0);
	if (step == 0) {
		max = ainfo->r * (ainfo->g - 1) * ainfo->strips_partition;
		step = 1;
	}

	tip->bs->left_stripes = CACHED_STRIPE_NUM;
	fprintf(stderr, "start recover [raid5], total size %fGB\n", ainfo->r * (ainfo->g - 1) * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);
	for(i = 0; i < ainfo->r * (ainfo->g - 1); i++){
		for(j = 0; j < ainfo->strips_partition; j++){
			if ((i * ainfo->strips_partition + j) % step == 0) {
				int cur = (i * ainfo->strips_partition + j) / step;
				fprintf(stderr, "progress %d/%d\n", cur, max);
			}

	        if (processed_stripes != 0 && processed_stripes % CACHED_STRIPE_NUM == 0) {   //du64_to_sec(gettime() - last_time) >= 10
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
				tip->bs->left_stripes = CACHED_STRIPE_NUM;
	            pthread_mutex_unlock(&tip->mutex);
	        }

	        int ntodo = ainfo->g - 1, ndone;
			for(k = 0; k < ainfo->g - 1; k++){
		        reqs[k].type = 1;
		        reqs[k].disk_num = disks[k];
		        reqs[k].offset = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
		        reqs[k].size = ainfo->strip_size;
		        reqs[k].stripe_id = processed_stripes % CACHED_STRIPE_NUM;

				// readReq[k].start = now;
				// readReq[k].devno = disks[k];
				// readReq[k].blkno = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK2SECTOR;
				// readReq[k].bytecount = ainfo->strip_size;
				// readReq[k].flags = DISKSIM_READ;
				// disksim_interface_request_arrive(disksim, now, &readReq[k]);
			}
			tip->bs->left_nums[processed_stripes % CACHED_STRIPE_NUM] = ainfo->g - 1;
			tip->bs->disk_dst[processed_stripes % CACHED_STRIPE_NUM] = ainfo->failedDisk;
			tip->bs->offset_dst[processed_stripes % CACHED_STRIPE_NUM] = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK;
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


			for (m = 0; m < requestPerSecond; m++){
				fscanf(ainfo->trace_f, "%d,%d,%d,%c,%f", &hostName, &logicAddr, &size, &op, &timeStamp);
				while (hostName == 1 || hostName == 3 || hostName == 5){
					fscanf(ainfo->trace_f, "%d,%d,%d,%c,%f", &hostName, &logicAddr, &size, &op, &timeStamp);;
				}
				if (logicAddr >= ainfo->capacity_total){
					continue;
				}
				raid5_3time7disks_request(ainfo, logicAddr, size, op);
			}

			processed_stripes++;
			// writeReq.start = now;
			// writeReq.devno = failedDisk;
			// writeReq.blkno = (i * ainfo->blocks_partition + j * ainfo->blocks_per_strip) * BLOCK2SECTOR;
			// writeReq.bytecount = ainfo->strip_size;
			// writeReq.flags = DISKSIM_WRITE;
			// disksim_interface_request_arrive(disksim, now, &writeReq);
		}
	}

	free(disks);
}

//oi-raid单盘修复
void oi_raid_online_recover(struct thr_info *tip){
    struct iocb *list[MAX_DEVICE_NUM];
    long long last_time = gettime();

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
    float timeStamp;

	int spareOffset = 0;

	for(i = 0; i < ainfo->r * (ainfo->g - 1); i++){
		int ii = i / (ainfo->g - 1);
		ii = ii * ainfo->g + i % (ainfo->g - 1);
		subRAID[i] = diskRegion[ainfo->failedDisk][ii];
		//fprintf(stderr, "%d ", subRAID[i]);
		j = 0;
		for(k = 0; k < ainfo->k; k++){
			if(diskArray[subRAID[i]][k] == ainfo->failedDisk)
				continue;
			disks[i][j] = diskArray[subRAID[i]][k];
			offsets[i][j] = offsetArray[subRAID[i]][k];
			j++;
		}
	}

// int map[21] = {0};
// for (i = 0; i < ainfo->r * (ainfo->g - 1); i++) {
// 	for (k = 0; k < ainfo->k - 1; k++){
// 		map[disks[i][k]] = 1;
// 	}
// }
// int mm = 1;
// for (i = 0; i < 21; i++) {
// 	if (map[i] == 1)
// 		map[i] = mm++;
// }
// if (mm != 13) {
// 	fprintf(stderr, "error mm is %d\n", mm);
// 	exit(1);
// }

	int max = 100;
	int step = (int) (ainfo->strips_partition / 100.0);
	if (step == 0) {
		max = ainfo->strips_partition;
		step = 1;
	}

	tip->bs->left_stripes = CACHED_STRIPE_NUM;
	fprintf(stderr, "start recover [oi-raid], total size %fGB\n", ainfo->r * (ainfo->g - 1) * ainfo->strips_partition * ainfo->strip_size * 1.0f / 1024 / 1024 / 1024);
	for(i = 0; i < ainfo->strips_partition; i++){
		if ((i) % step == 0)
			fprintf(stderr, "progress %d/%d\n", (i) / step, max);

		int req_count = 0;

		for(j = 0; j < ainfo->r * (ainfo->g - 1); j++){
	        if (processed_stripes != 0 && processed_stripes % CACHED_STRIPE_NUM == 0) {   //du64_to_sec(gettime() - last_time) >= 10
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
				tip->bs->left_stripes = CACHED_STRIPE_NUM;
		        pthread_mutex_unlock(&tip->mutex);
	        }
// int skip = 0;
			for(k = 0; k < ainfo->k - 1; k++){
// if (k == 1 && disks[j][k] >= 18) {
// 	skip = 1;
// 	continue;
// }

				reqs[req_count].type = 1;
// reqs[req_count].disk_num = map[disks[j][k]];
		        reqs[req_count].disk_num = disks[j][k];
		        reqs[req_count].offset = (offsets[j][k] * ainfo->blocks_partition + i*ainfo->blocks_per_strip) * BLOCK;
		        reqs[req_count].size = ainfo->strip_size;
		        reqs[req_count].stripe_id = processed_stripes % CACHED_STRIPE_NUM;
		        req_count++;

				// readReq[j][k].start = now;
				// readReq[j][k].devno = disks[j][k];
				// readReq[j][k].blkno = (offsets[j][k] * ainfo->blocks_partition + i*ainfo->blocks_per_strip) * BLOCK2SECTOR;
				// readReq[j][k].bytecount = ainfo->strip_size;
				// readReq[j][k].flags = DISKSIM_READ;
				// disksim_interface_request_arrive(disksim, now, &readReq[j][k]);
			}
// if (skip) 
// 			tip->bs->left_nums[processed_stripes % CACHED_STRIPE_NUM] = ainfo->k - 2;
// else

			tip->bs->left_nums[processed_stripes % CACHED_STRIPE_NUM] = ainfo->k - 1;
// tip->bs->disk_dst[processed_stripes % CACHED_STRIPE_NUM] = spareOffset % 11;
			tip->bs->disk_dst[processed_stripes % CACHED_STRIPE_NUM] = spareOffset % ainfo->disk_nums;
			tip->bs->offset_dst[processed_stripes % CACHED_STRIPE_NUM] = (ainfo->data_blocks + spareOffset / ainfo->disk_nums * ainfo->blocks_per_strip) * BLOCK;

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

		for (m = 0; m < requestPerSecond; m++){
			fscanf(ainfo->trace_f, "%d,%d,%d,%c,%f", &hostName, &logicAddr, &size, &op, &timeStamp);
			while (hostName == 1 || hostName == 3 || hostName == 5){
				fscanf(ainfo->trace_f, "%d,%d,%d,%c,%f", &hostName, &logicAddr, &size, &op, &timeStamp);
			}
			if (logicAddr < ainfo->capacity_total){
				oi_raid_request(ainfo, logicAddr, size, op);	
			}
		}


		// for(n = 0; n < 6; n++){
			// writeReq[n].start = now;
			// writeReq[n].devno = spareOffset % ainfo->disk_nums;
			// writeReq[n].blkno = (ainfo->data_blocks + spareOffset / ainfo->disk_nums * ainfo->blocks_per_strip) * BLOCK2SECTOR;
			// spareOffset++;
			// writeReq[n].bytecount = ainfo->strip_size;
			// writeReq[n].flags = DISKSIM_WRITE;
			// disksim_interface_request_arrive(disksim, now, &writeReq[n]);
		// }
	}

	
	free(subRAID);
	for (i = 0; i < ainfo->r * (ainfo->g - 1); i++) {
		free(disks[i]);
		free(offsets[i]);
	}
	free(disks);
	free(offsets);
}

// int main(int argc, char *argv[])
// {

// 	if (argc != 6 ) {
// 		fprintf(stderr, "usage: %s <param file> <output file> <traceName> <failedDisk> <type : 0 for raid5, 1 for oiraid>\n",
// 			  argv[0]);
// 		exit(1);
// 	}


// 	char *traceName = argv[3];
// 	int failedDiskId = atoi(argv[4]);
// 	int type = atoi(argv[5]); 

// 	FILE *fin;

// 	if ((fin=fopen(traceName,"r")) == NULL){
// 		fprintf(stderr, "trace file open failed\n");
// 		exit(0);
// 	}

// 	if (type == 0){
// 		//fprintf(stderr, "requestPerSecond:%d\n", requestPerSecond);
// 		raid5_online_recover(disksim, failedDiskId, fin);
// 	} else if (type == 1) {
// 		//fprintf(stderr, "requestPerSecond:%d\n", requestPerSecond);
// 		oi_raid_online_recover(disksim, failedDiskId, fin);
// 	}

// 	fclose(fin);
	

// 	return 0;
// }
