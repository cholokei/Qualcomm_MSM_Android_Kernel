#ifndef _MSM_KGSL_H
#define _MSM_KGSL_H

#include <uapi/linux/msm_kgsl.h>

#ifdef __KERNEL__
#ifdef CONFIG_MSM_KGSL_DRM
int kgsl_gem_obj_addr(int drm_fd, int handle, unsigned long *start,
			unsigned long *len);
#else
#define kgsl_gem_obj_addr(...) 0
#endif
#endif

#endif /* _MSM_KGSL_H */
