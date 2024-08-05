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
struct FaultGlobals {
    /* modules configuration table */
    FaultModuleRecord modules[FAULT_MODULE_MAX];
    fault_module modulesLen;

    /* faults configuration table */
    FaultConfRecord config[FAULT_ID_MAX];
    fault_id configLen;

    /* records table, len = configLen */
    FaultCounterRecord records[FAULT_ID_MAX];

    /* log queue */
    FaultLog logs[FAULT_LOG_MAX];
    size_t logsFront;
    size_t logsLen;
};

static struct FaultGlobals globals;

/* PROCEDURES */

/* fault_id range validation */
static
bool fault_id_valid(fault_id id)
{
    return (id < globals.configLen);
}

static
void fault_log_enqueue(const FaultLog log)
{
    size_t front = globals.logsFront;
    size_t len = globals.logsLen;
    size_t rear = (front + len) % FAULT_LOG_MAX;

    assert(len <= FAULT_LOG_MAX);
    assert(front + len < 2 * FAULT_LOG_MAX);
    assert(rear < FAULT_LOG_MAX);

    if (len == FAULT_LOG_MAX){
        /* queue full */
        front = (front + 1) % FAULT_LOG_MAX;
    } else {
        /* queue not full */
        len = (len + 1); /* since len < size, no modulo needed*/
    }

    assert(len <= FAULT_LOG_MAX);
    assert(front + len < 2 * FAULT_LOG_MAX);

    globals.logs[rear] = log;
    globals.logs[rear].saved = true;
    globals.logs[rear].index = rear;

    globals.logsFront = front;
    globals.logsLen = len;
}/* fault_log_enqueue */

static
fault_status_type fault_policy_apply_count_abs(fault_id id)
{
    /* internal procedure, trust the input */
    fault_counter warn = globals.config[id].policy.conf.countAbs.cntWarning;
    fault_counter err = globals.config[id].policy.conf.countAbs.cntError;
    fault_counter e = globals.records[id].errors;
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
    fault_counter reset = globals.config[id].policy.conf.countReset.cntReset;
    fault_counter clear = globals.records[id].clear;

    if (clear >= reset){
        fault_reset(id);
    }

    return fault_policy_apply_count_abs(id);
}/* fault_policy_apply_count_abs */

