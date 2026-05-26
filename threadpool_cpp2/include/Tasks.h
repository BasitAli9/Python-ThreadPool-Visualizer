#pragma once
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <functional>
#include <mutex>
#include <ctime>

// Defined in main.cpp
extern std::function<void(const std::string&)> g_log;
extern std::mutex g_log_mtx;

inline void emit(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if (g_log) g_log(s);
}

inline std::string now_str() {
    auto t  = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream o; o << std::put_time(&tm, "%H:%M:%S"); return o.str();
}

inline std::string tid() {
    std::ostringstream o; o << std::this_thread::get_id();
    std::string s = o.str();
    return "T-" + s.substr(s.size() > 4 ? s.size()-4 : 0);
}

// ── Tasks ────────────────────────────────────────────────────────────────
inline void task_print(const std::string& text, double dur = 0.1) {
    emit("["+now_str()+"]["+tid()+"] PRINT start: '"+text+"'");
    if (dur > 0) std::this_thread::sleep_for(std::chrono::milliseconds((int)(dur*1000)));
    emit("["+now_str()+"]["+tid()+"] PRINT done:  '"+text+"'");
}

inline void task_sleep(double sec) {
    emit("["+now_str()+"]["+tid()+"] SLEEP start: "+std::to_string(sec)+"s");
    std::this_thread::sleep_for(std::chrono::milliseconds((int)(sec*1000)));
    emit("["+now_str()+"]["+tid()+"] SLEEP done:  "+std::to_string(sec)+"s");
}

inline long long fib(int n) {
    if (n < 2) return n;
    long long a=0, b=1;
    for (int i=1; i<n; ++i) { long long c=a+b; a=b; b=c; }
    return b;
}

inline void task_cpu(int n) {
    emit("["+now_str()+"]["+tid()+"] CPU start: fib("+std::to_string(n)+")");
    auto t0 = std::chrono::high_resolution_clock::now();
    long long r = fib(n);
    double d = std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-t0).count();
    std::ostringstream o;
    o << "[" << now_str() << "][" << tid() << "] CPU done: fib(" << n
      << ")=" << r << " t=" << std::fixed << std::setprecision(3) << d << "s";
    emit(o.str());
}

inline void task_io_sim(const std::string& text, int writes=3, double delay=0.4) {
    emit("["+now_str()+"]["+tid()+"] IO_SIM start: '"+text+"'");
    for (int i=0; i<writes; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(delay*1000)));
        emit("["+now_str()+"]["+tid()+"] IO_SIM write "+
             std::to_string(i+1)+"/"+std::to_string(writes)+" '"+text+"'");
    }
    emit("["+now_str()+"]["+tid()+"] IO_SIM done: '"+text+"'");
}
