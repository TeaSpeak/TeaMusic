#include <string>
#include <vector>
#include <string_view>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace cw {
    struct Result {
        int exit_code{-1};

        std::string full_output_buffer{};
        std::vector<std::string_view> full_output{};

        std::string full_stdout_buffer{};
        std::vector<std::string_view> full_stdout{};

        std::string full_stderr_buffer{};
        std::vector<std::string_view> full_stderr{};
    };

    typedef std::function<void(const Result&)> callback_finish_t;
    typedef std::function<void(const std::string&)> callback_error_t;

    struct ExecutionHandle {
        std::string command{};

        callback_finish_t callback_finish{};
        callback_error_t callback_error{};
    };

    extern bool initialize(const std::string& /* prefix */, std::string& /* error */);
    extern void finalize();

    /* finish/error callback will be called within the event loop */
    extern std::shared_ptr<ExecutionHandle> execute(
            const std::string& /* command */,
            const callback_finish_t& /* finish callback */,
            const callback_error_t& /* error callback */
    );
}