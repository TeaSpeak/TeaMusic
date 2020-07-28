//
// Created by WolverinDEV on 28/07/2020.
//

#include <cassert>
#include <thread>
#include <deque>
#include <sstream>
#include <include/teaspeak/MusicPlayer.h>
#include <cstring>

#include "./CommandWrapper.h"
#include "./libevent.h"
#include "./pstream.h"

using namespace cw;

struct CommandExecutionImpl;
struct WrapperInstance {
    std::string prefix{};

    void* event_base{nullptr};
    std::thread event_base_thread{};

    std::mutex pending_commands_lock{};
    std::deque<std::shared_ptr<CommandExecutionImpl>> pending_commands{};

    std::deque<std::shared_ptr<CommandExecutionImpl>> errored_commands{};
    std::deque<std::shared_ptr<CommandExecutionImpl>> finished_commands{};

    void* event_dispatch_finished{nullptr};
};

struct ExecuteData {
    redi::pstream* pstream{nullptr};

    int fd_err{-1}, fd_out{-1};

    void* event_stderr_read{nullptr};
    void* event_stdout_read{nullptr};
    void* event_process_closed{nullptr};

    bool stderr_eof{false};
    bool stdout_eof{false};
};

struct CommandExecutionImpl : public ExecutionHandle, public std::enable_shared_from_this<CommandExecutionImpl> {
    void* execution_data{nullptr};

    Result result{};
    std::string error{};
};

static WrapperInstance* wrapper_instance{nullptr};
timeval kTimeoutZero{0, 0};
timeval kTimeoutProcessClosed{0, 1000};

void event_callback_closed(int, short, void*);
void event_callback_read(int, short, void*);
void dispatch_finished_callbacks(int, short, void*);

thread_local bool is_dispatcher_thread{false};
void event_base_dispatcher(WrapperInstance* instance) {
    is_dispatcher_thread = true;

    while(!libevent::functions->event_base_got_exit(instance->event_base))
        libevent::functions->event_base_loop(instance->event_base, 0x04); //EVLOOP_NO_EXIT_ON_EMPTY

    std::unique_lock pc_lock{instance->pending_commands_lock};
    auto commands = std::exchange(instance->pending_commands, {});
    pc_lock.unlock();

    for(const auto& command : commands)
        command->callback_error("shutdown");

    dispatch_finished_callbacks(0, 0, instance);
}

inline void parse_lines(std::vector<std::string_view>& result, const std::string_view& buffer, bool skip_empty) {
    result.reserve(64);

    size_t index = 0;
    do {
        auto nl_index = buffer.find('\n', index);
        auto line = buffer.substr(index, nl_index - index);
        index = nl_index + 1;

        if(skip_empty && line.find_first_not_of(" \n\r") == std::string::npos)
            continue;

        result.emplace_back(line);
    } while(index > 0);
}

void dispatch_finished_callbacks(int, short, void* ptr_instance) {
    auto instance = (WrapperInstance*) ptr_instance;

    std::unique_lock pc_lock{instance->pending_commands_lock};
    auto errored = std::exchange(instance->errored_commands, {});
    auto finished = std::exchange(instance->finished_commands, {});
    pc_lock.unlock();

    for(const auto& command : errored)
        command->callback_error(command->error);

    for(const auto& command : finished) {
        auto& response = command->result;
        parse_lines(response.full_output, response.full_output_buffer, true);
        parse_lines(response.full_stdout, response.full_stdout_buffer, true);
        parse_lines(response.full_stderr, response.full_stderr_buffer, true);

        std::ostringstream trace_log{};
        trace_log << wrapper_instance->prefix << " Command execution resulted in exit code " << std::hex << response.exit_code << (response.full_output.empty() ? "" : ":");
        for(const auto& line : response.full_output)
            trace_log << "\n"<< wrapper_instance->prefix << " " << line;
        music::log::log(response.exit_code == 0 ? music::log::trace : music::log::err, trace_log.str());

        command->callback_finish(command->result);
    }
}

