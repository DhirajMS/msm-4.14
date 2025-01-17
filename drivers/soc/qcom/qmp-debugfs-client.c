/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mailbox_client.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/mailbox/qmp.h>
#include <linux/uaccess.h>
#include <linux/mailbox_controller.h>
#include <linux/control_center/control_center.h>

#define MAX_MSG_SIZE 96 /* Imposed by the remote*/

struct qmp_debugfs_data {
	struct qmp_pkt pkt;
	char buf[MAX_MSG_SIZE + 1];
};

static struct qmp_debugfs_data data_pkt[MBOX_TX_QUEUE_LEN];
static struct mbox_chan *chan;
static struct mbox_client *cl;

static DEFINE_MUTEX(qmp_debugfs_mutex);

// tedlin@ASTI 2019/06/12 add for ddrfreq query (CONFIG_CONTROL_CENTER)
#define DDR_CONFIG_SIZE 12
#define DDR_BUFFER_SIZE 64
struct ddr_config {
	char buf[DDR_BUFFER_SIZE];
	size_t len;
} ddr_config[DDR_CONFIG_SIZE] = {
	{ "{class:ddr, res:fixed, val: 100}", 32 },
	{ "{class:ddr, res:fixed, val: 200}", 32 },
	{ "{class:ddr, res:fixed, val: 300}", 32 },
	{ "{class:ddr, res:fixed, val: 451}", 32 },
	{ "{class:ddr, res:fixed, val: 547}", 32 },
	{ "{class:ddr, res:fixed, val: 681}", 32 },
	{ "{class:ddr, res:fixed, val: 768}", 32 },
	{ "{class:ddr, res:fixed, val: 1017}", 33 },
	{ "{class:ddr, res:fixed, val: 1353}", 33 },
	{ "{class:ddr, res:fixed, val: 1555}", 33 },
	{ "{class:ddr, res:fixed, val: 1804}", 33 },
	{ "{class:ddr, res:fixed, val: 2092}", 33 }
};

// tedlin@ASTI 2019/06/12 add for ddrfreq query (CONFIG_CONTROL_CENTER)
void aop_lock_ddr_freq(int config)
{
	int target = 0;
	static struct qmp_debugfs_data data;

	mutex_lock(&qmp_debugfs_mutex);

	switch (config) {
	case 0:
	case 100:  target = 0; break;
	case 200:  target = 1; break;
	case 300:  target = 2; break;
	case 451:  target = 3; break;
	case 547:  target = 4; break;
	case 681:  target = 5; break;
	case 768:  target = 6; break;
	case 1017: target = 7; break;
	case 1353: target = 8; break;
	case 1555: target = 9; break;
	case 1804: target = 10; break;
	case 2092: target = 11; break;
	default:
		pr_warn("config not match: %d\n", config);
		mutex_unlock(&qmp_debugfs_mutex);
		return;
	}

	memset(&data, 0, sizeof(struct qmp_debugfs_data));
	memcpy(&data.buf, ddr_config[target].buf, ddr_config[target].len);
	data.buf[ddr_config[target].len] = '\0';
	data.pkt.size = (ddr_config[target].len + 0x3) & ~0x3;
	data.pkt.data = data.buf;

	if (mbox_send_message(chan, &(data.pkt)) < 0)
		pr_err("Failed to send qmp request\n");

	mutex_unlock(&qmp_debugfs_mutex);
}

static ssize_t aop_msg_write(struct file *file, const char __user *userstr,
		size_t len, loff_t *pos)
{
	static int count;
	int rc;

	if (!len || (len > MAX_MSG_SIZE))
		return len;

	mutex_lock(&qmp_debugfs_mutex);

	if (count >= MBOX_TX_QUEUE_LEN)
		count = 0;

	memset(&(data_pkt[count]), 0, sizeof(data_pkt[count]));
	rc  = copy_from_user(data_pkt[count].buf, userstr, len);
	if (rc) {
		pr_err("%s copy from user failed, rc=%d\n", __func__, rc);
		mutex_unlock(&qmp_debugfs_mutex);
		return len;
	}

	/*
	 * Controller expects a 4 byte aligned buffer
	 */
	data_pkt[count].pkt.size = (len + 0x3) & ~0x3;
	data_pkt[count].pkt.data = data_pkt[count].buf;

	if (mbox_send_message(chan, &(data_pkt[count].pkt)) < 0)
		pr_err("Failed to send qmp request\n");
	else
		count++;

	mutex_unlock(&qmp_debugfs_mutex);
	return len;
}

static const struct file_operations aop_msg_fops = {
	.write = aop_msg_write,
};

static int qmp_msg_probe(struct platform_device *pdev)
{
	struct dentry *file;

	cl = devm_kzalloc(&pdev->dev, sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return -ENOMEM;

	cl->dev = &pdev->dev;
	cl->tx_block = true;
	cl->tx_tout = 1000;
	cl->knows_txdone = false;

	chan = mbox_request_channel(cl, 0);
	if (IS_ERR(chan)) {
		dev_err(&pdev->dev, "Failed to mbox channel\n");
		return PTR_ERR(chan);
	}

	file = debugfs_create_file("aop_send_message", 0220, NULL, NULL,
			&aop_msg_fops);
	if (!file)
		goto err;
	return 0;
err:
	mbox_free_channel(chan);
	chan = NULL;
	return -ENOMEM;
}

static const struct of_device_id aop_qmp_match_tbl[] = {
	{.compatible = "qcom,debugfs-qmp-client"},
	{},
};

static struct platform_driver aop_qmp_msg_driver = {
	.probe = qmp_msg_probe,
	.driver = {
		.name = "debugfs-qmp-client",
		.owner = THIS_MODULE,
		.suppress_bind_attrs = true,
		.of_match_table = aop_qmp_match_tbl,
	},
};
builtin_platform_driver(aop_qmp_msg_driver);
