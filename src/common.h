/*
 * 2018 Tarpeeksi Hyvae Soft /
 * VCS
 *
 */

#ifndef COMMON_H
#define COMMON_H

#include "display.h"
#include "types.h"
#include "log.h"

#ifdef NDEBUG
    #error "NDEBUG disables assertions. Assertions are required by design."
#endif
#include <cassert>
#include <stdexcept>

const char PROGRAM_NAME[] = "\"VCS\" by Tarpeeksi Hyvae Soft";
const char PROGRAM_VERSION_STRING[] = "1.2.6";

const uint MAX_INPUT_CHANNELS = 2;  // For now, we only support up to two input channels.
const uint MAX_FRAME_SKIP = 4;

// The minimum and maximum resolutions we can output to.
// Note that these also set the limit for the input (from the capture card) resolutions,
// as they're not allowed to be larger/smaller than the output limits.
const uint MIN_OUTPUT_WIDTH = 200;
const uint MIN_OUTPUT_HEIGHT = 100;
const uint MAX_OUTPUT_WIDTH = 4096;
const uint MAX_OUTPUT_HEIGHT = 4096;
const uint MAX_OUTPUT_BPP = 32;
const u32 MAX_FRAME_SIZE = MAX_OUTPUT_WIDTH * MAX_OUTPUT_HEIGHT * u64(MAX_OUTPUT_BPP / 8);    // The maximum number of bytes a single frame will take.

const u32 FILTER_DATA_LENGTH = 16;          // How many bytes are allocated for each filter's parameter data.

#define NUM_ELEMENTS(array) int((sizeof(array) / sizeof((array)[0])))

#define k_assert(condition, error_string)   if (!(condition))\
                                            {\
                                                /*kd_show_headless_assert_error_message(error_string);*/\
                                                /*assert(condition && error_string);\*/\
                                                NBENE(("Exception in %s {%d}: %s", __FILE__, __LINE__, error_string));\
                                                throw std::runtime_error(error_string);\
                                            }

// For assertions in performance-critical sections; not guaranteed to evaluate
// to an assertion in release-oriented buids.
#ifdef ENFORCE_OPTIONAL_ASSERTS
    #define k_assert_optional k_assert
#else
    #define k_assert_optional(...)
#endif

#define DEBUG_(args)        (printf("[Debug] {%s:%i} ", __FILE__, __LINE__), printf args, printf("\n"), fflush(stdout)) /// Temp hack.
#define DEBUG(args)         (klog_log_debug args)
#define NBENE(args)         (klog_log_error args)
#define INFO(args)          (klog_log_info args)

extern unsigned int FRAME_SKIP;

extern i32 PROGRAM_EXIT_REQUESTED;

extern unsigned int INPUT_CHANNEL_IDX;

#endif