bool cw::initialize(const std::string& prefix, std::string &error) {
    assert(!wrapper_instance);
    wrapper_instance = new WrapperInstance{};
    wrapper_instance->prefix = prefix;

    if(!libevent::functions) {
        error = "missing libevent functions";
        return false;
    }

    wrapper_instance->event_base = libevent::functions->event_base_new();
    if(!wrapper_instance->event_base) {
        finalize();
        error = "failed to allocate event base";
        return false;
    }

    wrapper_instance->event_dispatch_finished = libevent::functions->event_new(wrapper_instance->event_base, -1, 0, dispatch_finished_callbacks, wrapper_instance);
    if(!wrapper_instance->event_dispatch_finished) {
        finalize();
        error = "failed to allocate finish dispatch event";
        return false;
    }

    wrapper_instance->event_base_thread = std::thread{event_base_dispatcher, wrapper_instance};
    return true;
}

void cw::finalize() {
    if(!wrapper_instance)
        return;

    if(wrapper_instance->event_base) {
        libevent::functions->event_base_loopexit(wrapper_instance->event_base, nullptr);
        wrapper_instance->event_base_thread.join();

        libevent::functions->event_base_free(std::exchange(wrapper_instance->event_base, nullptr));

        assert(wrapper_instance->pending_commands.empty());
    }

    delete wrapper_instance;
    wrapper_instance = nullptr;
}

inline void shutdown_command_execution(const std::shared_ptr<CommandExecutionImpl>& command) {
    auto edata = (ExecuteData*) std::exchange(command->execution_data, nullptr);
    if(!edata)
        return;

    auto event_del = is_dispatcher_thread ? libevent::functions->event_del_noblock : libevent::functions->event_del_block;

    if(auto event{std::exchange(edata->event_stderr_read, nullptr)}; event)
        event_del(event);

    if(auto event{std::exchange(edata->event_stdout_read, nullptr)}; event)
        event_del(event);

    if(auto event{std::exchange(edata->event_process_closed, nullptr)}; event)
        event_del(event);

    edata->fd_err = -1;
    edata->fd_out = -1;
    if(auto pstream{std::exchange(edata->pstream, nullptr)}; pstream) {
        if(!pstream->rdbuf()->exited())
            pstream->rdbuf()->kill(SIGKILL);

        delete pstream;
    }

    delete edata;
}

inline bool initialize_command(const std::shared_ptr<CommandExecutionImpl>& command, std::string& error) {
    auto edata = new ExecuteData{};
    command->execution_data = edata;

    edata->pstream = new redi::pstream{};

    music::log::log(music::log::debug, wrapper_instance->prefix + " Executing video query command \"" + command->command + "\"");
    edata->pstream->open(command->command, redi::pstreams::pstderr | redi::pstreams::pstdout);

    edata->fd_err = edata->pstream->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_err);
    edata->fd_out = edata->pstream->rdbuf()->rpipe(redi::basic_pstreambuf<char>::buf_read_src::rsrc_out);

    if(fcntl(edata->fd_err, F_SETFL, fcntl(edata->fd_err, F_GETFL, 0) | O_NONBLOCK) == -1) {
        shutdown_command_execution(command);
        error = "failed to enable non blocking mode for stderr";
        return false;
    }

    if(fcntl(edata->fd_out, F_SETFL, fcntl(edata->fd_out, F_GETFL, 0) | O_NONBLOCK) == -1) {
        shutdown_command_execution(command);
        error = "failed to enable non blocking mode for stdout";
        return false;
    }

    edata->event_process_closed = libevent::functions->event_new(wrapper_instance->event_base, -1, 0, event_callback_closed, &*command);
    if(!edata->event_process_closed) {
        shutdown_command_execution(command);
        error = "failed to allocate closed event";
        return false;
    }

    edata->event_stdout_read = libevent::functions->event_new(wrapper_instance->event_base, edata->fd_out, EV_READ, event_callback_read, &*command);
    if(!edata->event_stdout_read) {
        shutdown_command_execution(command);
        error = "failed to allocate stdout read event";
        return false;
    }

    edata->event_stderr_read = libevent::functions->event_new(wrapper_instance->event_base, edata->fd_err, EV_READ, event_callback_read, &*command);
    if(!edata->event_stderr_read) {
        shutdown_command_execution(command);
        error = "failed to allocate stderr read event";
        return false;
    }

    libevent::functions->event_add(edata->event_stdout_read, nullptr);
    libevent::functions->event_add(edata->event_stderr_read, nullptr);
    return true;
}

