// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Telechips Inc.
 */

#include <linux/interrupt.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <linux/mailbox/tcc805x_multi_mailbox/tcc805x_multi_mbox.h>
#include <dt-bindings/mailbox/tcc805x_multi_mailbox/tcc_mbox_ch.h>
#include <soc/tcc/chipinfo.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/atomic.h>
#include <uapi/linux/sched/types.h>
//추가한 헤더파일
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/err.h>

#define Hw37		(1LL << 37)
#define Hw36		(1LL << 36)
#define Hw35		(1LL << 35)
#define Hw34		(1LL << 34)
#define Hw33		(1LL << 33)
#define Hw32		(1LL << 32)
#define Hw31		(0x80000000U)
#define Hw30		(0x40000000)
#define Hw29		(0x20000000)
#define Hw28		(0x10000000)
#define Hw27		(0x08000000)
#define Hw26		(0x04000000)
#define Hw25		(0x02000000)
#define Hw24		(0x01000000)
#define Hw23		(0x00800000)
#define Hw22		(0x00400000)
#define Hw21		(0x00200000)
#define Hw20		(0x00100000)
#define Hw19		(0x00080000)
#define Hw18		(0x00040000)
#define Hw17		(0x00020000)
#define Hw16		(0x00010000)
#define Hw15		(0x00008000)
#define Hw14		(0x00004000)
#define Hw13		(0x00002000)
#define Hw12		(0x00001000)
#define Hw11		(0x00000800)
#define Hw10		(0x00000400)
#define Hw9			(0x00000200)
#define Hw8			(0x00000100)
#define Hw7			(0x00000080)
#define Hw6			(0x00000040)
#define Hw5			(0x00000020)
#define Hw4			(0x00000010)
#define Hw3			(0x00000008)
#define Hw2			(0x00000004)
#define Hw1			(0x00000002)
#define Hw0			(0x00000001)
#define HwZERO		(0x00000000)

/*************************************************************************
 *    MailBox Register offset
 ************************************************************************/
#define MBOXTXD(x)		(0x00 + (x) * 4)	/* Transmit cmd fifo */
#define MBOXRXD(x)		(0x20 + (x) * 4)	/* Receive cmd fifo */
#define MBOXCTR			(0x40)
#define MBOXSTR			(0x44)
#define MBOX_DT_STR		(0x50)	/* Transmit data status */
#define MBOX_RT_STR		(0x54)	/* Receive data status */
#define MBOXDTXD		(0x60)	/* Transmit data fifo */
#define MBOXRTXD		(0x70)	/* Receive data fifo */
#define MBOXCTR_SET		(0x74)
#define MBOXCTR_CLR		(0x78)
#define TRM_STS			(0x7C)


/*************************************************************************
 *    MailBox CTR Register Field Define
 ************************************************************************/
/* FLUSH: 6bit, OEN: 5bit, IEN: 4bit, LEVEL: 1~0bit */
/* Clear Tx Done Interrupt */
#define TXD_ICLR_BIT	(Hw21)
/* Enable Tx Done Interrupt */
#define TXD_IEN_BIT		(Hw20)
/* Flush Tx Data FIFO */
#define DF_FLUSH_BIT	(Hw7)
/* Flush Tx Cmd FIFO */
#define CF_FLUSH_BIT	(Hw6)
#define OEN_BIT			(Hw5)
#define IEN_BIT			(Hw4)
#define LEVEL0_BIT		(Hw1)
#define LEVEL1_BIT		(Hw0)

#define OPP_TMN_STS_BIT	(Hw16)
#define OWN_TMN_STS_BIT	(Hw0)

#define MEMP_MASK		(0x00000001)
#define MFUL_MASK		(0x00000002)
#define MCOUNT_MASK		(0x000000F0)
#define SEMP_MASK		(0x00010000)
#define SFUL_MASK		(0x00020000)
#define SCOUNT_MASK		(0x00F00000)

#define DATA_MEMP_MASK	(0x80000000U)
#define DATA_MFUL_MASK	(0x40000000U)
#define DATA_MCOUNT_MASK	(0x0000FFFFU)

#define DATA_SEMP_MASK	(0x80000000U)
#define DATA_SFUL_MASK	(0x40000000)
#define DATA_SCOUNT_MASK	(0x0000FFFF)

#define BSID			(0x46)
#define CID_A72			(0x72)
#define CID_A53			(0x53)

#define MBOX_ID0_LEN	(4U)
#define MBOX_ID1_LEN	(2U)
/* for Codesonar&qac : MBOX_ID0_LEN+MBOX_ID1_LEN */
#define MBOX_ID_MAX_LEN (6U)

#define SSS_MBOX_STATUS_ADDR (0x1E020000)
#define SSS_MBOX_DATA_ADDR (0x1E022000)
#define SSS_MAX_ATTEMPT_COUNT (100U)
#define CHIP_REVISION_ES (0x00000000U)
#define CHIP_REVISION_CS (0x00000001U)

#define TCC_MBOX_CH_DISALBE (0)
#define TCC_MBOX_CH_ENALBE  (1)

#define LOG_TAG	("MULTI_CH_MBOX_DRV")

static int32_t mbox_verbose_mode;
static uint32_t chip_rev;