static
fault_status_type fault_policy_apply_time_reset(fault_id id)
{
    /* internal procedure, trust the input */
    fault_millisecs warn = globals.config[id].policy.conf.timeReset.msWarning;
    fault_millisecs err = globals.config[id].policy.conf.timeReset.msError;
    fault_millisecs reset = globals.config[id].policy.conf.timeReset.msReset;

    fault_counter clear = globals.records[id].clear;
    fault_millisecs last = globals.records[id].msLast;

    fault_millisecs now = fault_now();

    if (clear > 0 && ((now - last) >= reset)){
        fault_reset(id);
    }

    /* calculate the status on the record (coudl be reset) */
    last = globals.records[id].msLast;
    fault_millisecs first = globals.records[id].msFirst;
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

    switch (globals.config[id].policy.type){
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

/* for internal use only, it does not guarantee the global consistency */
static
void fault_modules_reset(void)
{
    assert(FAULT_GENERIC_MODULE == 0);
    assert(FAULT_MODULE_MAX > 1);

    for (fault_module i=0; i < FAULT_MODULE_MAX; i++){
        globals.modules[i].module = i;
        globals.modules[i].numCodes = 1;
        globals.modules[i].confOffset = 0; /* empty module */
        globals.modules[i].tolerance = FAULT_NO_FAILURE;
    }/* for modules */

    /* setup the generic module, cannot fail */
    globals.modules[FAULT_GENERIC_MODULE].module = FAULT_GENERIC_MODULE;
    globals.modules[FAULT_GENERIC_MODULE].numCodes = FAULT_GENERIC_ALL;
    /* this is valid just because FAULT_GENERIC_MODULE = 0 */
    globals.modules[FAULT_GENERIC_MODULE].confOffset = 0;
    globals.modules[FAULT_GENERIC_MODULE].tolerance = FAULT_NO_FAILURE;

    globals.modulesLen = 1; /* one module configured */
}/* fault_modules_reset */

/* for internal use only, it does not guarantee the global consistency */
static
void fault_config_reset(void)
{
    for (fault_id i = 0; i < FAULT_ID_MAX; i++){
        globals.config[i].id = i;
        globals.config[i].module = FAULT_GENERIC_MODULE;
        globals.config[i].code = i;
        /* cannot fail */
        fault_policy_none(i);
    }/* for config */

    globals.configLen = FAULT_GENERIC_ALL;
}/* fault_config_reset */

/* for internal use only, it does not guarantee the global consistency */
static
void fault_records_reset(void)
{
    for (fault_id i = 0; i < FAULT_ID_MAX; i++){
        globals.records[i].id = i;
        fault_reset(i);
    }/* for config */
}/* fault_records_reset */

void fault_init(void)
{
    fault_modules_reset();
    fault_config_reset();
    fault_records_reset();
    fault_logs_reset();
}/* fault_init () */

fault_module fault_conf_module(fault_counter ncodes, fault_counter tolerance)
{
    if (globals.modulesLen >= FAULT_MODULE_MAX){
        return FAULT_MODULE_KO;
    }

    if (globals.configLen + ncodes > FAULT_ID_MAX){
        return FAULT_MODULE_KO;
    }

    fault_module module = globals.modulesLen;

    assert(globals.modules[module].module == module);
    assert(globals.configLen == (globals.modules[module-1].confOffset +
                                 globals.modules[module-1].numCodes));

    globals.modules[module].numCodes = ncodes;
    globals.modules[module].confOffset = globals.configLen;
    globals.modules[module].tolerance = tolerance;
    globals.modulesLen = globals.modulesLen + 1;

    /* Set a NONE policy to all the new codes, as default */
    for (fault_id i = 0; i < (fault_id)ncodes; i++){
        fault_id id = globals.configLen + i;
        assert(globals.config[id].id == id);

        globals.config[id].module = module;
        globals.config[id].code = i;

        /* cannot fail */
        fault_policy_none(id);
    }/* for config */

    globals.configLen = globals.configLen + (fault_id)ncodes;

    return module;
}/* fault_conf_module */

bool fault_policy_none(fault_id id)
{
    if (!fault_id_valid(id)){
        return false;
    }

    memset(&globals.config[id].policy, 0, sizeof(FaultPolicy));
    globals.config[id].policy.type = FAULT_POL_NONE;

    return fault_reset(id);
}/* fault_policy_none */

fault_id fault_getid(fault_module mod, fault_code code)
{
    if (mod >= globals.modulesLen){
        return FAULT_GENERIC_UNKNOWN;
    }

    if (code >= globals.modules[mod].numCodes){
        return FAULT_GENERIC_UNKNOWN;
    }

    return (fault_id)(globals.modules[mod].confOffset + code);
}/* fault_getid */

fault_status_type fault_status(fault_id id)
{
    if (id >= globals.configLen){
        return FAULT_ST_ERROR;
    }

    return globals.records[id].status;
}/* fault_status_type */

fault_status_module_type fault_status_module(fault_module mod)
{
    if (mod >= globals.modulesLen){
        return FAULT_SM_FAILED;
    }

    assert(globals.modules[mod].module == mod);

    fault_counter n = globals.modules[mod].numCodes;
    fault_id o = globals.modules[mod].confOffset;
    fault_counter t = globals.modules[mod].tolerance;
    fault_id end = (fault_id)(o + n);

    assert(end <= globals.configLen);

    fault_status_module_type s = FAULT_SM_NORMAL;
    fault_counter e = 0;

    /* NORMAL <= all NORMAL
     * WARNING <= exists a warning and not exists an error
     * FAULTED <= (0 < #errors <= tolerance)
     * FAILED <= (#errors > tolerance)
     */
    for (fault_id i = o; i < end; i++){
        switch (globals.records[i].status){
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


bool fault_update(fault_id id, long ref, bool condition)
{
    fault_id fid = id;

    if (id >= globals.configLen){
        fid = fault_getid(FAULT_GENERIC_MODULE, FAULT_GENERIC_UNKNOWN);
    }

    assert(globals.records[fid].id == fid);
    fault_millisecs now = fault_now();

    fault_counter totPrev = globals.records[fid].total;
    fault_counter one = 1;
    fault_counter total = 0;

    /* records[fid].total += 1; */
    if (__builtin_add_overflow(totPrev, one, &total)){
        /* the other values are less or equal to total */
        fault_reset(id);
        globals.records[fid].total += 1;
    }

    if (condition){
        if (globals.records[fid].errors == 0){
            globals.records[fid].msFirst = now;
        }
        globals.records[fid].errors += 1;
        globals.records[fid].msLast = now;
        globals.records[fid].refValue = ref;
        globals.records[fid].clear = 0; /* interupt the series */
    } else {
        globals.records[fid].clear += 1;
    }

    /* Must be done after updating the record.
     * The policy can also reset the counters.
     */
    globals.records[fid].status = fault_policy_apply(fid);

    /* log */
    FaultLog log = {
        .saved = false,
        .index = 0,
        .timestamp = now,
        .module = globals.config[fid].module,
        .code = globals.config[fid].code,
        .status = globals.records[fid].status,
        .refValue = globals.records[fid].refValue
    };
    fault_log_enqueue(log);

    return condition;
}/* fault_update */

fault_counter fault_count_errors(fault_id id)
{
    if (id >= globals.configLen){
        return 0;
    }

    return globals.records[id].errors;
}/* fault_count_errors */

bool fault_reset(fault_id id)
{
    if (id >= globals.configLen){
        return false;
    }

    assert(globals.records[id].id == id);

    globals.records[id].errors = 0;
    globals.records[id].total = 0;
    globals.records[id].clear = 0;
    globals.records[id].msFirst = 0;
    globals.records[id].msLast = 0;
    globals.records[id].status = FAULT_ST_NORMAL;
    globals.records[id].refValue = 0;

    return true;
}/* fault_reset */

long fault_refval(fault_id id)
{
    if (id >= globals.configLen){
        return 0;
    }

    assert(globals.records[id].id == id);

    return globals.records[id].refValue;
}/* fault_refval */

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

    memset(&globals.config[id].policy, 0, sizeof(FaultPolicy));
    globals.config[id].policy.type = FAULT_POL_COUNT_ABS;
    globals.config[id].policy.conf.countAbs = conf;

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

    memset(&globals.config[id].policy, 0, sizeof(FaultPolicy));
    globals.config[id].policy.type = FAULT_POL_COUNT_RESET;
    globals.config[id].policy.conf.countReset = conf;

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

    memset(&globals.config[id].policy, 0, sizeof(FaultPolicy));
    globals.config[id].policy.type = FAULT_POL_TIME_RESET;
    globals.config[id].policy.conf.timeReset = conf;

    return fault_reset(id);
}/* fault_policy_time_reset */

void fault_logs_reset(void)
{
    memset(globals.logs, 0, sizeof(globals.logs));
    globals.logsFront = 0;
    globals.logsLen = 0;
}/* fault_logs_reset */

size_t fault_logs_length(void)
{
    assert(globals.logsLen <= FAULT_LOG_MAX);
    return globals.logsLen;
}/* fault_logs_length */

FaultLog fault_log(size_t index)
{
    FaultLog out = {0};
    out.saved = false;

    if (index < globals.logsLen){
        /* reverse order, 0 is the last inserted log */
        size_t rev = globals.logsLen - index - 1;
        size_t i = (globals.logsFront + rev) % FAULT_LOG_MAX;
        out = globals.logs[i];
        out.index = index;
    }

    return out;
}/* fault_log */
