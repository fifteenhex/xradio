/*
 * DebugFS code for XRadio drivers
 *
 * Copyright (c) 2013, XRadio
 * Author: XRadio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef XRADIO_DEBUG_H_INCLUDED
#define XRADIO_DEBUG_H_INCLUDED

/* The logging is compiled out of this port.  A macro that expands to
 * nothing leaves "if (cond) wsm_printk(...);" with an empty body, which
 * the kernel build warns about, so expand to an empty statement instead.
 */
#define xradio_dbg(level, ...)	do { } while (0)
#define txrx_printk(level, ...)	do { } while (0)
#define wsm_printk(level, ...)	do { } while (0)
#define sta_printk(level, ...)	do { } while (0)
#define scan_printk(level, ...)	do { } while (0)
#define ap_printk(level, ...)	do { } while (0)
#define pm_printk(level, ...)	do { } while (0)

#endif /* XRADIO_DEBUG_H_INCLUDED */