std::shared_ptr<ExecutionHandle> cw::execute(const std::string &command, const callback_finish_t &finish_callback, const callback_error_t &error_callback) {
    assert(wrapper_instance);

    auto instance = std::make_shared<CommandExecutionImpl>();
    instance->command = command;
    instance->callback_finish = finish_callback;
    instance->callback_error = error_callback;

    if(!initialize_command(instance, instance->error)) {
        {
            std::lock_guard elock{wrapper_instance->pending_commands_lock};
            wrapper_instance->errored_commands.push_back(instance);
        }

        libevent::functions->event_add(wrapper_instance->event_dispatch_finished, nullptr);
        return instance;
    }

    std::lock_guard elock{wrapper_instance->pending_commands_lock};
    wrapper_instance->pending_commands.push_back(instance);
    return instance;
}

void dispatch_command_errored(const std::shared_ptr<CommandExecutionImpl>& command, const std::string& error) {
    command->error = error;

    {
        std::lock_guard elock{wrapper_instance->pending_commands_lock};
        auto pindex = std::find_if(wrapper_instance->pending_commands.begin(), wrapper_instance->pending_commands.end(), [&](const auto& cmd){
            return &*cmd == &*command;
        });
        if(pindex == wrapper_instance->pending_commands.end())
            return;

        wrapper_instance->pending_commands.erase(pindex);
        wrapper_instance->errored_commands.push_back(command);
    }

    shutdown_command_execution(command);
    libevent::functions->event_add(wrapper_instance->event_dispatch_finished, &kTimeoutZero);
}

void dispatch_command_finished(const std::shared_ptr<CommandExecutionImpl>& command) {
    {
        std::lock_guard elock{wrapper_instance->pending_commands_lock};
        auto pindex = std::find_if(wrapper_instance->pending_commands.begin(), wrapper_instance->pending_commands.end(), [&](const auto& cmd){
            return &*cmd == &*command;
        });
        if(pindex == wrapper_instance->pending_commands.end())
            return;

        wrapper_instance->pending_commands.erase(pindex);
        wrapper_instance->finished_commands.push_back(command);
    }

    shutdown_command_execution(command);
    libevent::functions->event_add(wrapper_instance->event_dispatch_finished, &kTimeoutZero);
}

constexpr static auto kReadBufferSize{1024};
void event_callback_read(int fd, short events, void* ptr_command) {
    auto command = ((CommandExecutionImpl*) ptr_command)->shared_from_this();
    auto edata = (ExecuteData*) command->execution_data;
    const auto is_err = fd == edata->fd_err;

    if((uint16_t) events & (uint16_t) EV_TIMEOUT) {
        dispatch_command_errored(command, "execution timeout");
        return;
    } else if((uint16_t) events & (uint16_t) EV_READ) {
        char buffer[kReadBufferSize];
        auto read = ::read(fd, buffer, kReadBufferSize);
        if(read < 0) {
            if(errno != EAGAIN) {
                dispatch_command_errored(command, "stderr/stdout read failed (" + std::to_string(errno) + "/" + strerror(errno) + ")");
                return;
            }
        } else if(read == 0) {
            if(is_err) {
                edata->stderr_eof = true;
            } else {
                edata->stdout_eof = true;
            }

            if(edata->stdout_eof && edata->stderr_eof)
                libevent::functions->event_add(edata->event_process_closed, &kTimeoutProcessClosed); /* await process close */
            return;
        } else {
            command->result.full_output_buffer.append(buffer, read);
            if(is_err)
                command->result.full_stderr_buffer.append(buffer, read);
            else
                command->result.full_stdout_buffer.append(buffer, read);
        }
    }
    libevent::functions->event_add(is_err ? edata->event_stderr_read : edata->event_stdout_read, nullptr);
}

void event_callback_closed(int, short, void* ptr_command) {
    auto command = ((CommandExecutionImpl*) ptr_command)->shared_from_this();
    auto edata = (ExecuteData*) command->execution_data;

    if(!edata->pstream->rdbuf()->exited()) {
        libevent::functions->event_add(edata->event_process_closed, &kTimeoutProcessClosed);
        return;
    }

    command->result.exit_code = edata->pstream->rdbuf()->status();
    dispatch_command_finished(command);
}