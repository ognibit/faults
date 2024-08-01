/* Faults Module
 *
 * This version is NOT threadsafe.
 *
 * Author: Omar Rampado <omar@ognibit.it>
 * Version: 1.0.x
 */

#include "faults.h"
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#ifndef FAULT_MODULE_MAX
#define FAULT_MODULE_MAX  16
#endif

#ifndef FAULT_ID_MAX
#define FAULT_ID_MAX     128
#endif

/* Configuration for FAULT_POL_COUNT_ABS */
struct FaultPolicyCountAbs {
    fault_counter cntWarning;
    fault_counter cntError;
};

/* Configuration for FAULT_POL_COUNT_RESET */
struct FaultPolicyCountReset {
    fault_counter cntWarning;
    fault_counter cntError;
    fault_counter cntReset;
};

/* Configuration for FAULT_POL_TIME_RESET */
struct FaultPolicyTimeReset {
    fault_millisecs msWarning;
    fault_millisecs msError;
    fault_millisecs msReset;
};

struct FaultPolicy {
    fault_policy_type type;
    union { /* 'conf' based on 'type' */
        /* FAULT_POL_NONE has no configuration */
        /* FAULT_POL_COUNT_ABS */
        struct FaultPolicyCountAbs countAbs;
        /* FAULT_POL_COUNT_RESET */
        struct FaultPolicyCountReset countReset;
        /* FAULT_POL_TIME_RESET */
        struct FaultPolicyTimeReset timeReset;
    } conf;
};

typedef struct FaultPolicy FaultPolicy;

/* Single fault configuration record */
struct FaultConfRecord {
    fault_id id; /* primary key, row index */
    fault_module module;
    fault_code code;
    FaultPolicy policy;
};

typedef struct FaultConfRecord FaultConfRecord;

/* The fault_id is calculated by (module, code).
 * off = table[module].conf_offset
 * fault_id = off + code
 */
struct FaultModuleRecord {
    fault_module module; /* primary key, row index */
    fault_counter numCodes; /* number of codes in the modules */
    fault_id confOffset; /* where the module starts */
    /* Number of Errors at the same time
     * that leads to a module failure.
     */
    fault_counter tolerance;
};

typedef struct FaultModuleRecord FaultModuleRecord;

/* Register for a single fault.
 * It is updated during the validations and the policies applications.
 */
struct FaultCounterRecord {
    fault_id id;    /* primary key, row index */
    fault_counter errors; /* fault counter */
    fault_counter total;  /* all (faults + not faults) counter */
    fault_counter clear;   /* number of consecutive not faults */
    fault_millisecs msFirst; /* timestamp of the first fault */
    fault_millisecs msLast;  /* timestamp of the last fault */
    fault_status_type status;
    long refValue; /* a user reference value to add information */
};

typedef struct FaultCounterRecord FaultCounterRecord;

/* GLOBAL STUCTURES */

/* modules configuration table */
static FaultModuleRecord modules[FAULT_MODULE_MAX];
static fault_module modulesLen = 0;

/* faults configuration table */
static FaultConfRecord config[FAULT_ID_MAX];
static fault_id configLen = 0;

/* records table, len = configLen */
static FaultCounterRecord records[FAULT_ID_MAX];

/* PROCEDURES */

/* fault_id range validation */
static
bool fault_id_valid(fault_id id)
{
    return (id < configLen);
}

static
fault_status_type fault_policy_apply_count_abs(fault_id id)
{
    /* internal procedure, trust the input */
    fault_counter warn = config[id].policy.conf.countAbs.cntWarning;
    fault_counter err = config[id].policy.conf.countAbs.cntError;
    fault_counter e = records[id].errors;
    fault_status_type s = FAULT_ST_NORMAL;

    if (e >= warn){
        s = FAULT_ST_WARNING;
        if (err >= warn && e >= err){
            s = FAULT_ST_ERROR;
        }
    }

    return s;
}/* fault_policy_apply_count_abs */

static
fault_status_type fault_policy_apply_count_reset(fault_id id)
{
    /* internal procedure, trust the input */
    fault_counter reset = config[id].policy.conf.countReset.cntReset;
    fault_counter clear = records[id].clear;

    if (clear >= reset){
        fault_reset(id);
    }

    return fault_policy_apply_count_abs(id);
}/* fault_policy_apply_count_abs */

static
fault_status_type fault_policy_apply_time_reset(fault_id id)
{
    /* internal procedure, trust the input */
    fault_millisecs warn = config[id].policy.conf.timeReset.msWarning;
    fault_millisecs err = config[id].policy.conf.timeReset.msError;
    fault_millisecs reset = config[id].policy.conf.timeReset.msReset;

    fault_counter clear = records[id].clear;
    fault_millisecs last = records[id].msLast;

    fault_millisecs now = fault_now();

    if (clear > 0 && ((now - last) >= reset)){
        fault_reset(id);
    }

    /* calculate the status on the record (coudl be reset) */
    last = records[id].msLast;
    fault_millisecs first = records[id].msFirst;
    fault_millisecs elaps = (last - first);

    fault_status_type s = FAULT_ST_NORMAL;

    if (elaps >= warn){
        s = FAULT_ST_WARNING;
        if (err >= warn && elaps >= err){
            s = FAULT_ST_ERROR;
        }
    }

    return s;
}/* fault_policy_apply_count_reset */

