/*
 * debugfs interface for XRadio drivers
 *
 * Exposes device status, a manual firmware-reboot trigger, and a firmware
 * memory "peek" built on WSM command 0x0000 (a read primitive the vendor
 * driver never used, recovered by reverse-engineering fw_xr819.bin; see
 * docs/firmware.md). Everything here is for debugging the black-box
 * firmware and is compiled out when CONFIG_DEBUG_FS is disabled.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include "xradio.h"
#include "bh.h"
#include "wsm.h"
#include "debugfs.h"

static int xradio_status_show(struct seq_file *s, void *data)
{
	struct xradio_common *hw_priv = s->private;

	seq_printf(s, "fw label:        %s\n", hw_priv->wsm_caps.fw_label);
	seq_printf(s, "fw api version:  %u\n", hw_priv->wsm_caps.firmwareApiVer);
	seq_printf(s, "fw build:        %u\n",
		   hw_priv->wsm_caps.firmwareBuildNumber);
	seq_printf(s, "input buffers:   %u x %u bytes\n",
		   hw_priv->wsm_caps.numInpChBufs,
		   hw_priv->wsm_caps.sizeInpChBuf);
	seq_printf(s, "hw_bufs_used:    %d\n", hw_priv->hw_bufs_used);
	seq_printf(s, "wsm tx/rx seq:   %d / %d\n",
		   hw_priv->wsm_tx_seq, hw_priv->wsm_rx_seq);
	seq_printf(s, "bh_error:        %d\n", hw_priv->bh_error);
	seq_printf(s, "recovery:        %s, count=%u\n",
		   hw_priv->recovery_enabled ? "enabled" : "disabled",
		   hw_priv->recovery_count);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(xradio_status);

/*
 * Writing anything to "recover" forces a warm chip reboot, so the reset
 * path can be exercised without waiting for a real firmware crash.
 */
static ssize_t xradio_recover_write(struct file *file,
				    const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;

	dev_info(hw_priv->pdev, "manual firmware reboot requested via debugfs\n");
	hw_priv->bh_error = 1;
	xradio_schedule_recovery(hw_priv);
	return count;
}

static const struct file_operations xradio_recover_fops = {
	.open  = simple_open,
	.write = xradio_recover_write,
	.llseek = default_llseek,
};

/*
 * "fw_peek": write "<addr> [len]" to select a firmware memory region, then
 * read the file to dump it. len defaults to 64 and the firmware caps a
 * single read at 1024 bytes.
 */
static ssize_t xradio_fw_peek_write(struct file *file,
				    const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct xradio_common *hw_priv = s->private;
	char buf[32];
	u32 addr, len = 64;
	int n;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	n = sscanf(buf, "%i %u", &addr, &len);
	if (n < 1)
		return -EINVAL;
	if (len == 0 || len > 1024)
		return -EINVAL;

	hw_priv->debug_peek_addr = addr;
	hw_priv->debug_peek_len = len;
	return count;
}

static int xradio_fw_peek_show(struct seq_file *s, void *data)
{
	struct xradio_common *hw_priv = s->private;
	u32 addr = hw_priv->debug_peek_addr;
	u32 len = hw_priv->debug_peek_len ? hw_priv->debug_peek_len : 64;
	u8 *buf;
	int ret;

	if (len > 1024)
		len = 1024;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = wsm_fw_read(hw_priv, addr, buf, len);
	if (ret) {
		seq_printf(s, "peek 0x%08x (%u bytes) failed: %d\n",
			   addr, len, ret);
		kfree(buf);
		return 0;
	}

	seq_printf(s, "0x%08x (%u bytes):\n", addr, len);
	seq_hex_dump(s, "", DUMP_PREFIX_OFFSET, 16, 1, buf, len, false);
	kfree(buf);
	return 0;
}

static int xradio_fw_peek_open(struct inode *inode, struct file *file)
{
	return single_open(file, xradio_fw_peek_show, inode->i_private);
}

static const struct file_operations xradio_fw_peek_fops = {
	.open    = xradio_fw_peek_open,
	.read    = seq_read,
	.write   = xradio_fw_peek_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

void xradio_debugfs_init(struct xradio_common *hw_priv)
{
	struct dentry *d;

	d = debugfs_create_dir(wiphy_name(hw_priv->hw->wiphy), NULL);
	if (IS_ERR_OR_NULL(d))
		return;
	hw_priv->debug_dir = d;

	debugfs_create_file("status", 0400, d, hw_priv, &xradio_status_fops);
	debugfs_create_file("recover", 0200, d, hw_priv, &xradio_recover_fops);
	debugfs_create_bool("recovery_enabled", 0600, d,
			    &hw_priv->recovery_enabled);
	debugfs_create_u32("recovery_count", 0400, d, &hw_priv->recovery_count);
	debugfs_create_file("fw_peek", 0600, d, hw_priv, &xradio_fw_peek_fops);
}

void xradio_debugfs_deinit(struct xradio_common *hw_priv)
{
	debugfs_remove_recursive(hw_priv->debug_dir);
	hw_priv->debug_dir = NULL;
}
