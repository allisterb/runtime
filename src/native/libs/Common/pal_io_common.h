// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include <stdlib.h>
#include <assert.h>
#include <poll.h>
#include <time.h>
#include <pal_error_common.h>
#include <pal_utilities.h>

/**
 * Our intermediate pollfd struct to normalize the data types
 */
typedef struct
{
    int32_t FileDescriptor;  // The file descriptor to poll
    int16_t Events;          // The events to poll for
    int16_t TriggeredEvents; // The events that triggered the poll
} PollEvent;

/**
 * Constants passed to and from poll describing what to poll for and what
 * kind of data was received from poll.
 */
typedef enum
{
    PAL_POLLIN = 0x0001,   /* non-urgent readable data available */
    PAL_POLLPRI = 0x0002,  /* urgent readable data available */
    PAL_POLLOUT = 0x0004,  /* data can be written without blocked */
    PAL_POLLERR = 0x0008,  /* an error occurred */
    PAL_POLLHUP = 0x0010,  /* the file descriptor hung up */
    PAL_POLLNVAL = 0x0020, /* the requested events were invalid */
} PollEvents;

inline static int32_t Common_Read(intptr_t fd, void* buffer, int32_t bufferSize)
{
    assert(buffer != NULL || bufferSize == 0);
    assert(bufferSize >= 0);

    if (bufferSize < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ssize_t count;
    while ((count = read(ToFileDescriptor(fd), buffer, (uint32_t)bufferSize)) < 0 && errno == EINTR);

    assert(count >= -1 && count <= bufferSize);
    return (int32_t)count;
}

inline static int32_t Common_Write(intptr_t fd, const void* buffer, int32_t bufferSize)
{
    assert(buffer != NULL || bufferSize == 0);
    assert(bufferSize >= 0);

    if (bufferSize < 0)
    {
        errno = ERANGE;
        return -1;
    }

    ssize_t count;
    while ((count = write(ToFileDescriptor(fd), buffer, (uint32_t)bufferSize)) < 0 && errno == EINTR);

    assert(count >= -1 && count <= bufferSize);
    return (int32_t)count;
}

inline static int16_t Common_ConvertPollEventsPalToPlatform(int16_t palEvents)
{
    // we need to do this for platforms like AIX where PAL_POLL* doesn't
    // match up to their reality; this is PollEvent -> system polling
    int16_t platformEvents = 0;

    if ((palEvents & PAL_POLLIN) != 0)
    {
        platformEvents |= POLLIN;
    }
#ifdef POLLPRI // not available in WASI
    if ((palEvents & PAL_POLLPRI) != 0)
    {
        platformEvents |= POLLPRI;
    }
#endif
    if ((palEvents & PAL_POLLOUT) != 0)
    {
        platformEvents |= POLLOUT;
    }
    if ((palEvents & PAL_POLLERR) != 0)
    {
        platformEvents |= POLLERR;
    }
    if ((palEvents & PAL_POLLHUP) != 0)
    {
        platformEvents |= POLLHUP;
    }
    if ((palEvents & PAL_POLLNVAL) != 0)
    {
        platformEvents |= POLLNVAL;
    }

    return platformEvents;
}

inline static int16_t Common_ConvertPollEventsPlatformToPal(int16_t platformEvents)
{
    // same as the other function, just system -> PollEvent
    int16_t palEvents = 0;

    if ((platformEvents & POLLIN) != 0)
    {
        palEvents |= PAL_POLLIN;
    }
#ifdef POLLPRI // not available in WASI
    if ((platformEvents & POLLPRI) != 0)
    {
        palEvents |= PAL_POLLPRI;
    }
#endif
    if ((platformEvents & POLLOUT) != 0)
    {
        palEvents |= PAL_POLLOUT;
    }
    if ((platformEvents & POLLERR) != 0)
    {
        palEvents |= PAL_POLLERR;
    }
    if ((platformEvents & POLLHUP) != 0)
    {
        palEvents |= PAL_POLLHUP;
    }
    if ((platformEvents & POLLNVAL) != 0)
    {
        palEvents |= PAL_POLLNVAL;
    }

    return palEvents;
}

inline static int32_t Common_Poll(PollEvent* pollEvents, uint32_t eventCount, int32_t milliseconds, uint32_t* triggered)
{
    if (pollEvents == NULL || triggered == NULL)
    {
        return Error_EFAULT;
    }

    if (milliseconds < -1)
    {
        return Error_EINVAL;
    }

    struct pollfd stackBuffer[(uint32_t)(2048/sizeof(struct pollfd))];
    int useStackBuffer = eventCount <= ARRAY_SIZE(stackBuffer);
    struct pollfd* pollfds = NULL;
    if (useStackBuffer)
    {
        pollfds = &stackBuffer[0];
    }
    else
    {
        pollfds = (struct pollfd*)calloc(eventCount, sizeof(*pollfds));
        if (pollfds == NULL)
        {
            return Error_ENOMEM;
        }
    }

    for (uint32_t i = 0; i < eventCount; i++)
    {
        const PollEvent* event = &pollEvents[i];
        pollfds[i].fd = event->FileDescriptor;
        pollfds[i].events = Common_ConvertPollEventsPalToPlatform(event->Events);
        pollfds[i].revents = 0;
    }

    // After EINTR, or a timeout reported before the deadline, poll again only for the time left: restarting the full
    // timeout would stretch the wait by up to one timeout per signal. Elapsed time is rounded down, so the wait never
    // ends early. On QNX, which has no SA_RESTART, every GC stop signal interrupts a poll, and an interrupted poll can
    // also come back as a timeout before its time (qflat).
    int rv;
    int32_t remaining = milliseconds;
    struct timespec start;
    bool timed = milliseconds > 0 && clock_gettime(CLOCK_MONOTONIC, &start) == 0;
    while (true)
    {
        rv = poll(pollfds, (nfds_t)eventCount, remaining);
        if (rv > 0)
        {
            // Only a requested event, or an error/hang-up/invalid fd (always reported), ends the wait. A return with
            // none of them is spurious, like a wakeup by a signal on QNX, and is treated as an early timeout.
            bool any = false;
            for (uint32_t i = 0; i < eventCount && !any; i++)
            {
                any = (pollfds[i].revents & (pollfds[i].events | POLLERR | POLLHUP | POLLNVAL)) != 0;
            }
            if (any)
            {
                break;
            }
            for (uint32_t i = 0; i < eventCount; i++)
            {
                pollfds[i].revents = 0;
            }
            rv = 0;
        }
        if (rv == 0 && milliseconds < 0)
        {
            continue; // an infinite wait never times out
        }
        if (!(rv < 0 && errno == EINTR) && !(rv == 0 && timed))
        {
            break;
        }
        struct timespec now;
        if (!timed || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        {
            if (rv == 0)
            {
                break;
            }
            continue;
        }
        // Rounded down from the whole difference: dividing the nanosecond part on its own would round a negative one
        // toward zero, which counts up to 1 ms too much.
        int64_t elapsed = ((int64_t)(now.tv_sec - start.tv_sec) * 1000000000 + (now.tv_nsec - start.tv_nsec)) / 1000000;
        if (elapsed >= milliseconds)
        {
            if (rv == 0)
            {
                break; // a genuine timeout
            }
            remaining = 0; // interrupted at the deadline: one last non-blocking poll
            continue;
        }
        remaining = milliseconds - (int32_t)elapsed;
    }

    if (rv < 0)
    {
        if (!useStackBuffer)
        {
            free(pollfds);
        }

        *triggered = 0;
        return ConvertErrorPlatformToPal(errno);
    }

    for (uint32_t i = 0; i < eventCount; i++)
    {
        const struct pollfd* pfd = &pollfds[i];
        assert(pfd->fd == pollEvents[i].FileDescriptor);
        assert(pfd->events == Common_ConvertPollEventsPalToPlatform(pollEvents[i].Events));
        pollEvents[i].TriggeredEvents = Common_ConvertPollEventsPlatformToPal(pfd->revents);
    }

    *triggered = (uint32_t)rv;

    if (!useStackBuffer)
    {
        free(pollfds);
    }

    return Error_SUCCESS;
}
