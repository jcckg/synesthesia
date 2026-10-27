include(CheckCXXSourceCompiles)

function(check_synesthesia_compiler_features)
    find_package(Threads REQUIRED)
    set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    check_cxx_source_compiles([[
        #include <condition_variable>
        #include <mutex>
        #include <stop_token>
        #include <thread>

        int main() {
            std::stop_source source;
            std::jthread worker([](std::stop_token token) { return token.stop_requested(); });
            std::condition_variable_any condition;
            std::mutex mutex;
            std::unique_lock lock(mutex);
            condition.wait(lock, source.get_token(), [] { return true; });
            source.request_stop();
            return 0;
        }
    ]] SYNESTHESIA_HAS_CXX20_CANCELLATION)
    if(NOT SYNESTHESIA_HAS_CXX20_CANCELLATION)
        message(FATAL_ERROR
            "Synesthesia requires C++20 std::stop_source, std::stop_token, std::jthread and stop-aware condition variables. "
            "Use a compiler and standard library that support these features; on macOS, select Xcode 26 or later.")
    endif()
endfunction()

check_synesthesia_compiler_features()
