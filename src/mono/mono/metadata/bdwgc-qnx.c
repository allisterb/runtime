/**
 * \file
 * qflat: QNX hook required by bdwgc's QNX port (gcconfig.h's STACKBOTTOM), which it declares but does not define.
 *
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */
#include <sys/storage.h>

/* The main thread's stack bottom (highest address): QNX keeps each thread's stack base and size in its TLS. */
void *
qnx_get_stack_bottom (void)
{
	struct _thread_local_storage *tls = __tls ();

	return (char *)tls->__stackaddr + tls->__stacksize;
}
