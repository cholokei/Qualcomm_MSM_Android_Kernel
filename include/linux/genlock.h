#ifndef _GENLOCK_H_
#define _GENLOCK_H_

#include <uapi/linux/genlock.h>

struct genlock;
struct genlock_handle;

struct genlock_handle *genlock_get_handle(void);
struct genlock_handle *genlock_get_handle_fd(int fd);
void genlock_put_handle(struct genlock_handle *handle);
struct genlock *genlock_create_lock(struct genlock_handle *);
struct genlock *genlock_attach_lock(struct genlock_handle *, int fd);
int genlock_wait(struct genlock_handle *handle, u32 timeout);
/* genlock_release_lock was deprecated */
int genlock_lock(struct genlock_handle *handle, int op, int flags,
	u32 timeout);

#endif
