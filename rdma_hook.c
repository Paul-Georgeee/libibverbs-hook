#define _GNU_SOURCE

#include <infiniband/verbs.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>

#include "hash_table.h"

static int (*real_post_send)(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);
static int (*real_poll_cq)(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
int hook_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);
int hook_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);

static bool init = false;
HashTable* ht = NULL;
static void init_hash_table(void){
    if(!init){
        init = true;
    }
    ht = createTable();
    if(!ht){
        printf("[Warning]: Create hash table failed!\n");
        return;
    }
}


struct ibv_context *ibv_open_device(struct ibv_device *device) {
    static typeof(ibv_open_device) *real_ibv_open_device = NULL;
    if (real_ibv_open_device == NULL) {
        // Retrieve the original function pointer
        real_ibv_open_device = dlvsym(RTLD_NEXT, "ibv_open_device", "IBVERBS_1.1");

        // Check for errors in dlsym
        char *error = dlerror();
        if (error != NULL) {
            fprintf(stderr, "Error in `dlsym`: %s\n", error);
            exit(EXIT_FAILURE);
        }
    }

    struct ibv_context *ctx = real_ibv_open_device(device);
    real_post_send = ctx->ops.post_send;
    ctx->ops.post_send = hook_post_send;
    real_poll_cq = ctx->ops.poll_cq;
    ctx->ops.poll_cq = hook_poll_cq;
    return ctx;
}

struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector){    
    struct ibv_cq_init_attr_ex cq_attr = {
		.cqe = cqe,
		.comp_vector = comp_vector,
		.channel = channel,
		.wc_flags = 0,
		.comp_mask = 0,
		.cq_context = cq_context,
		.wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP
	};
	struct verbs_context *vctx = verbs_get_ctx_op(context, create_cq_ex);

	if (!vctx) {
		errno = EOPNOTSUPP;
		return NULL;
	}

	return (struct ibv_cq*)vctx->create_cq_ex(context, &cq_attr);    
}


int hook_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr){
    if(!init){
        init_hash_table();
    }

    static int wr_count = 0;
	struct ibv_send_wr *tmp = wr;
    
	while (tmp)
	{
        WRInfo info;
        info.original_wr_id = tmp->wr_id; // 保存原始的wr_id
        gettimeofday(&info.post_timestamp, NULL);
        tmp->wr_id = wr_count++;

		int length = 0;
		for(int i = 0; i < wr->num_sge; ++i)
			length += wr->sg_list[i].length;
        info.data_size = length;
        
        //插入hash表
        insert(ht, tmp->wr_id, &info);
        // printf("[INFO]: Insert wr_id=%ld, data_size=%d, post_time=%ld us into hash table\n", tmp->wr_id, info.data_size, info.post_timestamp.tv_usec);
		tmp = tmp->next;
	}

	int ret = real_post_send(qp, wr, bad_wr);
    
    //返回之前还原wr_id
    tmp = wr;
    while (tmp){
        tmp->wr_id = search(ht, tmp->wr_id)->original_wr_id;
        tmp = tmp->next;
    }
    return ret;
}


int hook_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc){
    if(!init){
        init_hash_table();
    }
    if(num_entries  <= 0)
        return 0;

    struct timeval cur_ts;
    gettimeofday(&cur_ts, NULL);
    static uint64_t last_soft_ts = 0, last_hard_ts = 0;

    struct ibv_poll_cq_attr attr = {};
    struct ibv_cq_ex *cq_ex = (struct ibv_cq_ex *)cq;
    int ret = cq_ex->start_poll(cq_ex, &attr);;
    if (ret ==  ENOENT){
        cq_ex->end_poll(cq_ex);
        return 0;
    } else if(ret > 0){
        cq_ex->end_poll(cq_ex);
        printf("[ERROR]: ibv_start_poll return %d\n", ret);
        return ret;
    }

    int i = 0;
    while(i < num_entries){
        // 通过调用ibv_start_poll来获得硬件时间戳
        uint64_t key = cq_ex->wr_id;
        uint64_t hardware_ts = cq_ex->read_completion_ts(cq_ex);

        // 填充ibv_wc结构体
        wc[i].wr_id = key;
        wc[i].status = cq_ex->status;
        wc[i].opcode = cq_ex->read_opcode == NULL ? -1 : cq_ex->read_opcode(cq_ex);
        wc[i].vendor_err = cq_ex->read_vendor_err == NULL ? 0 : cq_ex->read_vendor_err(cq_ex);
        wc[i].byte_len = cq_ex->read_byte_len == NULL ? 0 : cq_ex->read_byte_len(cq_ex);
        wc[i].imm_data = cq_ex->read_imm_data == NULL ? 0 : cq_ex->read_imm_data(cq_ex);
        wc[i].qp_num = cq_ex->read_qp_num == NULL ? 0 : cq_ex->read_qp_num(cq_ex);  
        wc[i].src_qp = cq_ex->read_src_qp == NULL ? 0 : cq_ex->read_src_qp(cq_ex);
        wc[i].wc_flags = cq_ex->read_wc_flags == NULL ? 0 : cq_ex->read_wc_flags(cq_ex);
        wc[i].slid = cq_ex->read_slid == NULL ? 0 : cq_ex->read_slid(cq_ex);
        wc[i].sl = cq_ex->read_sl == NULL ? 0 : cq_ex->read_sl(cq_ex);
        wc[i].dlid_path_bits = cq_ex->read_dlid_path_bits == NULL ? 0 : cq_ex->read_dlid_path_bits(cq_ex);
        wc[i].pkey_index = 0; // 未使用

        if(wc[i].opcode != IBV_WC_RECV){ // 只处理发送的wr_id
            WRInfo* info = search(ht, key);
            if(info){
                uint64_t soft_ts_diff = cur_ts.tv_usec - last_soft_ts, hard_ts_diff = hardware_ts - last_hard_ts;
                double thput = (double)info->data_size * 8 / (cur_ts.tv_usec - info->post_timestamp.tv_usec) / 1000; // Gbps
                printf("[INFO]: wr_id=%ld, data_size=%d, post_time(software)=%ld, complete_time(software)=%ld, latency(complete_time - post_time)=%ld us, throughput %lf Gbps, difference between the software timestamps of two adjacent CQEs %lu us, hardware timestamp diff %lu ns\n\n",
                    key, info->data_size, info->post_timestamp.tv_usec, cur_ts.tv_usec, cur_ts.tv_usec - info->post_timestamp.tv_usec, thput, last_hard_ts == 0 ? 0 : soft_ts_diff, last_hard_ts == 0 ? 0 : hard_ts_diff);
                wc[0].wr_id = info->original_wr_id;
                delete(ht, key);
                
                last_hard_ts = hardware_ts;
                last_soft_ts = cur_ts.tv_usec;
            }else{
                printf("[WARN]: Cannot find wr_id=%ld in hash table!\n", wc[0].wr_id);
            }
        }

        ++i;
        if(i >= num_entries)
            break;

        //获取下一个cqe
        ret = cq_ex->next_poll(cq_ex);
        if (ret == ENOENT)
            break;
        else if (ret < 0){
            cq_ex->end_poll(cq_ex);
            printf("[ERROR]: ibv_next_poll return %d\n", ret);
            return ret; 
        }
    }

    cq_ex->end_poll(cq_ex);
    return i;
}

