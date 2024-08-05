# Faults - An Error Collector

`Faults` is a C module for handling errors (faults) that can lead to a failure.

## Features

- Agnostic about the actual errors, decoupled from the callers.
- Enhanced assert substitution.
- Configurable behaviour via policies.

## Configuration

At the very start of the application the user must configure the system.
Every module can have multiple error codes and the failures happen at the
module level via policies.

In a procedure, the data validation refers to the module of the procedure
and the specific code that has a meaning for the control.
For example, the module `SENSORS` can checks `SENSOR_PRESSURE` and
`SENSOR_TEMPERATURE` doing validation for the two sensors with different codes.

```
fault_module fmod;
fault_id fid_pres;
fault_id fid_temp;

void application_init()
{
    //...
    fault_init();
    /* 2 error codes for this module
     * 0 tollerance: one fault lead to a failure
    fmod = fault_conf_module(2, 0);
    if (fmod == FAULT_MODULE_KO){
        abort();
    }
    //...
    fid_pres = fault_getid(fmod, SENSOR_PRESSURE);
    fid_temp = fault_getid(fmod, SENSOR_TEMPERATURE);

    /* pressure warning where 1 error is present
     * failure when 2 errors are present
     */
    fault_policy_count_abs(fid_pres, 1, 2);

    /* temperature get only warning at 10 errors
     */
    fault_policy_count_reset(fid_pres, 10, 10);
}

void sensors_proc(long pressure, long temperature){
    // ...

    /* pressure must be greater than 1
    if (fault_update(fid_pres, pressure, pressure <= 0)){
        // register the fault
        // cannot recover, do nothing
        return;
    }

    /* pressure must be lower than 100 */
    if (fault_update(fid_temp, temperature, temperature >= 100)){
        // fault registered
        // use default valure for temperature
        temperature = 50;
    }
    // ...
}

void module_check()
{
    switch (fault_status_module(fmod)){
    case FAULT_SM_NORMAL:   /* no fault */
        break;
    case FAULT_SM_WARNING:  /* some codes in warning */
        // manage warning, alert the operator
        break;
    case FAULT_SM_FAULTED:  /* some codes in error */
        // manage fault, maybe disable some features
        break;
    case FAULT_SM_FAILED:   /* errors over the tolerance */
        // manage failure, go into a safe state
        break;
    default:
        abort();
    }
}
```
## Timestamps

The module works using an external time function that must be provided
by the user. The correct implemetation is mandatory to use the
`FAULT_POL_TIME_RESET` policy.

```
fault_millisecs fault_now(void);
```

It must return a monotonicaly increasing value representing
the time in milliseconds.
The user can refer to other units, but the output must always
increase (or be the same) with respect of the previous call.
It is used for marking the events timestamp.
The overflow of the counter must be managed by the user,
but it will be better if does not happen.

## Logs

The module also stores a limited amount of logs for further inspection.
The event stored in the logs are only the warnings and errors, not the single
validations, to trace the important events that lead to a module fault.

The amount of logs stored is, by default, just one in order to have a small
memory footprint.
For example, to change the maximum amount of the history to ten records,
set the compilation flag `FAULT_LOG_MAX=10`.

When the log queue is full, the oldest entry is removed to allow the newest to
be inserted. In this way, the user has the most recent history.

The queue can be empty using the procedure

```
void fault_logs_reset(void);
```

To inspect the log queue, an index is used, as follow

```
size_t len = fault_logs_length();

for (int i=0; i<len; i++){
    FaultLog log = fault_log(i);
    assert(log.saved); /* when 0 <= index < len */
    printf("%uli [%ui, %ui] (%i) %li\n",
            log.timestamp,
            log.module,
            log.code,
            log.status,
            log.refValue);
}
```
