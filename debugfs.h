/*
 * debugfs interface for XRadio drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef XRADIO_DEBUGFS_H
#define XRADIO_DEBUGFS_H

struct xradio_common;

#ifdef CONFIG_DEBUG_FS
void xradio_debugfs_init(struct xradio_common *hw_priv);
void xradio_debugfs_deinit(struct xradio_common *hw_priv);
#else
static inline void xradio_debugfs_init(struct xradio_common *hw_priv) {}
static inline void xradio_debugfs_deinit(struct xradio_common *hw_priv) {}
#endif

#endif /* XRADIO_DEBUGFS_H */
