#include <Arduino.h>
#include <utility>
#include <TeensyThreads.h>

// cf. https://forum.pjrc.com/threads/41504-Teensy-3-x-multithreading-library-first-release/page6

// println implementation - can be placed in a header file
namespace arduino_preprocessor_is_buggy {
    // helper for parameter pack expansion, 
    // due to GCC bug 51253 we use an array
    using expand = int[];
    // used for suppressing compiler warnings
    template<class T> void silence(T&&) {};

    inline Threads::Mutex& getSerialLock() {
        static Threads::Mutex serial_lock;
        return serial_lock;
    }

  bool serialavailable();
  char serialgetch();

    template<class T> void print_fwd(const T& arg) {
        Serial.print(arg);
    }

    template<class T1, class T2> void print_fwd(const std::pair<T1, T2>& arg) {
        Serial.print(arg.first, arg.second);
    }

    template<class... args_t> void print(args_t... params) {
        Threads::Scope locker(getSerialLock());
        silence(expand{ (print_fwd(params), 42)... });
    }
  
    template<class... args_t> void println(args_t... params) {
        Threads::Scope locker(getSerialLock());
        silence(expand{ (print_fwd(params), 42)... });
        Serial.println();
	Serial.flush();
    }
}

using arduino_preprocessor_is_buggy::print;
using arduino_preprocessor_is_buggy::println;
using arduino_preprocessor_is_buggy::serialavailable;
using arduino_preprocessor_is_buggy::serialgetch;
using std::make_pair;
// end println implementation
