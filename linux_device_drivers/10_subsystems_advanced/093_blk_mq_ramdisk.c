// SPDX-License-Identifier: GPL-2.0
/*
 * 093_blk_mq_ramdisk.c - A RAM-backed block device using blk-mq.
 *
 * sudo insmod 093_blk_mq_ramdisk.ko size_mb=32
 * sudo mkfs.ext4 /dev/trainram0 && sudo mount /dev/trainram0 /mnt
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/vmalloc.h>
#include <linux/version.h>

#define SECTOR_SHIFT_ 9

static unsigned long size_mb = 16;
module_param(size_mb, ulong, 0444);

struct ramdisk {
	u8 *data;
	sector_t capacity;	/* in 512-byte sectors */
	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
	int major;
};
static struct ramdisk rd;

static blk_status_t rd_queue_rq(struct blk_mq_hw_ctx *hctx,
				const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	loff_t pos = (loff_t)blk_rq_pos(rq) << SECTOR_SHIFT_;
	struct req_iterator iter;
	struct bio_vec bv;
	blk_status_t st = BLK_STS_OK;

	blk_mq_start_request(rq);

	if (blk_rq_pos(rq) + blk_rq_sectors(rq) > rd.capacity) {
		st = BLK_STS_IOERR;
		goto end;
	}

	rq_for_each_segment(bv, rq, iter) {
		void *buf = bvec_kmap_local(&bv);

		switch (req_op(rq)) {
		case REQ_OP_READ:
			memcpy(buf, rd.data + pos, bv.bv_len);
			break;
		case REQ_OP_WRITE:
			memcpy(rd.data + pos, buf, bv.bv_len);
			break;
		default:
			st = BLK_STS_NOTSUPP;
		}
		kunmap_local(buf);
		if (st)
			break;
		pos += bv.bv_len;
	}
end:
	blk_mq_end_request(rq, st);
	return BLK_STS_OK;
}

static const struct blk_mq_ops rd_mq_ops = {
	.queue_rq = rd_queue_rq,
};

static const struct block_device_operations rd_fops = {
	.owner = THIS_MODULE,
};

static int __init rd_init(void)
{
	int ret;

	rd.capacity = (size_mb << 20) >> SECTOR_SHIFT_;
	rd.data = vzalloc(size_mb << 20);
	if (!rd.data)
		return -ENOMEM;

	rd.major = register_blkdev(0, "trainram");
	if (rd.major < 0) {
		ret = rd.major;
		goto err_data;
	}

	rd.tag_set.ops = &rd_mq_ops;
	rd.tag_set.nr_hw_queues = 1;
	rd.tag_set.queue_depth = 128;
	rd.tag_set.numa_node = NUMA_NO_NODE;
	rd.tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ret = blk_mq_alloc_tag_set(&rd.tag_set);
	if (ret)
		goto err_major;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	{
		struct queue_limits lim = { .logical_block_size = 512 };

		rd.disk = blk_mq_alloc_disk(&rd.tag_set, &lim, &rd);
	}
#else
	rd.disk = blk_mq_alloc_disk(&rd.tag_set, &rd);
#endif
	if (IS_ERR(rd.disk)) {
		ret = PTR_ERR(rd.disk);
		goto err_tags;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
	blk_queue_logical_block_size(rd.disk->queue, 512);
#endif

	rd.disk->major = rd.major;
	rd.disk->first_minor = 0;
	rd.disk->minors = 1;
	rd.disk->fops = &rd_fops;
	rd.disk->private_data = &rd;
	snprintf(rd.disk->disk_name, DISK_NAME_LEN, "trainram0");
	set_capacity(rd.disk, rd.capacity);

	ret = add_disk(rd.disk);
	if (ret)
		goto err_disk;
	pr_info("ramdisk: %lu MB /dev/trainram0\n", size_mb);
	return 0;

err_disk:
	put_disk(rd.disk);
err_tags:
	blk_mq_free_tag_set(&rd.tag_set);
err_major:
	unregister_blkdev(rd.major, "trainram");
err_data:
	vfree(rd.data);
	return ret;
}

static void __exit rd_exit(void)
{
	del_gendisk(rd.disk);
	put_disk(rd.disk);
	blk_mq_free_tag_set(&rd.tag_set);
	unregister_blkdev(rd.major, "trainram");
	vfree(rd.data);
}

module_init(rd_init);
module_exit(rd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("blk-mq RAM disk example");
