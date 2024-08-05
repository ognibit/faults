#include "faults.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

enum ModOne {
    MONE_1,
    MONE_2,
    MONE_3,
    MONE_ALL
};

enum ModTwo {
    MTWO_1,
    MTWO_2,
    MTWO_3,
    MTWO_4,
    MTWO_ALL
};

static fault_millisecs mockTime = 0;

fault_millisecs fault_now(void)
{
    return mockTime;
}/* fault_now */

void test_conf_module()
{
    printf("test_conf_module: ");

    fault_module mod1;
    fault_module mod2;

    fault_init();

    assert(fault_conf_module(INT_MAX, 0) == FAULT_MODULE_KO);

    mod1 = fault_conf_module(MONE_ALL, 1);
    mod2 = fault_conf_module(MTWO_ALL, 2);

    assert(mod1 != mod2);

    puts("OK");
}

void test_policy_none()
{
    printf("test_policy_none: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);

    fault_id fid = fault_getid(mod1, MONE_1);

    assert(fault_policy_none(fid));
    assert(!fault_policy_none(999));

    assert(!fault_update(fid, 1, false));
    assert(fault_count_errors(fid) == 0);

    assert(fault_update(fid, 2, true));
    assert(fault_count_errors(fid) == 1);

    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    /* set the policy trigger a reset */
    assert(fault_policy_none(fid));
    assert(fault_count_errors(fid) == 0);

    puts("OK");
}

void test_update()
{
    printf("test_update: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);
    fault_module mod2 = fault_conf_module(MTWO_ALL, 2);

    fault_id fid = fault_getid(mod1, MONE_1);
    fault_id fid2 = fault_getid(mod2, MTWO_1);
    fault_id fidg = fault_getid(FAULT_GENERIC_MODULE, FAULT_GENERIC_UNKNOWN);

    assert(!fault_update(99999, 0, false));
    assert(fault_update(99999, 0, true));
    assert(fault_count_errors(fidg) == 1);

    assert(fault_update(fid, 1, true));
    assert(fault_count_errors(fid) == 1);
    assert(fault_refval(fid) == 1);

    assert(fault_update(fid, 2, true));
    assert(fault_count_errors(fid) == 2);
    assert(fault_refval(fid) == 2);

    assert(fault_update(fid2, 1, true));
    assert(fault_count_errors(fid2) == 1);
    assert(fault_refval(fid2) == 1);
    assert(fault_count_errors(fid) == 2);

    assert(!fault_update(fid2, 9, false));
    assert(fault_refval(fid2) == 1);

    puts("OK");
}

void test_reset()
{
    printf("test_reset: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);
    fault_module mod2 = fault_conf_module(MTWO_ALL, 2);

    fault_id fid = fault_getid(mod1, MONE_1);
    fault_id fid2 = fault_getid(mod2, MTWO_1);

    fault_update(fid, 1, true);
    assert(fault_count_errors(fid) == 1);

    fault_update(fid, 2, true);
    assert(fault_count_errors(fid) == 2);

    /* control on second module */
    fault_update(fid2, 1, true);
    assert(fault_count_errors(fid2) == 1);

    assert(fault_reset(fid));
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    /* leave the other untouched */
    assert(fault_count_errors(fid2) == 1);

    puts("OK");
}

void test_policy_count_abs()
{
    printf("test_policy_count_abs: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);

    fault_id fid = fault_getid(mod1, MONE_1);

    assert(fault_policy_count_abs(fid, 1, 2));
    assert(!fault_policy_count_abs(999, 1, 2));
    assert(!fault_policy_count_abs(fid, 2, 1));

    fault_update(fid, 0, false);
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    fault_update(fid, 1, true);
    assert(fault_count_errors(fid) == 1);
    assert(fault_status(fid) == FAULT_ST_WARNING);
    assert(fault_status_module(mod1) == FAULT_SM_WARNING);

    fault_update(fid, 2, true);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_ERROR);
    assert(fault_status_module(mod1) == FAULT_SM_FAULTED);

    puts("OK");
}

void test_status_module()
{
    printf("test_status_module: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);
    fault_module mod2 = fault_conf_module(MTWO_ALL, 2);

    fault_id m1f1 = fault_getid(mod1, MONE_1);
    fault_id m1f2 = fault_getid(mod1, MONE_2);
    fault_id m2f1 = fault_getid(mod2, MTWO_1);
    fault_id m2f2 = fault_getid(mod2, MTWO_2);

    fault_policy_count_abs(m1f1, 1, 2);
    fault_policy_count_abs(m1f2, 1, 2);
    fault_policy_count_abs(m2f1, 1, 2);
    fault_policy_count_abs(m2f2, 1, 2);

    /* m1: 0 warn, 0 err
     * m2: 0 warn, 0 err
     */
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);
    assert(fault_status_module(mod2) == FAULT_SM_NORMAL);

    /* m1: 1 warn, 0 err
     * m2: 0 warn, 0 err
     */
    fault_update(m1f1, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_WARNING);
    assert(fault_status_module(mod2) == FAULT_SM_NORMAL);

    /* m1: 1 warn, 0 err
     * m2: 1 warn, 0 err
     */
    fault_update(m2f1, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_WARNING);
    assert(fault_status_module(mod2) == FAULT_SM_WARNING);

    /* m1: 2 warn, 0 err
     * m2: 1 warn, 0 err
     */
    fault_update(m1f2, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_WARNING);
    assert(fault_status_module(mod2) == FAULT_SM_WARNING);

    /* m1: 1 warn, 1 err (tol = 1)
     * m2: 1 warn, 0 err
     */
    fault_update(m1f2, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_FAULTED);
    assert(fault_status_module(mod2) == FAULT_SM_WARNING);

    /* m1: 0 warn, 2 err (tol = 1)
     * m2: 1 warn, 0 err
     */
    fault_update(m1f1, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_FAILED);
    assert(fault_status_module(mod2) == FAULT_SM_WARNING);

    /* m1: 0 warn, 2 err
     * m2: 0 warn, 1 err (tol = 2)
     */
    fault_update(m2f1, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_FAILED);
    assert(fault_status_module(mod2) == FAULT_SM_FAULTED);

    /* m1: 0 warn, 2 err
     * m2: 1 warn, 1 err (tol = 2)
     */
    fault_update(m2f2, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_FAILED);
    assert(fault_status_module(mod2) == FAULT_SM_FAULTED);

    /* m1: 0 warn, 2 err
     * m2: 0 warn, 2 err (tol = 2)
     */
    fault_update(m2f2, 0, true);
    assert(fault_status_module(mod1) == FAULT_SM_FAILED);
    assert(fault_status_module(mod2) == FAULT_SM_FAULTED);

    puts("OK");
}

void test_policy_count_reset()
{
    printf("test_policy_count_reset: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);

    fault_id fid = fault_getid(mod1, MONE_1);

    assert(fault_policy_count_reset(fid, 1, 2, 2));
    assert(!fault_policy_count_reset(999, 1, 2, 2));
    assert(!fault_policy_count_reset(fid, 2, 1, 2));
    assert(!fault_policy_count_reset(fid, 1, 2, 0));

    /* as policy count abs */
    fault_update(fid, 0, false);
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    fault_update(fid, 1, true);
    assert(fault_count_errors(fid) == 1);
    assert(fault_status(fid) == FAULT_ST_WARNING);
    assert(fault_status_module(mod1) == FAULT_SM_WARNING);

    fault_update(fid, 2, true);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_ERROR);
    assert(fault_status_module(mod1) == FAULT_SM_FAULTED);

    /* reset test */
    fault_update(fid, 3, false);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_ERROR);
    assert(fault_status_module(mod1) == FAULT_SM_FAULTED);

    fault_update(fid, 4, false);
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    puts("OK");
}

void test_policy_time_reset()
{
    printf("test_policy_time_reset: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);

    fault_id fid = fault_getid(mod1, MONE_1);

    assert(fault_policy_time_reset(fid, 4, 5, 3));
    assert(!fault_policy_time_reset(999, 1, 2, 2));
    assert(!fault_policy_time_reset(fid, 2, 1, 2));
    assert(!fault_policy_time_reset(fid, 1, 2, 0));

    /* 0 */
    mockTime = 0;
    fault_update(fid, 0, false);
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);
    assert(fault_status_module(mod1) == FAULT_SM_NORMAL);

    /* 01
     *  S
     */
    mockTime = 1;
    fault_update(fid, 1, true);
    assert(fault_count_errors(fid) == 1);
    assert(fault_status(fid) == FAULT_ST_NORMAL);

    /* 011
     *  S
     */
    mockTime = 2;
    fault_update(fid, 2, true);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_NORMAL);

    /* 0110
     *  S
     */
    mockTime = 3;
    assert(fault_update(fid, 3, false) == FAULT_ST_NORMAL);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_NORMAL);

    /* 01100
     *  S
     */
    mockTime = 4;
    fault_update(fid, 4, false);
    assert(fault_count_errors(fid) == 2);
    assert(fault_status(fid) == FAULT_ST_NORMAL);

    /* 011001
     *  S   W
     *  warning: it is passed more than 4 ms since the first error
     */
    mockTime = 5;
    fault_update(fid, 5, true);
    assert(fault_count_errors(fid) == 3);
    assert(fault_status(fid) == FAULT_ST_WARNING);

    /* 0110011
     *  S   WE
     */
    mockTime = 6;
    fault_update(fid, 6, true);
    assert(fault_count_errors(fid) == 4);
    assert(fault_status(fid) == FAULT_ST_ERROR);

    /* 01100110
     *  S   WE
     */
    mockTime = 7;
    fault_update(fid, 7, false);
    assert(fault_count_errors(fid) == 4);
    assert(fault_status(fid) == FAULT_ST_ERROR);

    /* 011001100
     *  S   WE
     */
    mockTime = 8;
    fault_update(fid, 8, false);
    assert(fault_count_errors(fid) == 4);
    assert(fault_status(fid) == FAULT_ST_ERROR);

    /* 0110011000
     *  S   WE  R
     *  reset: 3 ms of good event only
     */
    mockTime = 9;
    fault_update(fid, 9, false);
    assert(fault_count_errors(fid) == 0);
    assert(fault_status(fid) == FAULT_ST_NORMAL);

    puts("OK");
}

