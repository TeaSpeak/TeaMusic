#pragma once

#include <string>
#include <cstddef>

namespace libevent {
    typedef void(event_callback_fn)(int, short, void *);

    struct function_handle {
        void* dl_handle;

        int(*evthread_use_pthreads)();

        void*(*event_base_new)();
        void(*event_base_free)(void *eb);

        int(*event_base_loop)(void *eb, int flags);
        int(*event_base_got_exit)(void *eb);
        int(*event_base_loopexit)(void *eb, const struct timeval *tv);

        int(*event_add)(void* ev, const struct timeval *tv);
        int(*event_del)(void* ev);
        int(*event_del_noblock)(void* ev);
        int(*event_del_block)(void* ev);

        int(*event_free)(void* ev);
        void *(*event_new)(void *base, int fd, short events, event_callback_fn callback, void *callback_arg);
    };
    extern function_handle* functions;

    bool resolve_functions(std::string& error);
    void release_functions();
}

#define EV_TIMEOUT	0x01
#define EV_READ		0x02
#define EV_WRITE	0x04
#define EV_PERSIST	0x10