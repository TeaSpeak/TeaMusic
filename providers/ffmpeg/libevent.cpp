#include "./libevent.h"
#include <dlfcn.h>
#include <cassert>

libevent::function_handle* libevent::functions{nullptr};

#define _str(x) #x

#define resolve_method(name) \
functions->name = (decltype(functions->name)) dlsym(functions->dl_handle, _str(name)); \
if(!functions->name) { error = std::string{"failed to resolve function " _str(name)}; goto error_cleanup; }

bool libevent::resolve_functions(std::string& error) {
    assert(!functions);

    functions = new libevent::function_handle{};
    functions->dl_handle = dlopen(nullptr, RTLD_NOW);
    if(!functions->dl_handle) {
        error = "failed to open main file handle";
        goto error_cleanup;
    }

    resolve_method(event_base_new)
    resolve_method(event_base_free)

    resolve_method(event_base_loop)
    resolve_method(event_base_loopexit)
    resolve_method(event_base_got_exit)

    resolve_method(event_free)
    resolve_method(event_new)

    resolve_method(event_add)
    resolve_method(event_del)
    resolve_method(event_del_block)
    resolve_method(event_del_noblock)
    return true;

    error_cleanup:
    libevent::release_functions();
    return false;
}

void libevent::release_functions() {
    if(functions) {
        if(functions->dl_handle)
            dlclose(functions->dl_handle);
        delete functions;
        functions = nullptr;
    }
}