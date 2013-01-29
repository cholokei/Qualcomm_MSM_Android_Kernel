#ifndef _UAPI_LINUX_CI_BRIDGE_SPI_H
#define _UAPI_LINUX_CI_BRIDGE_SPI_H

#include <linux/ioctl.h>

#define CI_BRIDGE_IOCTL_MAGIC 'c'
#define CI_BRIDGE_IOCTL_RESET         _IOW(CI_BRIDGE_IOCTL_MAGIC, 0, unsigned)
#define CI_BRIDGE_IOCTL_GET_INT_STATE _IOR(CI_BRIDGE_IOCTL_MAGIC, 1, unsigned)


#endif /* _UAPI_LINUX_CI_BRIDGE_SPI_H */
