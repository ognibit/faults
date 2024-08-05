#pragma once
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Faults Module - An errors collector.
 *
 * Author: Omar Rampado <omar@ognibit.it>
 * Version: v0.1.x
 *
 * The current version is NOT thread safe.
 *
 * Compilation Flags:
 *
 * FAULT_ID_MAX max number of configurable fault_code.
 *                Default: 128
 * FAULT_MODULE_MAX max number of configurable fault_module.
 *                Default: 16
 * FAULT_LOG_MAX a positive value for the logs queue dimension.
 *                Default: 1
 */

/* COMPILATION FLAGS */
#ifndef FAULT_MODULE_MAX
#define FAULT_MODULE_MAX  16
#endif

#ifndef FAULT_ID_MAX
#define FAULT_ID_MAX     128
#endif

#ifndef FAULT_LOG_MAX
#define FAULT_LOG_MAX     1
#endif

/* DO NOT CHANGE THE FOLLOWING VALUES */
#define FAULT_MODULE_KO      INT_MAX
#define FAULT_NO_FAILURE     0
#define FAULT_GENERIC_MODULE 0

/* Generic fault codes for handling unregistered events.
 * Their are part of FAULT_GENERIC_MODULE
 */
enum FaultGenericCode {
    FAULT_GENERIC_UNKNOWN,
    FAULT_GENERIC_ALL
};

typedef unsigned int fault_id;
typedef unsigned int fault_module;
typedef unsigned int fault_code;
typedef unsigned long fault_counter;  /* >= 0 */
typedef unsigned long fault_millisecs;

enum FaultPolicyType {
    /* never trigger */
    FAULT_POL_NONE,

    /* Trigger when the number of faults
     * are greater than or equals to the threshold
     */
    FAULT_POL_COUNT_ABS,

    /* As FAULT_POL_COUNT_ABS but it resets the counter
     * when a sequence of N events is complete without faults
     */
    FAULT_POL_COUNT_RESET,

    /* Trigger when the fault persists for more than the milliseconds
     * configured. It resets the counter when a sequence of not fault
     * events is stable for more than N milliseconds.
     */
    FAULT_POL_TIME_RESET,
    FAULT_POL_ALL  /* placeholder */
};

typedef enum FaultPolicyType fault_policy_type;

enum FaultStatusType {
    FAULT_ST_NORMAL,  /* no fault */
    FAULT_ST_WARNING, /* first threshold */
    FAULT_ST_ERROR,   /* second threshold */
    FAULT_ST_ALL /* placeholder */
};

typedef enum FaultStatusType fault_status_type;

enum FaultStatusModuleType {
    FAULT_SM_NORMAL,   /* no fault */
    FAULT_SM_WARNING,  /* some codes in warning */
    FAULT_SM_FAULTED,  /* some codes in error */
    FAULT_SM_FAILED,   /* errors over the tolerance */
    FAULT_SM_ALL /* placeholder */
};

typedef enum FaultStatusModuleType fault_status_module_type;

struct FaultLog {
    bool saved;   /* the data represent a real log entry */
    size_t index; /* position in the log history */
    fault_millisecs timestamp;
    fault_module module;
    fault_code code;
    fault_status_type status;
    long refValue; /* a user defined reference value for the event */
};

typedef struct FaultLog FaultLog;

/* External function that must be provided by the user.
 * It must return a monotonicaly increasing value representing
 * the time in milliseconds.
 * The user can refer to other units, but the output must always
 * increase (or be the same) with respect of the previous call.
 * It is used for marking the events timestamp.
 * The overflow of the counter must be managed by the user,
 * but it will be better if does not happen.
 */
extern
fault_millisecs fault_now(void);

/* It must be called at the very beginning of the program */
void fault_init(void);

/* Register a new module that could contains at maximum 'ncodes'
 * different errors/faults.
 * Tolerance is the number of faults at the same time that leads
 * to a failure. Set to FAULT_NO_FAILURE to avoid the control.
 */
fault_module fault_conf_module(fault_counter ncodes, fault_counter tolerance);

/* Convert the pair (module, code) into a fault identifier.
 * 'mod' must be the identifier created with fault_conf_module.
 * 'code' must be in the range set on configuration.
 * In case of error, a generic fault identifier will be returned.
 */
