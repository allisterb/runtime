/**
 * \file
 * QNX Neutrino thread support: stack bounds and the OS thread id.
 */

#include <config.h>

#if defined(__QNXNTO__)

#include <mono/utils/mono-threads.h>
#include <pthread.h>
#include <sys/storage.h>

/* QNX has no pthread_getattr_np. The thread local storage block (at the top of each thread's stack) records the
 * stack's base address and size, for the main thread as well. */
void
mono_threads_platform_get_stack_bounds (guint8 **staddr, size_t *stsize)
{
	struct _thread_local_storage *tls = __tls ();

	*staddr = (guint8 *)tls->__stackaddr;
	*stsize = tls->__stacksize;
}

/* pthread_t is the kernel thread id within the process. */
guint64
mono_native_thread_os_id_get (void)
{
	return (guint64)pthread_self ();
}

#else

#include <mono/utils/mono-compiler.h>

MONO_EMPTY_SOURCE_FILE (mono_threads_qnx);

#endif