#define eprintk(dev, msg, ...)	\
	((void)dev_err(dev, "[ERROR][%s]%s: " pr_fmt(msg), \
	(const char *)LOG_TAG, __func__, ##__VA_ARGS__))
#define wprintk(dev, msg, ...)	\
	((void)dev_warn(dev, "[WARN][%s]%s: " pr_fmt(msg), \
	(const char *)LOG_TAG, __func__, ##__VA_ARGS__))
#define iprintk(dev, msg, ...)	\
	((void)dev_info(dev, "[INFO][%s]%s: " pr_fmt(msg), \
	(const char *)LOG_TAG, __func__, ##__VA_ARGS__))
#define dprintk(dev, msg, ...)	\
	{ if (mbox_verbose_mode == 1) { \
	(void)dev_info(dev, "[INFO][%s]%s: " pr_fmt(msg), \
	(const char *)LOG_TAG, __func__, ##__VA_ARGS__); } }

#define d1printk(mdev, dev, msg, ...) \
	{ if (mdev->debug_level > 0) \
	{ (void)dev_info(dev, "[DEBUG][%s]%s: " \
	pr_fmt(msg), (const char *)LOG_TAG, \
	__func__, ##__VA_ARGS__); } }
#define d2printk(mdev, dev, msg, ...) \
	{ if (mdev->debug_level > 1) { \
	(void)dev_info(dev, "[DEBUG][%s]%s: " pr_fmt(msg), \
	(const char *)LOG_TAG, __func__, ##__VA_ARGS__); } }

#if (CLIENT_MBOX_TX_TIMEOUT <= (MBOX_TX_TIMEOUT*TCC_CB_MBOX_MAX)) || \
	(CLIENT_MBOX_TX_TIMEOUT <= (MBOX_TX_TIMEOUT*TCC_MICOM_MBOX0_MAX)) || \
	(CLIENT_MBOX_TX_TIMEOUT <= (MBOX_TX_TIMEOUT*TCC_MICOM_MBOX1_MAX))
#error "CLIENT_MBOX_TX_TIMEOUT value is too small.";
#endif

struct channel_id0 {
	s8 bsid[1];
	s8 cid[1];
	char idName[MBOX_ID1_LEN];
};

struct channel_id1 {
	char idName[MBOX_ID0_LEN];
};

union mbox_header0 {
	struct channel_id0 id;
	uint32_t cmd;
};

union mbox_header1 {
	struct channel_id1 id;
	uint32_t cmd;
};

typedef void (*mbox_receive_queue_t)(const void *dev_id, uint32_t ch,
			struct tcc_mbox_data *msg);

struct mbox_receive_list {
	void *dev_id;
	uint32_t ch;
	union mbox_header0 header0;
	union mbox_header1 header1;
	struct tcc_mbox_data msg;
	struct list_head      queue; //for linked list
};

struct mbox_receiveQueue {
	struct kthread_worker  kworker;
	struct task_struct     *kworker_task;
	struct kthread_work    pump_messages;
	spinlock_t             rx_queue_lock;
	struct list_head       rx_queue;

	mbox_receive_queue_t  handler;
	void                  *handler_pdata;
};

struct mbox_wait_queue {
	wait_queue_head_t cmdQueue;
	int32_t condition;
};

typedef void (*mbox_transmit_queue_t)(struct mbox_chan *chan,
				const struct tcc_mbox_data *mbox_msg);

struct mbox_transmit_list {
	struct mbox_chan *chan;
	struct tcc_mbox_data	msg;
	struct list_head      queue; //for linked list
};

struct mbox_transmitQueue {
	struct kthread_worker  kworker;
	struct task_struct     *kworker_task;
	struct kthread_work    pump_messages;
	spinlock_t             tx_queue_lock;
	struct list_head       tx_queue;

	mbox_transmit_queue_t  handler;
	void                   *handler_pdata;
};

typedef void (*mbox_tx_done_queue_t)(struct mbox_chan *chan, int32_t error);

struct mbox_tx_done_list {
	struct mbox_chan *chan;
	int32_t error;
	struct list_head      queue; //for linked list
};
struct mbox_txDoneQueue {
	struct kthread_worker  kworker;
	struct task_struct     *kworker_task;
	struct kthread_work    pump_messages;
	spinlock_t             queue_lock;
	struct list_head       queue;

	mbox_tx_done_queue_t  handler;
	void                   *handler_pdata;
};

struct tcc_mbox_device {
	struct mbox_controller mbox;
	void __iomem *base;
	int32_t rx_irq;
	int32_t txd_irq;
	struct mutex lock;
	struct mutex sendLock;
	struct mbox_transmitQueue transmitQueue;
	struct mbox_wait_queue waitQueue;
	int32_t debug_level;
};

/**
 * tcc mbox channel data
 *
 * mdev:	Pointer to parent Mailbox device
 * channel:	Channel number pertaining to this container
 * msg:mbox message sturct for rx.
 * receiveQueue: rx queue
 */
struct tcc_channel {
	struct tcc_mbox_device	*mdev;
	uint32_t			channel;

	struct mbox_txDoneQueue txDoneQueue;

	struct tcc_mbox_data *msg;
	struct mbox_receiveQueue receiveQueue;

	union mbox_header0 header0;
	union mbox_header1 header1;
	char mbox_id[MBOX_ID_MAX_LEN+(uint32_t)1];
	int32_t ch_enable;
	atomic_t rx_remain_count;

	uint32_t rx_count;
	uint32_t tx_count;
	uint32_t tx_fail_count;
	const char *client_name;
	struct list_head proc_list;

};

static LIST_HEAD(mbox_proc_list);

static void mbox_mdelay(unsigned long msec_delay);
static void mbox_pump_rx_messages(struct kthread_work *work);
static int32_t mbox_receive_queue_init(
			struct mbox_receiveQueue *mbox_queue,
			mbox_receive_queue_t handler,
			void *handler_pdata,
			const char *name);
static int32_t deregister_receive_queue(
			struct mbox_receiveQueue *mbox_queue);
static int32_t mbox_add_rx_queue_and_work(
			struct mbox_receiveQueue *mbox_queue,
			struct mbox_receive_list *mbox_list);
static void mbox_pump_tx_messages(struct kthread_work *work);
static int32_t mbox_transmit_queue_init(struct mbox_transmitQueue *mbox_queue,
			mbox_transmit_queue_t handler,
			void *handler_pdata, const char *name);
static int32_t deregister_transmit_queue(struct mbox_transmitQueue *mbox_queue);
static int32_t mbox_add_tx_queue_and_work(
			struct mbox_transmitQueue *mbox_queue,
			struct mbox_transmit_list *mbox_list);
static void tcc_mbox_event_create(struct tcc_mbox_device *mdev);
static void tcc_mbox_event_delete(struct tcc_mbox_device *mdev);
static int32_t tcc_mbox_wait_event_timeout(struct tcc_mbox_device *mdev,
		uint32_t timeOut);
static void tcc_mbox_wake_up(struct tcc_mbox_device *mdev);
static int32_t tcc_mbox_send_message(const struct mbox_chan *chan,
			const struct tcc_mbox_data *msg);
static void tcc_multich_mbox_send_worker(struct mbox_chan *chan,
			const struct tcc_mbox_data *msg);
static int32_t tcc_multich_mbox_submit(struct mbox_chan *chan, void *mbox_msg);
static int32_t read_mboxid_from_client(const struct tcc_mbox_device *mdev,
			const struct mbox_client *cl,
			char *mbox_id);
static int32_t add_client_to_chan_info(const struct mbox_chan *chan, const char *mbox_id);
static int32_t tcc_multich_mbox_startup(struct mbox_chan *chan);
static void tcc_multich_mbox_shutdown(struct mbox_chan *chan);
static bool tcc_multich_mbox_last_tx_done(struct mbox_chan *chan);
static void tcc_txdone(struct mbox_chan *chan, int32_t error);
static void tcc_received_msg(const void *dev_id,
			uint32_t ch, struct tcc_mbox_data *msg);
static int32_t tcc_mbox_find_header(const struct mbox_chan *chan,
				const struct mbox_receive_list *mbox_list);
static int32_t mbox_read_fifo(void *dev_id,
			const struct tcc_mbox_device *mdev,
			struct mbox_receive_list *mbox_list);
static irqreturn_t tcc_multich_mbox_rx_irq(int irq, void *dev_id);
static irqreturn_t tcc_multich_mbox_txd_irq(int32_t irq, void *dev_id);
static void change_mbox_irq_affinity(uint32_t irq_number);
static int32_t init_tcc_mbox_device(struct platform_device *pdev,
			struct tcc_mbox_device *mdev);
static int32_t tcc_mbox_register(struct platform_device *pdev,
			struct tcc_mbox_device *mdev);
static int32_t set_mbox_irq(struct platform_device *pdev,
			struct tcc_mbox_device *mdev);
static struct mbox_chan *tcc_multich_mbox_xlate(struct mbox_controller *mbox,
					const struct of_phandle_args *spec);

static ssize_t debug_level_show(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t debug_level_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count);

static void sss_busy_check(void);
static void sss_set_func_id(void);

static void mbox_mdelay(unsigned long msec_delay)
{
	mdelay(msec_delay);
}

static void sss_busy_check(void)
{
	void __iomem *sss_status_vaddr = NULL;
	uint32_t i;

	sss_status_vaddr =
		ioremap_nocache((phys_addr_t)SSS_MBOX_STATUS_ADDR, (ulong)64);

	/* Wait until SSS mbox busy is 0 */
	/* Timeout if there is a delay of more than 100ms */
	for (i = 0; i < SSS_MAX_ATTEMPT_COUNT; i++) {
		if (((readl_relaxed(sss_status_vaddr) & (uint32_t)0x01)
		     == (uint32_t)0x00)) {
			break;
		}
		mbox_mdelay(1);
	}

	if (i >= SSS_MAX_ATTEMPT_COUNT)	{
		(void)pr_err("sss busy cmd error(%d)!\n", i);
	}

	iounmap(sss_status_vaddr);
}

static void sss_set_func_id(void)
{
	void __iomem *sss_data_vaddr = NULL;

	/*Set funcID(0x02) to SSS mailbox */
	sss_data_vaddr =
		ioremap_nocache((phys_addr_t)SSS_MBOX_DATA_ADDR, (ulong)64);
	writel_relaxed((uint32_t)0x02, sss_data_vaddr);
	iounmap(sss_data_vaddr);
}


static ssize_t debug_level_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;

	if ((dev != NULL) && (attr != NULL) && (buf != NULL)) {
		const struct tcc_mbox_device *mdev = dev_get_drvdata(dev);

		count = (ssize_t)scnprintf(buf, PAGE_SIZE, "%d\n", mdev->debug_level);
		(void)dev;
		(void)attr;
	}
	return count;
}

static ssize_t debug_level_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	ssize_t retCnt = 0;

	if ((dev != NULL) && (attr != NULL) && (buf != NULL)) {
		ulong data;
		struct tcc_mbox_device *mdev = dev_get_drvdata(dev);
		int32_t error = kstrtoul(buf, 10, &data);

		if (error == 0) {
			if (data < 3UL) {
				mdev->debug_level = (int32_t)data;
			}
		} else {
			eprintk(mdev->mbox.dev, "store fail (%s)\n", buf);
		}

		(void)dev;
		(void)attr;
	}

	if (count < (ULONG_MAX>>1)) {
		retCnt = (ssize_t)count;
	}

	return retCnt;
}

static DEVICE_ATTR_RW(debug_level);

int32_t show_mbox_channel_info(struct seq_file *p, void *v)
{
	struct tcc_channel *chan_info = NULL;

	(void)v;
	seq_puts(p, "\t\t Rx\t\t Tx\t    Tx Fail\t\t  Mbox\t      index\t      client\n");
	list_for_each_entry(chan_info, &mbox_proc_list, proc_list) {
		seq_printf(p, "%s:\t %10d\t %10d\t %10d\t %10s\t %10d\t %10s\n",
				chan_info->mbox_id,
				chan_info->rx_count,
				chan_info->tx_count,
				chan_info->tx_fail_count,
				chan_info->mdev->mbox.dev->kobj.name,
				chan_info->channel,
				chan_info->client_name);
	}

	return 0;
}

static void mbox_pump_rx_messages(struct kthread_work *work)
{
	struct mbox_receiveQueue *mbox_queue;
	struct mbox_receive_list *mbox_list;
	struct mbox_receive_list *mbox_list_tmp;
	ulong flags;

	mbox_queue = container_of(work, struct mbox_receiveQueue,
		pump_messages);

	spin_lock_irqsave(&mbox_queue->rx_queue_lock, flags);

	list_for_each_entry_safe(mbox_list, mbox_list_tmp,
		&mbox_queue->rx_queue, queue) {

		if (mbox_queue->handler != NULL) {
			spin_unlock_irqrestore(
				&mbox_queue->rx_queue_lock,
				flags);

			//call IPC handler
			mbox_queue->handler(mbox_list->dev_id,
				mbox_list->ch, &mbox_list->msg);

			spin_lock_irqsave(&mbox_queue->rx_queue_lock,
				flags);
		}

		list_del(&mbox_list->queue);
		kfree(mbox_list);
	}
	spin_unlock_irqrestore(&mbox_queue->rx_queue_lock, flags);
}

static int32_t mbox_receive_queue_init(
			struct mbox_receiveQueue *mbox_queue,
			mbox_receive_queue_t handler,
			void *handler_pdata,
			const char *name)
{
	int32_t ret;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	if (mbox_queue != NULL) {
		INIT_LIST_HEAD(&mbox_queue->rx_queue);
		spin_lock_init(&mbox_queue->rx_queue_lock);

		mbox_queue->handler = handler;
		mbox_queue->handler_pdata = handler_pdata;

		kthread_init_worker(&mbox_queue->kworker);
		mbox_queue->kworker_task = kthread_run(kthread_worker_fn,
				&mbox_queue->kworker,
				name);

		if (IS_ERR(mbox_queue->kworker_task)) {
			(void)pr_err(
				"[ERROR][%s]%s:failed to create message pump task\n",
				LOG_TAG, __func__);
			ret = -ENOMEM;
		} else {
			(void)sched_setscheduler(mbox_queue->kworker_task, SCHED_FIFO, &param);
			kthread_init_work(&mbox_queue->pump_messages,
				mbox_pump_rx_messages);
			ret = 0;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int32_t deregister_receive_queue(struct mbox_receiveQueue *mbox_queue)
{
	int32_t ret;

	if (mbox_queue == NULL) {
		ret = -EINVAL;
	} else {
		kthread_flush_worker(&mbox_queue->kworker);
		(void)kthread_stop(mbox_queue->kworker_task);
		ret = 0;
	}

	return ret;
}

static int32_t mbox_add_rx_queue_and_work(
			struct mbox_receiveQueue *mbox_queue,
			struct mbox_receive_list *mbox_list)
{
	ulong flags;

	spin_lock_irqsave(&mbox_queue->rx_queue_lock, flags);

	if (mbox_list != NULL) {
		list_add_tail(&mbox_list->queue, &mbox_queue->rx_queue);
	}

	spin_unlock_irqrestore(&mbox_queue->rx_queue_lock, flags);

	(void)kthread_queue_work(&mbox_queue->kworker,
		&mbox_queue->pump_messages);

	return 0;
}

static void mbox_pump_tx_messages(struct kthread_work *work)
{
	struct mbox_transmitQueue *mbox_queue;
	struct mbox_transmit_list *tx_list;
	struct mbox_transmit_list *tx_list_tmp;
	ulong flags;

	mbox_queue = container_of(work, struct mbox_transmitQueue,
			pump_messages);

	spin_lock_irqsave(&mbox_queue->tx_queue_lock, flags);

	list_for_each_entry_safe(tx_list, tx_list_tmp,
		&mbox_queue->tx_queue, queue) {
		if (mbox_queue->handler != NULL) {

			spin_unlock_irqrestore(
				&mbox_queue->tx_queue_lock,
				flags);

			mbox_queue->handler(
				tx_list->chan, &tx_list->msg);

			spin_lock_irqsave(&mbox_queue->tx_queue_lock,
				flags);
		}

		list_del(&tx_list->queue);
		kfree(tx_list);
	}
	spin_unlock_irqrestore(&mbox_queue->tx_queue_lock, flags);

}

static int32_t mbox_transmit_queue_init(struct mbox_transmitQueue *mbox_queue,
			mbox_transmit_queue_t handler,
			void *handler_pdata, const char *name)
{
	int32_t ret;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	if (mbox_queue != NULL) {
		INIT_LIST_HEAD(&mbox_queue->tx_queue);
		spin_lock_init(&mbox_queue->tx_queue_lock);

		mbox_queue->handler = handler;
		mbox_queue->handler_pdata = handler_pdata;

		kthread_init_worker(&mbox_queue->kworker);
		mbox_queue->kworker_task = kthread_run(kthread_worker_fn,
				&mbox_queue->kworker,
				name);

		if (IS_ERR(mbox_queue->kworker_task)) {
			(void)pr_err(
				"[ERROR][%s]%s:failed to create rx message pump task\n",
				LOG_TAG, __func__);
			ret = -ENOMEM;
		} else {
			(void)sched_setscheduler(mbox_queue->kworker_task, SCHED_FIFO, &param);
			kthread_init_work(
				&mbox_queue->pump_messages,
				mbox_pump_tx_messages);
			ret = 0;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int32_t deregister_transmit_queue(struct mbox_transmitQueue *mbox_queue)
{
	int32_t ret;

	if (mbox_queue == NULL) {
		ret = -EINVAL;
	} else {
		kthread_flush_worker(&mbox_queue->kworker);
		(void)kthread_stop(mbox_queue->kworker_task);
		ret = 0;
	}
	return ret;
}

static int32_t mbox_add_tx_queue_and_work(
		struct mbox_transmitQueue *mbox_queue,
		struct mbox_transmit_list *mbox_list)
{
	ulong flags;

	spin_lock_irqsave(&mbox_queue->tx_queue_lock, flags);

	if (mbox_list != NULL) {
		list_add_tail(&mbox_list->queue, &mbox_queue->tx_queue);
	}

	spin_unlock_irqrestore(&mbox_queue->tx_queue_lock, flags);

	(void)kthread_queue_work(&mbox_queue->kworker,
		&mbox_queue->pump_messages);

	return 0;
}

static void tcc_mbox_event_create(struct tcc_mbox_device *mdev)
{
	if (mdev != NULL) {
		init_waitqueue_head(&mdev->waitQueue.cmdQueue);
		mdev->waitQueue.condition = 0;
	}
}

static void tcc_mbox_event_delete(struct tcc_mbox_device *mdev)
{
	if (mdev != NULL) {
		mdev->waitQueue.condition = 0;
	}
}

static int32_t tcc_mbox_wait_event_timeout(struct tcc_mbox_device *mdev,
		uint32_t timeOut)
{
	int32_t ret = -EINVAL;

	if (mdev != NULL) {
		long wait_ret;

		wait_ret = wait_event_interruptible_timeout(
			mdev->waitQueue.cmdQueue,
			mdev->waitQueue.condition == 0,
			msecs_to_jiffies(timeOut));

		if ((mdev->waitQueue.condition == 1)
			|| (wait_ret <= 0)) {
			/* timeout */
			ret = -ETIME;
		} else {
			ret = 0;
		}

		/* clear flag */
		mdev->waitQueue.condition = 0;
	}
	return ret;
}

static void tcc_mbox_wake_up(struct tcc_mbox_device *mdev)
{
	if (mdev != NULL) {
		mdev->waitQueue.condition = 0;
		wake_up_interruptible(&mdev->waitQueue.cmdQueue);
	}
}

static void mbox_write_fifo(struct tcc_mbox_device *mdev,
	const struct tcc_channel *chan_info, const struct tcc_mbox_data *msg)
{
	if ((mdev != NULL) && (msg != NULL)) {

		uint32_t idx;

		/* check data fifo */
		if ((readl_relaxed(mdev->base + MBOX_DT_STR) &
			(uint32_t)DATA_MEMP_MASK)
			== (uint32_t)0) {

			/* flush buffer */
			writel_relaxed((uint32_t)DF_FLUSH_BIT,
				mdev->base + MBOXCTR_SET);
		}

		/* disabsle & clear tx done irq */
		writel_relaxed(~((uint32_t)TXD_IEN_BIT |
			(uint32_t)TXD_ICLR_BIT),
			mdev->base + MBOXCTR_CLR);

		/* disable data output. */
		writel_relaxed(~(uint32_t)OEN_BIT,
			mdev->base + MBOXCTR_CLR);

		/* write data fifo */
		if (msg->data_len > (uint32_t)0) {
			for (idx = 0; idx < msg->data_len; idx++) {
				writel_relaxed(msg->data[idx],
					mdev->base + MBOXDTXD);
			}
		}

		/* write command fifo */
		writel_relaxed(chan_info->header0.cmd,
			mdev->base + MBOXTXD(0));
		writel_relaxed(msg->cmd[0],
			mdev->base + MBOXTXD(1));
		writel_relaxed(msg->cmd[1],
			mdev->base + MBOXTXD(2));
		writel_relaxed(msg->cmd[2],
			mdev->base + MBOXTXD(3));
		writel_relaxed(msg->cmd[3],
			mdev->base + MBOXTXD(4));
		writel_relaxed(msg->cmd[4],
			mdev->base + MBOXTXD(5));
		writel_relaxed(msg->cmd[5],
			mdev->base + MBOXTXD(6));
		writel_relaxed(chan_info->header1.cmd,
			mdev->base + MBOXTXD(0));

		/* Required only on ES chip and HSM */
		if (chip_rev == CHIP_REVISION_ES) {
			if ((strncmp(chan_info->mbox_id, "HSM", (size_t)3) == (int32_t)0)) {
				sss_busy_check();
			}
		}

		/* enable tx done irq. */
		writel_relaxed((uint32_t)TXD_IEN_BIT,
			mdev->base + MBOXCTR_SET);

		/* enable data output. */
		mdev->waitQueue.condition = 1;
		writel_relaxed((uint32_t)OEN_BIT,
			mdev->base + MBOXCTR_SET);

		/* Required only on ES chip and HSM */
		if (chip_rev == CHIP_REVISION_ES) {
			if ((strncmp(chan_info->mbox_id, "HSM", (size_t)3) == (int32_t)0)) {
				sss_set_func_id();
			}
		}
	}
}


static int32_t tcc_mbox_send_message(const struct mbox_chan *chan,
		const struct tcc_mbox_data *msg)
{
	int32_t ret = 0;

	if ((chan != NULL) && (msg != NULL)) {
		struct tcc_mbox_device *mdev = dev_get_drvdata(chan->mbox->dev);
		struct tcc_channel *chan_info = chan->con_priv;
		int32_t i;

		if (msg->data_len > (uint32_t)MBOX_DATA_FIFO_SIZE) {
			eprintk(mdev->mbox.dev,
				"mbox data fifo is too big. fifo size(%d), input size(%d)\n",
				MBOX_DATA_FIFO_SIZE, msg->data_len);
			eprintk(mdev->mbox.dev, "message of %s driver is error.\n",
				chan_info->mbox_id);

			for (i = 0; i < (MBOX_CMD_FIFO_SIZE); i++) {
				d1printk((mdev), mdev->mbox.dev,
					"cmd[%d]:(0x%08x)\n", i+1, msg->cmd[i]);
			}
			eprintk(mdev->mbox.dev,
				"cmd[7]: (0x%08x)\n", chan_info->header1.cmd);
			ret = -EINVAL;

		} else {
			if ((readl_relaxed(mdev->base + (uint32_t)TRM_STS) &
				(uint32_t)OPP_TMN_STS_BIT) == (uint32_t)0) {
				d1printk((mdev), mdev->mbox.dev,
					"Opposite MBOX is not ready\n");
				ret = -ECOMM;
			} else if ((readl_relaxed(mdev->base + MBOXSTR) &
				(uint32_t)MEMP_MASK) == (uint32_t)0) {
				d1printk((mdev), mdev->mbox.dev,
					"MBOX is not empty\n");
				ret = -EBUSY;
			} else {
				mbox_write_fifo(mdev, chan_info, msg);
				ret = tcc_mbox_wait_event_timeout(
					mdev, MBOX_TX_TIMEOUT);
			}
		}
	} else {
		ret = EINVAL;
	}

	return ret;
}


static void tcc_multich_mbox_send_worker(struct mbox_chan *chan,
			const struct tcc_mbox_data *msg)
{
	int32_t ret;

	if ((chan != NULL) && (msg != NULL)) {
		struct tcc_mbox_device *mdev = dev_get_drvdata(chan->mbox->dev);
		struct tcc_channel *chan_info = chan->con_priv;

		if ((mdev != NULL) && (chan_info != NULL))	{
			d2printk((mdev), mdev->mbox.dev, "In, %s ch(%d)\n",
				chan_info->mbox_id, chan_info->channel);

			mutex_lock(&mdev->sendLock);

			chan_info->tx_count++;
			ret = tcc_mbox_send_message(chan, msg);

			if (ret != 0) {
				chan_info->tx_fail_count++;
			}

			d2printk((mdev), mdev->mbox.dev,
				"tx result : %d\n", ret);

			tcc_txdone(chan, ret);

			mutex_unlock(&mdev->sendLock);
		} else {
			(void)pr_err("[ERROR][%s]%s: mdev or chan_info is NULL",
				(const char *)LOG_TAG, __func__);
		}

	} else {
		(void)pr_err("[ERROR][%s]%s: Invaild arguments",
			(const char *)LOG_TAG, __func__);
	}
}


static int32_t tcc_multich_mbox_submit(struct mbox_chan *chan, void *mbox_msg)
{
	if ((chan != NULL) && (mbox_msg != NULL)) {
		struct tcc_mbox_device *mdev =
			dev_get_drvdata(chan->mbox->dev);
		struct mbox_transmit_list *tx_list =
			kzalloc(sizeof(struct mbox_transmit_list),
			GFP_ATOMIC);

		if (tx_list != NULL) {
			INIT_LIST_HEAD(&tx_list->queue);
			tx_list->chan = chan;

			(void)memcpy((void *)&tx_list->msg,
				(const void *)mbox_msg,
				sizeof(struct tcc_mbox_data));

			(void)mbox_add_tx_queue_and_work(
				&mdev->transmitQueue, tx_list);
		} else {
			eprintk(mdev->mbox.dev, "Allocate fail\n");
		}
	} else {
		(void)pr_err("[ERROR][%s]%s: Parameter Error",
			(const char *)LOG_TAG, __func__);
	}

	return 0;
}

static int32_t read_mboxid_from_client(const struct tcc_mbox_device *mdev,
			const struct mbox_client *cl,
			char *mbox_id)
{
	int32_t ret;
	const char *mbox_id_ptr = NULL;
	__kernel_size_t mbox_id_len;

	if (of_property_read_string(cl->dev->of_node, "mbox-id",
			&mbox_id_ptr) == 0) {

		d1printk((mdev), mdev->mbox.dev,
			"mbox id is %s\n", mbox_id_ptr);

		mbox_id_len = strnlen(mbox_id_ptr,
			(__kernel_size_t)(MBOX_ID_MAX_LEN) +
			(__kernel_size_t)1);

		if (mbox_id_len > (__kernel_size_t)(MBOX_ID_MAX_LEN)) {
			eprintk(mdev->mbox.dev,
				"mbox id(%s) is too long. Max len is %d\n",
				mbox_id_ptr, MBOX_ID_MAX_LEN);
			ret = -EINVAL;

		} else if (mbox_id_len < (__kernel_size_t)3) {
			eprintk(mdev->mbox.dev,
				"mbox id(%s) is too short. Minimal len is 3\n",
				mbox_id_ptr);
			ret = -EINVAL;
		} else if (cl->tx_tout < CLIENT_MBOX_TX_TIMEOUT) {
			eprintk(mdev->mbox.dev,
			"The client timeout is greater than the mbox timeout.\n");
			eprintk(mdev->mbox.dev,
			"Please set it to less than mbox time(%lums).\n",
			CLIENT_MBOX_TX_TIMEOUT);
			ret = -EINVAL;
		} else {
			(void)memset(mbox_id, 0x00, MBOX_ID_MAX_LEN);
			(void)memcpy(mbox_id,
				mbox_id_ptr,
				(__kernel_size_t)mbox_id_len);
			ret = 0;
		}
	} else {
		eprintk(mdev->mbox.dev,
			"Not find 'mbox-id' in device tree.\n");
		ret = -EINVAL;
	}

	return ret;
}

static int32_t add_client_to_chan_info(const struct mbox_chan *chan,
		const char *mbox_id)
{
	int32_t ret = -1;

	if (chan != NULL) {
		const struct tcc_mbox_device *mdev = NULL;
		struct tcc_channel *chan_info = NULL;
		const struct mbox_client *cl = NULL;
		const struct mbox_controller *mbox = NULL;

		mdev = dev_get_drvdata(chan->mbox->dev);
		chan_info = chan->con_priv;
		cl = chan->cl;
		mbox = chan->mbox;

		(void)memcpy(chan_info->mbox_id,
			mbox_id,
			MBOX_ID_MAX_LEN);

		atomic_set(&chan_info->rx_remain_count, 0);
		chan_info->rx_count = 0;
		chan_info->tx_count = 0;
		chan_info->tx_fail_count = 0;
		chan_info->client_name = cl->dev->kobj.name;
		list_add_tail(&chan_info->proc_list,
			&mbox_proc_list);

		chan_info->header0.id.idName[0] = mbox_id[4];
		chan_info->header0.id.idName[1] = mbox_id[5];
#ifdef CONFIG_TCC805X_CA53Q
		chan_info->header0.id.cid[0] = CID_A53;
#else
		chan_info->header0.id.cid[0] = CID_A72;
#endif
		chan_info->header0.id.bsid[0] = BSID;

		chan_info->header1.id.idName[0] = mbox_id[0];
		chan_info->header1.id.idName[1] = mbox_id[1];
		chan_info->header1.id.idName[2] = mbox_id[2];
		chan_info->header1.id.idName[3] = mbox_id[3];

		chan_info->msg = devm_kzalloc(chan->mbox->dev,
				sizeof(struct tcc_mbox_data),
				GFP_KERNEL);

		if (chan_info->msg == NULL) {
			ret = -ENOMEM;
		} else {
			ret = mbox_receive_queue_init(
				&chan_info->receiveQueue,
				&tcc_received_msg,
				NULL,
				cl->dev->kobj.name);

			if (ret == 0) {
				chan_info->ch_enable = TCC_MBOX_CH_ENALBE;
			}
		}
	}
	return ret;

}

static int32_t tcc_multich_mbox_startup(struct mbox_chan *chan)
{
	int32_t ret = 0;
	int32_t i;
	struct tcc_mbox_device *mdev = NULL;
	const struct tcc_channel *chan_info = NULL;
	const struct mbox_client *cl = NULL;
	const struct mbox_controller *mbox = NULL;
	char mbox_id[MBOX_ID_MAX_LEN];

	if (chan != NULL) {
		mdev = dev_get_drvdata(chan->mbox->dev);
		chan_info = chan->con_priv;
		cl = chan->cl;
		mbox = chan->mbox;
	}

	if ((mdev != NULL) && (chan_info != NULL) &&
		(cl != NULL)) {

		ret = read_mboxid_from_client(mdev, cl, mbox_id);

		if (ret == 0) {
			mutex_lock(&mdev->lock);

			/* Check for duplicate use of the client. */
			for (i = 0; i < mbox->num_chans; i++) {
				if (mbox->chans[i].con_priv != NULL) {
					const struct tcc_channel *chan_info_temp =
						mbox->chans[i].con_priv;

					if (strncmp(
						(const char *)
						chan_info_temp->mbox_id,
						(const char *)mbox_id,
						(size_t)MBOX_ID_MAX_LEN) == 0) {

						eprintk(mdev->mbox.dev,
							"This mbox id(%s) is already in use.\n",
							mbox_id);
						ret = -EBUSY;
						break;
					}
				}
			}

			if (ret == 0) {
				 ret = add_client_to_chan_info(chan, mbox_id);
			}
			mutex_unlock(&mdev->lock);
		}
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static void wait_mbox_rx_message_empty(struct mbox_chan *chan)
{
	if (chan != NULL) {
		const struct tcc_mbox_device *mdev = dev_get_drvdata(chan->mbox->dev);
		const struct tcc_channel *chan_info =
			(const struct tcc_channel *)chan->con_priv;

		if ((mdev != NULL) && (chan_info != NULL)) {
			bool irq_state;
			unsigned long flags;
			int ret;

			if (mdev->rx_irq >= 0) {
				spin_lock_irqsave(&chan->lock, flags);
				ret = irq_get_irqchip_state((unsigned int)mdev->rx_irq,
						IRQCHIP_STATE_ACTIVE, &irq_state);

				while ((atomic_read(&chan_info->rx_remain_count) > 0)
						|| (irq_state == true)) {
					spin_unlock_irqrestore(&chan->lock, flags);

					mbox_mdelay(5);

					spin_lock_irqsave(&chan->lock, flags);
					ret = irq_get_irqchip_state((unsigned int)mdev->rx_irq,
						IRQCHIP_STATE_ACTIVE, &irq_state);

				}
				spin_unlock_irqrestore(&chan->lock, flags);
			}
		}
	}

}

static void tcc_multich_mbox_shutdown(struct mbox_chan *chan)
{
	if (chan != NULL) {
		struct tcc_mbox_device *mdev = dev_get_drvdata(chan->mbox->dev);
		struct tcc_channel *chan_info = chan->con_priv;

		if ((mdev != NULL) && (chan_info != NULL)) {

			d1printk((mdev), mdev->mbox.dev, "In\n");

			chan_info->ch_enable = TCC_MBOX_CH_DISALBE;

			wait_mbox_rx_message_empty(chan);

			mutex_lock(&mdev->lock);

			list_del(&chan_info->proc_list);

			(void)deregister_receive_queue(
				&chan_info->receiveQueue);

			mutex_unlock(&mdev->lock);

			d1printk((mdev), mdev->mbox.dev, "out\n");

			chan->con_priv = NULL;
		}
	}
}

static bool tcc_multich_mbox_last_tx_done(struct mbox_chan *chan)
{
	bool ret = (bool)false;

	if (chan != NULL) {
		const struct tcc_mbox_device *mdev = dev_get_drvdata(chan->mbox->dev);

		d1printk((mdev), mdev->mbox.dev, "In\n");

		/* check transmmit cmd fifo */
		if ((readl_relaxed(mdev->base + MBOXSTR) & (uint32_t)MEMP_MASK)
			== (uint32_t)MEMP_MASK) {
			ret = (bool)true;
		}
	}
	return ret;
}

static const struct mbox_chan_ops tcc_multich_mbox_chan_ops = {
	.send_data = tcc_multich_mbox_submit,
	.startup = tcc_multich_mbox_startup,
	.shutdown = tcc_multich_mbox_shutdown,
	.last_tx_done = tcc_multich_mbox_last_tx_done,
};

static void tcc_txdone(struct mbox_chan *chan, int32_t error)
{
	if (chan != NULL) {
		mbox_chan_txdone(chan, error);
		if ((chan->cl->tx_block == (bool)true)
			&& (error == -ETIME)) {
			complete(&chan->tx_complete);
		}
	}
}
/*
static void tcc_received_msg(const void *dev_id, uint32_t ch,
			struct tcc_mbox_data *msg)
{
	if ((dev_id != NULL) && (msg != NULL)) {
		const struct tcc_mbox_device *mdev =
			(const struct tcc_mbox_device *)dev_id;
		const struct mbox_controller *mbox;
		struct mbox_chan *chan;
		struct tcc_channel *chan_info;

		d2printk((mdev), mdev->mbox.dev, "In, ch(%d)\n", ch);

		mbox = &mdev->mbox;
		chan = &mbox->chans[ch];

		if (chan->cl != NULL) {
			mbox_chan_received_data(chan, msg);

			chan_info =
				(struct tcc_channel *)chan->con_priv;

			if (chan_info != NULL) {
				atomic_dec(&chan_info->rx_remain_count);
			}

			d2printk((mdev),
				mdev->mbox.dev, "out, ch(%d)\n", ch);
		} else {
			eprintk(mdev->mbox.dev,
				"mbox_chan is NULL\n");
		}
	}
}
*/

static void tcc_received_msg(const void *dev_id, uint32_t ch,
         struct tcc_mbox_data *msg)
{

   // from here
   struct file *file;
   char *buffer;
   mm_segment_t old_fs;
   loff_t pos = 0;

   int whitchSensor;
   // to here

   if ((dev_id != NULL) && (msg != NULL)) {
      const struct tcc_mbox_device *mdev =
         (const struct tcc_mbox_device *)dev_id;
      const struct mbox_controller *mbox;
      struct mbox_chan *chan;
      struct tcc_channel *chan_info;

      d2printk((mdev), mdev->mbox.dev, "In, ch(%d)\n", ch);

      mbox = &mdev->mbox;
      chan = &mbox->chans[ch];

      if (chan->cl != NULL) {
         mbox_chan_received_data(chan, msg);

         chan_info =
            (struct tcc_channel *)chan->con_priv;

         if (chan_info != NULL) {
            atomic_dec(&chan_info->rx_remain_count);
         }

         // always listening messgage


         // 여기부터
         if (msg->cmd[1] > 1) {
            printk(KERN_CONT"Let's write File!!!\n");

         printk(KERN_CONT"cmd[0]: %d || ",(uint16_t)(msg->cmd[0]));
         //printk(KERN_CONT"cmd[1]: %d || ",(uint16_t)(msg->cmd[1]));
         //printk(KERN_CONT"cmd[2]: %d || ",(uint16_t)(msg->cmd[2]));
         //printk(KERN_CONT"data_len: %d || ",(uint16_t)(msg->data_len));
         printk(KERN_CONT"data[1]: %d || ",(int)(uint8_t)(msg->data[1]));
         printk(KERN_CONT"data[2]: %d || ",(int)(uint8_t)(msg->data[2]));
         //printk(KERN_CONT"data[3]: %d || ",(int)(uint8_t)(msg->data[3]));
         printk(KERN_CONT "\n");

                      // memory alloc
                      buffer = kmalloc(512, GFP_KERNEL);
                      if (!buffer) {
                          printk(KERN_ERR "Failed to allocate memory for buffer\n");
                          return;
                      }

            printk(KERN_CONT"File Open!!\n");
                      // file open

            whitchSensor = (int)(uint8_t)(msg->data[1]);

            if(whitchSensor == 1)
            {
                      file = filp_open("/tmp/dht11.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            else if(whitchSensor == 2)
            {
                      file = filp_open("/tmp/light.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
         else{
               file = filp_open("/tmp/mbox_data.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
         }
                      if (IS_ERR(file)) {
                          printk(KERN_ERR "Failed to open file: /tmp/mbox_data.log\n");
                          kfree(buffer);
                          return;
                      }

                      // 사용자 공간 파일 시스템 컨텍스트 설정
                      old_fs = get_fs();
                      set_fs(KERNEL_DS);

            printk(KERN_CONT"File write!!!\n");
                      // 버퍼에 메시지 포맷 작성
                      snprintf(buffer, 512, "%d", (int)(uint8_t)msg->data[2]);
                      kernel_write(file, buffer, strlen(buffer), &pos);

            printk(KERN_CONT"File Close!!!\n");
                      // 파일 닫기 및 자원 해제
                      set_fs(old_fs);
                      filp_close(file, NULL);
                      kfree(buffer);
                     }
         // 여기까지 추가

         //
         d2printk((mdev),
            mdev->mbox.dev, "out, ch(%d)\n", ch);
      } else {
         eprintk(mdev->mbox.dev,
            "mbox_chan is NULL\n");
      }
   }
}

static int32_t tcc_mbox_find_header(const struct mbox_chan *chan,
				const struct mbox_receive_list *mbox_list)
{
	int32_t ret = 0;

	if ((chan != NULL) && (mbox_list != NULL)) {
		const struct tcc_channel *chan_info =
			(struct tcc_channel *)chan->con_priv;

		if ((chan_info != NULL) &&
			(chan_info->ch_enable == TCC_MBOX_CH_ENALBE)) {
			if (chan_info->header1.cmd == mbox_list->header1.cmd) {
				if ((chan_info->header0.id.idName[0] ==
					mbox_list->header0.id.idName[0]) &&
					(chan_info->header0.id.idName[1] ==
					mbox_list->header0.id.idName[1])) {
					ret = 1;
				}
			}
		}
	}
	return ret;
}

static int32_t mbox_read_fifo(void *dev_id,
			const struct tcc_mbox_device *mdev,
			struct mbox_receive_list *mbox_list)
{
	int32_t is_valid_ch = 0;

	if ((dev_id != NULL) && (mdev != NULL) && (mbox_list != NULL)) {
		uint32_t max_channel;
		uint32_t i;
		int32_t idx;

		INIT_LIST_HEAD(&mbox_list->queue);

		mbox_list->dev_id = dev_id;
		mbox_list->msg.data_len =
			readl_relaxed(mdev->base + MBOX_RT_STR)
			& (uint32_t)DATA_SCOUNT_MASK;

		for (idx = 0; idx < (int32_t)mbox_list->msg.data_len; idx++) {
			mbox_list->msg.data[idx] =
				readl_relaxed(mdev->base + MBOXRTXD);
		}

		mbox_list->header0.cmd =
			readl_relaxed(mdev->base + MBOXRXD(0));
		mbox_list->msg.cmd[0] =
			readl_relaxed(mdev->base + MBOXRXD(1));
		mbox_list->msg.cmd[1] =
			readl_relaxed(mdev->base + MBOXRXD(2));
		mbox_list->msg.cmd[2] =
			readl_relaxed(mdev->base + MBOXRXD(3));
		mbox_list->msg.cmd[3] =
			readl_relaxed(mdev->base + MBOXRXD(4));
		mbox_list->msg.cmd[4] =
			readl_relaxed(mdev->base + MBOXRXD(5));
		mbox_list->msg.cmd[5] =
			readl_relaxed(mdev->base + MBOXRXD(6));
		mbox_list->header1.cmd =
			readl_relaxed(mdev->base + MBOXRXD(7));

		if (mdev->mbox.num_chans > 0) {
			max_channel = (uint32_t)mdev->mbox.num_chans;
		} else {
			max_channel = 0;
		}

		for (i = 0; i < max_channel; i++) {
			int32_t is_find;
			const struct mbox_chan *chan;
			struct tcc_channel *chan_info;

			chan = &mdev->mbox.chans[i];
			is_find = tcc_mbox_find_header(chan, mbox_list);

			if (is_find == 1) {
				chan_info =
					(struct tcc_channel *)chan->con_priv;
				atomic_inc(&chan_info->rx_remain_count);

				mbox_list->ch = i;
				(void)mbox_add_rx_queue_and_work(
					&chan_info->receiveQueue, mbox_list);

				if (chan_info->rx_count < 0xFFFFFFFFU) {
					chan_info->rx_count++;
				} else {
					chan_info->rx_count = 0;
				}

				is_valid_ch = 1;
				break;
			}
		}
	}

	return is_valid_ch;
}

static irqreturn_t tcc_multich_mbox_rx_irq(int irq, void *dev_id)
{
	const struct tcc_mbox_device *mdev = (struct tcc_mbox_device *)dev_id;
	struct mbox_receive_list *mbox_list = NULL;
	irqreturn_t ret;
	int32_t result;
	u32 status = 0;
	uint32_t i;

	if (mdev == NULL) {
		result = -1;
	} else {
		status = readl_relaxed(mdev->base + MBOXSTR);

		if ((irq != mdev->rx_irq) || ((status & (uint32_t)SEMP_MASK) == (uint32_t)SEMP_MASK)) {
			result = -1;
		} else {
			result = 0;
		}
	}

	if (result == 0) {
		if (((readl_relaxed(mdev->base + MBOXSTR) >> (uint32_t)20)
			& (uint32_t)0xF) == (uint32_t)0) {
			eprintk(mdev->mbox.dev, "Mailbox FIFO(SCOUNT) is empty\n");
			result = -1;
		} else {
			mbox_list = kzalloc(
				sizeof(struct mbox_receive_list), GFP_ATOMIC);
		}
	}

	if (mbox_list != NULL) {
		int32_t is_valid_ch = 0;

		is_valid_ch = mbox_read_fifo(dev_id, mdev, mbox_list);

		if (is_valid_ch == 0) {
			char mbox_id[MBOX_ID_MAX_LEN+(uint32_t)1];

			(void)memset(mbox_id, 0x00,
				((__kernel_size_t)(MBOX_ID_MAX_LEN) +
				(__kernel_size_t)1));

			for (i = 0; i < (uint32_t)MBOX_ID0_LEN; i++) {
				mbox_id[i] = mbox_list->header1.id.idName[i];
			}

			mbox_id[MBOX_ID0_LEN] =
				mbox_list->header0.id.idName[0];
			mbox_id[MBOX_ID0_LEN+(u8)1] =
				mbox_list->header0.id.idName[1];

			d1printk((mdev), mdev->mbox.dev,
				"Not registered : id(%s)\n",
				mbox_id);

			kfree(mbox_list);
		}
	} else {
		result = -1;
	}

	if (result == 0) {
		ret = (irqreturn_t)IRQ_HANDLED;
	} else {
		ret = (irqreturn_t)IRQ_NONE;
	}

	return ret;
}

static irqreturn_t tcc_multich_mbox_txd_irq(int32_t irq, void *dev_id)
{
	struct tcc_mbox_device *mdev = (struct tcc_mbox_device *)dev_id;
	irqreturn_t ret;

	if ((mdev == NULL) || (irq != mdev->txd_irq)) {
		ret = (irqreturn_t)IRQ_NONE;
	} else {
		tcc_mbox_wake_up(mdev);
		ret = (irqreturn_t)IRQ_HANDLED;
	}

	if (mdev != NULL) {
		writel_relaxed((uint32_t)TXD_ICLR_BIT,
			mdev->base + MBOXCTR_SET);
	}
	return ret;
}


static struct mbox_chan *tcc_multich_mbox_xlate(struct mbox_controller *mbox,
					const struct of_phandle_args *spec)
{
	struct mbox_chan *chan = NULL;

	if ((mbox != NULL) && (spec != NULL)) {
		struct tcc_mbox_device *mdev = dev_get_drvdata(mbox->dev);
		uint32_t channel   = spec->args[0];
		struct tcc_channel *chan_info;

		if (channel > 0x0FFFFFFFU) {
			channel = 0x0FFFFFFFU;
		}

		d1printk((mdev), mdev->mbox.dev,
			"In, channel (%d)\n", channel);

		if (mbox->num_chans <= (int32_t)channel) {
			eprintk(mbox->dev,
				"Invalid channel requested channel:%d\n",
				channel);
			chan = ERR_PTR(-EINVAL);
		} else {
			chan_info = mbox->chans[channel].con_priv;
			if ((chan_info != NULL) &&
				(channel == chan_info->channel)) {
				eprintk(mbox->dev, "Channel in use\n");
				chan = ERR_PTR(-EBUSY);
			} else {
				chan = &mbox->chans[channel];

				chan_info = devm_kzalloc(mbox->dev,
					sizeof(*chan_info), GFP_KERNEL);

				if (chan_info == NULL) {
					chan = ERR_PTR(-ENOMEM);
				} else {
					chan_info->mdev = mdev;
					chan_info->channel = (uint32_t)channel;

					chan->con_priv = chan_info;

					d1printk((mdev), mbox->dev,
						"Mbox: Created channel: channel: %d\n",
						channel);
				}
			}
		}
	}
	return chan;
}


static const struct of_device_id tcc_multich_mbox_of_match[] = {
	{
		.compatible = "telechips,multichannel-mailbox"},
	{},
};

MODULE_DEVICE_TABLE(of, tcc_multich_mbox_of_match);

static void change_mbox_irq_affinity(uint32_t irq_number)
{
#ifdef CONFIG_SMP
	/* Change the CPU number that handles interrupts.*/
	struct cpumask affinity_set;
	struct cpumask affinity_mask;

	/*delete cpu0*/
	cpumask_xor(&affinity_set,
		(const struct cpumask *)cpu_all_mask,
		get_cpu_mask(0));

	/*choose online cpus*/
	(void)cpumask_and(&affinity_mask,
		cpu_online_mask,
		(const struct cpumask *)&affinity_set);

	if (!cpumask_empty(&affinity_mask)) {
		(void)irq_set_affinity_hint(
			irq_number,
			(const struct cpumask *)&affinity_mask);
	}
#else
	(void)irq_number;
#endif
}

static int32_t init_tcc_mbox_device(struct platform_device *pdev,
			struct tcc_mbox_device *mdev)
{
	int32_t ret;

	if (mdev != NULL) {
		uint32_t max_channel;
		int32_t rx_irq;
		int32_t txd_irq;

		platform_set_drvdata(pdev, mdev);

		/* read dtb node */
		if (of_property_read_u32(
			pdev->dev.of_node, "max-channel", &max_channel) != 0) {
			eprintk(&pdev->dev,
				"Not find max-channel in dtb\n");
			ret = -EINVAL;
		} else {
			rx_irq = platform_get_irq(pdev, 0);
			txd_irq = platform_get_irq(pdev, 1);

			if ((rx_irq < 0) || (txd_irq < 0)) {
				eprintk(&pdev->dev, "Not fine irq in dtb\n");
				ret = -EINVAL;
			} else {
				ret = 0;
			}
		}

		if (ret == 0) {
			mdev->debug_level = mbox_verbose_mode;
			mdev->rx_irq = rx_irq;
			mdev->txd_irq = txd_irq;

			mdev->mbox.dev = &pdev->dev;

			if (max_channel < 0x0FFFFFFFU) {
				mdev->mbox.num_chans = (int)max_channel;
			} else {
				mdev->mbox.num_chans = 0;
			}

			mdev->mbox.ops = &tcc_multich_mbox_chan_ops;
			mdev->mbox.txdone_irq = (bool)true;
			mdev->mbox.txdone_poll = (bool)false;
			mdev->mbox.txpoll_period = 1; /* 1ms */
			mdev->mbox.of_xlate = &tcc_multich_mbox_xlate;
			mdev->base = of_iomap(pdev->dev.of_node, 0);

			if (IS_ERR(mdev->base)) {
				ret = -EINVAL;
			} else	{
				/* Allocated channel */
				mdev->mbox.chans =
				devm_kzalloc(&pdev->dev,
					sizeof(struct mbox_chan)*
					(size_t)mdev->mbox.num_chans,
					GFP_KERNEL);

				if (mdev->mbox.chans == NULL) {
					ret = -ENOMEM;
				}
			}
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int32_t tcc_mbox_register(struct platform_device *pdev,
			struct tcc_mbox_device *mdev)
{
	int32_t ret = -1;

	if ((pdev != NULL) && (mdev != NULL)) {
		mutex_init(&mdev->lock);
		mutex_init(&mdev->sendLock);

		tcc_mbox_event_create(mdev);

		ret = mbox_transmit_queue_init(&mdev->transmitQueue,
			&tcc_multich_mbox_send_worker,
			NULL,
			pdev->dev.kobj.name);

		if (ret == 0) {
			ret = devm_mbox_controller_register(&pdev->dev, &mdev->mbox);
			if (ret < 0) {
				eprintk(&pdev->dev,
					"Failed to register mailbox: %d\n",
					ret);
			} else {
				writel_relaxed(
					((uint32_t)CF_FLUSH_BIT |
					(uint32_t)DF_FLUSH_BIT |
					(uint32_t)IEN_BIT |
					(uint32_t)LEVEL0_BIT |
					(uint32_t)LEVEL1_BIT),
					mdev->base + MBOXCTR_SET);
				writel_relaxed(
					readl_relaxed(mdev->base + TRM_STS) |
					(uint32_t)OWN_TMN_STS_BIT,
					mdev->base + TRM_STS);
			}
		} else {
			eprintk(&pdev->dev, "mbox queue init fail\n");
		}
	}
	return ret;
}

static int32_t set_mbox_irq(struct platform_device *pdev,
			struct tcc_mbox_device *mdev)
{
	int32_t ret = -1;

	if ((pdev != NULL) && (mdev != NULL) &&
		(mdev->rx_irq >= 0) && (mdev->txd_irq >= 0)) {
		/* mbox rx irq init */
		ret = devm_request_irq(&pdev->dev, (uint32_t)mdev->rx_irq,
			&tcc_multich_mbox_rx_irq, IRQF_ONESHOT,
			dev_name(&pdev->dev), mdev);

		if (ret == 0) {
			change_mbox_irq_affinity((uint32_t)mdev->rx_irq);
		} else {
			eprintk(&pdev->dev, "rx irq fail.(%d)\n", ret);
		}

		/* mbox tx done irq init */
		if (ret == 0) {
			ret = devm_request_irq(&pdev->dev, (uint32_t)mdev->txd_irq,
				tcc_multich_mbox_txd_irq, IRQF_ONESHOT,
				dev_name(&pdev->dev), mdev);

			if (ret == 0) {
				change_mbox_irq_affinity((uint32_t)mdev->txd_irq);
			} else {
				eprintk(&pdev->dev, "tx done irq fail.(%d)\n", ret);
			}
		}
	}
	return ret;
}

static int32_t tcc_multich_mbox_probe(struct platform_device *pdev)
{
	struct tcc_mbox_device *mdev = NULL;
	int32_t ret = 0;

	if (pdev != NULL) {
		if (pdev->dev.of_node == NULL) {
			ret = -ENODEV;
		}
	} else {
		ret = -EINVAL;
	}

	/* Get the chip revision, ES:0 CS:1 */
	chip_rev = (uint32_t)get_chip_rev();

	/* set tcc_mbox_device */
	if (ret == 0) {
		mdev = devm_kzalloc(&pdev->dev,
			sizeof(struct tcc_mbox_device), GFP_KERNEL);

		if (mdev == NULL) {
			ret = -ENOMEM;
		} else {
			ret = init_tcc_mbox_device(pdev, mdev);

			/* clear STS BIT */
			writel_relaxed(readl_relaxed(mdev->base + TRM_STS) &
					~(uint32_t)OWN_TMN_STS_BIT, mdev->base + TRM_STS);
			/* Flush bit */
			writel_relaxed((uint32_t)CF_FLUSH_BIT | (uint32_t)DF_FLUSH_BIT,
					mdev->base + MBOXCTR);
			/* Clear CTR Register */
			writel_relaxed((uint32_t)0x0U , mdev->base + MBOXCTR_CLR);
		}
	}

	/* mbox rx & txd irq init */
	if (ret == 0) {
		ret = set_mbox_irq(pdev, mdev);
	}

	/* Create the mbox_debug_level sysfs */
	if (ret == 0) {
		ret = device_create_file(&pdev->dev, &dev_attr_debug_level);
		if (ret < 0) {
			eprintk(&pdev->dev,	"failed create sysfs\n");
		} else {
			ret = 0;
		}
	}

	if (ret == 0) {
		ret = tcc_mbox_register(pdev, mdev);
	}
	return ret;
}

static int32_t tcc_multich_mbox_remove(struct platform_device *pdev)
{
	struct tcc_mbox_device *mdev = platform_get_drvdata(pdev);

	writel_relaxed(readl_relaxed(mdev->base + TRM_STS) &
		~(uint32_t)OWN_TMN_STS_BIT, mdev->base + TRM_STS);
	writel_relaxed((uint32_t)CF_FLUSH_BIT | (uint32_t)DF_FLUSH_BIT,
		mdev->base + MBOXCTR);
	writel_relaxed((uint32_t)0x0U , mdev->base + MBOXCTR_CLR);

	devm_mbox_controller_unregister(&pdev->dev, &mdev->mbox);

	(void)deregister_transmit_queue(&mdev->transmitQueue);

	tcc_mbox_event_delete(mdev);

	device_remove_file(&pdev->dev, &dev_attr_debug_level);

	return 0;
}

#if defined(CONFIG_PM)
static int32_t tcc_multich_mbox_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	const struct tcc_mbox_device *mdev = platform_get_drvdata(pdev);

	(void)state;
	writel_relaxed(readl_relaxed(mdev->base + TRM_STS) &
		~(uint32_t)OWN_TMN_STS_BIT, mdev->base + TRM_STS);

	/* Flush RX buffer */
	writel_relaxed(((uint32_t)CF_FLUSH_BIT | (uint32_t)DF_FLUSH_BIT),
		mdev->base + MBOXCTR);

	/*Disable rx interrupt */
	writel_relaxed(~(uint32_t)IEN_BIT, mdev->base + MBOXCTR_CLR);

	/* disabsle & clear tx done irq */
	writel_relaxed(~((uint32_t)TXD_IEN_BIT |(uint32_t)TXD_ICLR_BIT),
			mdev->base + MBOXCTR_CLR);

	return 0;
}

static int32_t tcc_multich_mbox_resume(struct platform_device *pdev)
{
	const struct tcc_mbox_device *mdev = platform_get_drvdata(pdev);

	if ((mdev->rx_irq >= 0) && (mdev->txd_irq >= 0)) {
		/* for rx interrupt */
		change_mbox_irq_affinity((uint32_t)mdev->rx_irq);
		/* for tx done interrupt */
		change_mbox_irq_affinity((uint32_t)mdev->txd_irq);
	}

	/*Enable interrupt*/
	writel_relaxed((uint32_t)IEN_BIT |
		(uint32_t)LEVEL0_BIT | (uint32_t)LEVEL1_BIT,
		mdev->base + MBOXCTR_SET);

	writel_relaxed(readl_relaxed(mdev->base + TRM_STS)
		| (uint32_t)OWN_TMN_STS_BIT, mdev->base + TRM_STS);

	return 0;
}
#endif

static struct platform_driver tcc_multich_mbox_driver = {
	.probe = tcc_multich_mbox_probe,
	.remove = tcc_multich_mbox_remove,
	.driver = {
			.name = "tcc-multich-mailbox",
			.of_match_table =
			of_match_ptr(tcc_multich_mbox_of_match),
		},
#if defined(CONFIG_PM)
	.suspend = tcc_multich_mbox_suspend,
	.resume = tcc_multich_mbox_resume,
#endif
};

static int32_t __init tcc_multich_mbox_init(void)
{
	return platform_driver_register(&tcc_multich_mbox_driver);
}
subsys_initcall(tcc_multich_mbox_init);

static void __exit tcc_multich_mbox_exit(void)
{
	platform_driver_unregister(&tcc_multich_mbox_driver);
}
module_exit(tcc_multich_mbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Telechips Multi Channel Mailbox Driver");
MODULE_AUTHOR("jun@telechips.com");