static
fault_status_type fault_policy_apply(fault_id id)
{
    /* private method, the input is trusted */
    fault_status_type s = FAULT_ST_ERROR;

    switch (config[id].policy.type){
    case FAULT_POL_NONE:
        s = FAULT_ST_NORMAL;
        break;
    case FAULT_POL_COUNT_ABS:
        s = fault_policy_apply_count_abs(id);
        break;
    case FAULT_POL_COUNT_RESET:
        s = fault_policy_apply_count_reset(id);
        break;
    case FAULT_POL_TIME_RESET:
        s = fault_policy_apply_time_reset(id);
        break;
    default:
        /* in case of undefined/unimplemented policy,
         * in order to be identified
         */
        s = FAULT_ST_ERROR;
        break;
    }

    return s;
}/* fault_policy_apply */

void fault_init()
{
    //TODO split in subprocedures

    assert(FAULT_GENERIC_MODULE == 0);
    assert(FAULT_MODULE_MAX > 1);

    /* SETUP MODULES */
    for (fault_module i=0; i < FAULT_MODULE_MAX; i++){
        modules[i].module = i;
        modules[i].numCodes = 1;
        modules[i].confOffset = 0; /* empty module */
        modules[i].tolerance = FAULT_NO_FAILURE;
    }/* for modules */

    /* setup the generic module, cannot fail */
    modules[FAULT_GENERIC_MODULE].module = FAULT_GENERIC_MODULE;
    modules[FAULT_GENERIC_MODULE].numCodes = FAULT_GENERIC_ALL;
    /* this is valid just because FAULT_GENERIC_MODULE = 0 */
    modules[FAULT_GENERIC_MODULE].confOffset = 0;
    modules[FAULT_GENERIC_MODULE].tolerance = FAULT_NO_FAILURE;

    modulesLen = 1; /* one module configured */

    /* SETUP CONFIG */
    for (fault_id i = 0; i < FAULT_ID_MAX; i++){
        config[i].id = i;
        config[i].module = FAULT_GENERIC_MODULE;
        config[i].code = i;
        /* cannot fail */
        fault_policy_none(i);
    }/* for config */

    configLen = FAULT_GENERIC_ALL;

    /* SETUP RECORDS */
    for (fault_id i = 0; i < FAULT_ID_MAX; i++){
        records[i].id = i;
        fault_reset(i);
    }/* for config */

}/* fault_init () */

fault_module fault_conf_module(fault_counter ncodes, fault_counter tolerance)
{
    if (modulesLen >= FAULT_MODULE_MAX){
        return FAULT_MODULE_KO;
    }

    if (configLen + ncodes > FAULT_ID_MAX){
        return FAULT_MODULE_KO;
    }

    fault_module module = modulesLen;

    assert(modules[module].module == module);
    assert(configLen == (modules[module-1].confOffset +
                         modules[module-1].numCodes));

    modules[module].numCodes = ncodes;
    modules[module].confOffset = configLen;
    modules[module].tolerance = tolerance;
    modulesLen = modulesLen + 1;

    /* Set a NONE policy to all the new codes, as default */
    for (fault_id i = 0; i < (fault_id)ncodes; i++){
        fault_id id = configLen + i;
        assert(config[id].id == id);

        config[id].module = module;
        config[id].code = i;

        /* cannot fail */
        fault_policy_none(id);
    }/* for config */

    configLen = configLen + (fault_id)ncodes;

    return module;
}/* fault_conf_module */

bool fault_policy_none(fault_id id)
{
    if (!fault_id_valid(id)){
        return false;
    }

    memset(&config[id].policy, 0, sizeof(FaultPolicy));
    config[id].policy.type = FAULT_POL_NONE;

    return fault_reset(id);
}/* fault_policy_none */

fault_id fault_getid(fault_module mod, fault_code code)
{
    if (mod >= modulesLen){
        return FAULT_GENERIC_UNKNOWN;
    }

    if (code >= modules[mod].numCodes){
        return FAULT_GENERIC_UNKNOWN;
    }

    return (fault_id)(modules[mod].confOffset + code);
}/* fault_getid */

fault_status_type fault_status(fault_id id)
{
    if (id >= configLen){
        return FAULT_ST_ERROR;
    }

    return records[id].status;
}/* fault_status_type */

