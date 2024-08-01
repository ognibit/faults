# Faults - An Error Collector

`Faults` is a C module for handling errors (faults) that can lead to a failure.

## Features

- Agnostic about the actual errors, decoupled from the callers.
- Enhanced assert substitution.
- Configurable behaviour via policies.

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