fault_id fault_getid(fault_module mod, fault_code code);

/* Inspect the status of a specific condition.
 * On wrong 'id' it returns FAULT_ST_ERROR
 */
fault_status_type fault_status(fault_id id);

/* Verify the condition of the whole module.
 * On wrong 'mod' it returns FAULT_SM_FAILED
 */
fault_status_module_type fault_status_module(fault_module mod);

/* Configure the fault policy to FAULT_POL_NONE
 * return false in case of error
 */
bool fault_policy_none(fault_id id);

/* Configure the fault policy to FAULT_POL_COUNT_ABS.
 * It never reset the counters, must be done manually.
 *
 * id: from fault_getid()
 * warn: threshold for the FAULT_ST_WARNING, must be positive >0
 * err: threshold for the FAULT_ST_ERROR
 * return false in case of error
 *
 * To get only warnings, set err = 0.
 * To get only errors, set err = warn.
 *
 * The status chagens when the errors counter is greater
 * or equals to the threshold.
 */
bool fault_policy_count_abs(fault_id id, fault_counter warn, fault_counter err);

/* Configure the fault policy to FAULT_POL_COUNT_RESET.
 * It resets the counters after a series of good events.
 *
 * id: from fault_getid()
 * warn: threshold for the FAULT_ST_WARNING, must be positive >0
 * err: threshold for the FAULT_ST_ERROR
 * reset: threshold for the reset (series of not error events).
 *        Must be positive >0.
 * return false in case of error
 *
 * To get only warnings, set err = 0.
 * To get only errors, set err = warn.
 *
 * The status chagens when the errors counter is greater
 * or equals to the threshold.
 */
bool fault_policy_count_reset(fault_id id,
                              fault_counter warn,
                              fault_counter err,
                              fault_counter reset);

/* Configure the fault policy to FAULT_POL_TIME_RESET.
 * It resets the counters after a certain amount of time is passed
 * from the last error (having only good events).
 *
 * id: from fault_getid()
 * warn: threshold for the FAULT_ST_WARNING, must be positive >0
 * err: threshold for the FAULT_ST_ERROR
 * reset: threshold for the reset (series of not error events).
 *        Must be positive >0.
 * return false in case of error
 *
 * To get only warnings, set err = 0.
 * To get only errors, set err = warn.
 *
 * The status chagens when the errors counter is greater
 * or equals to the threshold.
 */
bool fault_policy_time_reset(fault_id id,
                             fault_millisecs warn,
                             fault_millisecs err,
                             fault_millisecs reset);

/* Update the internal database of faults.
 * id: the fault reference from fault_getid()
 * ref: a reference value for future inspections
 * condition: true for a fault, false for a valid check.
 * return: condition
 *
 * If the id does not match, FAULT_GENERIC_UNKNOWN will be used.
 */
bool fault_update(fault_id id, long ref, bool condition);

/* Get the number of fault registered in the database.
 * The number depends on the policy, since it can reset it.
 * id: the fault reference from fault_getid()
 * return: the error counter
 *
 * If id does not mach, zero will be returned.
 */
fault_counter fault_count_errors(fault_id id);

/* Erase the couters in the database.
 * id: the fault reference from fault_getid()
 * return: false in case of wrong id
 */
bool fault_reset(fault_id id);

/* Get the reference value of the last error.
 * It is registered by fault_update() when condition is false.
 * id: the fault reference from fault_getid()
 * return: the same value the user send to fault_update()
 *
 * If id does not match, zero will be returned.
 */
long fault_refval(fault_id id);

/* Empty the logs queue */
void fault_logs_reset(void);

/* Get the number of logs stored in the logs queue.
 * return a value less or equals to FAULT_LOG_MAX
 */
size_t fault_logs_length(void);

/* Get the log in the queue (history) at position 'index'.
 * index: the relative position of the log to retrieve.
 *        0 is the most recent,
 *        fault_logs_length()-1 is the oldest.
 *
 * return: a copy of the log entry with the attribute 'saved' at true
 *         or a invalid structure with 'saved' at false when the index
 *         is out of range.
 */
FaultLog fault_log(size_t index);