fault_status_module_type fault_status_module(fault_module mod)
{
    if (mod >= modulesLen){
        return FAULT_SM_FAILED;
    }

    assert(modules[mod].module == mod);

    fault_counter n = modules[mod].numCodes;
    fault_id o = modules[mod].confOffset;
    fault_counter t = modules[mod].tolerance;
    fault_id end = (fault_id)(o + n);

    assert(end <= configLen);

    fault_status_module_type s = FAULT_SM_NORMAL;
    fault_counter e = 0;

    /* NORMAL <= all NORMAL
     * WARNING <= exists a warning and not exists an error
     * FAULTED <= (0 < #errors <= tolerance)
     * FAILED <= (#errors > tolerance)
     */
    for (fault_id i = o; i < end; i++){
        switch (records[i].status){
        case FAULT_ST_NORMAL:
            /* empty */
            break;
        case FAULT_ST_WARNING:
            /* warning has low priority */
            if (s == FAULT_SM_NORMAL){ /* not warn, not err */
                s = FAULT_SM_WARNING; /* can change to fault, failed */
            }
            break;
        case FAULT_ST_ERROR:
            e++;
            break;
        default: /* should not happen */
            s = FAULT_SM_FAILED;
            break;
        }/* switch record status */
    }/* for record */

    if (0 < e && e <= t){
        s = FAULT_SM_FAULTED;
    } else if (e > t){
        s = FAULT_SM_FAILED;
    } /* else remains NORMAL or WARNING */

    return s;
}/* fault_status_module_type */


fault_status_type fault_update(fault_id id, long ref, bool condition)
{
    fault_id fid = id;

    if (id >= configLen){
        fid = fault_getid(FAULT_GENERIC_MODULE, FAULT_GENERIC_UNKNOWN);
    }

    assert(records[fid].id == fid);
    fault_millisecs now = fault_now();

    fault_counter totPrev = records[fid].total;
    fault_counter one = 1;
    fault_counter total = 0;

    /* records[fid].total += 1; */
    if (__builtin_add_overflow(totPrev, one, &total)){
        /* the other values are less or equal to total */
        fault_reset(id);
        records[fid].total += 1;
    }

    if (condition){
        if (records[fid].errors == 0){
            records[fid].msFirst = now;
        }
        records[fid].errors += 1;
        records[fid].msLast = now;
        records[fid].refValue = ref;
        records[fid].clear = 0; /* interupt the series */
    } else {
        records[fid].clear += 1;
    }

    /* Must be done after updating the record.
     * The policy can also reset the counters.
     */
    records[fid].status = fault_policy_apply(fid);

    return records[fid].status;
}/* fault_update */

fault_counter fault_count_errors(fault_id id)
{
    if (id >= configLen){
        return 0;
    }

    return records[id].errors;
}/* fault_count_errors */

bool fault_reset(fault_id id)
{
    if (id >= configLen){
        return false;
    }

    assert(records[id].id == id);

    records[id].errors = 0;
    records[id].total = 0;
    records[id].clear = 0;
    records[id].msFirst = 0;
    records[id].msLast = 0;
    records[id].status = FAULT_ST_NORMAL;
    records[id].refValue = 0;

    return true;
}/* fault_reset */

bool fault_policy_count_abs(fault_id id, fault_counter warn, fault_counter err)
{
    if (!fault_id_valid(id)){
        return false;
    }

    if (warn < 1){
        return false;
    }

    if (err < warn){
        return false;
    }

    /* input validated */

    struct FaultPolicyCountAbs conf = {
        .cntWarning = warn,
        .cntError = err
    };

    memset(&config[id].policy, 0, sizeof(FaultPolicy));
    config[id].policy.type = FAULT_POL_COUNT_ABS;
    config[id].policy.conf.countAbs = conf;

    return fault_reset(id);
}/* fault_policy_count_abs */

bool fault_policy_count_reset(fault_id id,
                              fault_counter warn,
                              fault_counter err,
                              fault_counter reset)
{
    if (!fault_id_valid(id)){
        return false;
    }

    if (warn < 1){
        return false;
    }

    if (err < warn){
        return false;
    }

    if (reset < 1){
        return false;
    }

    /* input validated */

    struct FaultPolicyCountReset conf = {
        .cntWarning = warn,
        .cntError = err,
        .cntReset = reset
    };

    memset(&config[id].policy, 0, sizeof(FaultPolicy));
    config[id].policy.type = FAULT_POL_COUNT_RESET;
    config[id].policy.conf.countReset = conf;

    return fault_reset(id);
}/* fault_policy_count_reset */

bool fault_policy_time_reset(fault_id id,
                             fault_millisecs warn,
                             fault_millisecs err,
                             fault_millisecs reset)
{
    if (!fault_id_valid(id)){
        return false;
    }

    if (warn < 1){
        return false;
    }

    if (err < warn){
        return false;
    }

    if (reset < 1){
        return false;
    }

    /* input validated */

    struct FaultPolicyTimeReset conf = {
        .msWarning = warn,
        .msError = err,
        .msReset = reset
    };

    memset(&config[id].policy, 0, sizeof(FaultPolicy));
    config[id].policy.type = FAULT_POL_TIME_RESET;
    config[id].policy.conf.timeReset = conf;

    return fault_reset(id);
}/* fault_policy_time_reset */