void test_logs(void)
{
    printf("test_logs: ");

    fault_init();
    fault_module mod1 = fault_conf_module(MONE_ALL, 1);

    fault_id fid1 = fault_getid(mod1, MONE_1);
    fault_id fid2 = fault_getid(mod1, MONE_2);

    fault_policy_count_abs(fid1, 1, 2);
    fault_policy_count_abs(fid2, 1, 2);

    assert(fault_logs_length() == 0);

    mockTime = 100;
    fault_update(fid1, 1, true);

    assert(fault_logs_length() == 1);
    assert(fault_log(0).saved);
    assert(fault_log(0).index == 0);
    assert(fault_log(0).timestamp == 100);
    assert(fault_log(0).module == mod1);
    assert(fault_log(0).code == MONE_1);
    assert(fault_log(0).status == FAULT_ST_WARNING);
    assert(fault_log(0).refValue == 1);

    mockTime = 101;
    fault_update(fid1, 2, true);

    assert(fault_logs_length() == 2);
    assert(fault_log(0).saved);
    assert(fault_log(0).index == 0);
    assert(fault_log(0).timestamp == 101);
    assert(fault_log(0).module == mod1);
    assert(fault_log(0).code == MONE_1);
    assert(fault_log(0).status == FAULT_ST_ERROR);
    assert(fault_log(0).refValue == 2);

    assert(fault_log(1).saved);
    assert(fault_log(1).index == 1);
    assert(fault_log(1).timestamp == 100);
    assert(fault_log(1).module == mod1);
    assert(fault_log(1).code == MONE_1);
    assert(fault_log(1).status == FAULT_ST_WARNING);
    assert(fault_log(1).refValue == 1);

    mockTime = 102;
    fault_update(fid1, 3, true);

    assert(FAULT_LOG_MAX == 2);
    assert(fault_logs_length() == 2);
    assert(fault_log(0).saved);
    assert(fault_log(0).index == 0);
    assert(fault_log(0).timestamp == 102);
    assert(fault_log(0).module == mod1);
    assert(fault_log(0).code == MONE_1);
    assert(fault_log(0).status == FAULT_ST_ERROR);
    assert(fault_log(0).refValue == 3);

    assert(fault_log(1).saved);
    assert(fault_log(1).index == 1);
    assert(fault_log(1).timestamp == 101);
    assert(fault_log(1).module == mod1);
    assert(fault_log(1).code == MONE_1);
    assert(fault_log(1).status == FAULT_ST_ERROR);
    assert(fault_log(1).refValue == 2);

    fault_logs_reset();
    assert(fault_logs_length() == 0);

    puts("OK");
}

int main()
{
    test_conf_module();
    test_policy_none();
    test_update();
    test_reset();
    test_policy_count_abs();
    test_status_module();
    test_policy_count_reset();
    test_policy_time_reset();
    test_logs();
    return 0;
}/* main */
