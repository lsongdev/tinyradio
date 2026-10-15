#include "app/rds_activity.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  rds_activity_t activity = {0};
  si473x_rds_status_t status = {.synchronized = true};
  rds_activity_update(&activity, &status, true, 0);
  assert(!activity.receiving); // Sync alone is not data activity.
  status.usable_groups = 1;
  rds_activity_update(&activity, &status, true, 0);
  assert(activity.receiving); // Timestamp zero is a valid first event.
  status.usable_groups = 2;
  rds_activity_update(&activity, &status, true, 100);
  assert(activity.receiving);
  rds_activity_update(&activity, &status, true, 150);
  assert(!activity.receiving); // Traffic does not extend the pulse indefinitely.
  status.usable_groups = 3;
  rds_activity_update(&activity, &status, true, 300);
  assert(!activity.receiving); // Leave a visible gap in sustained traffic.
  rds_activity_update(&activity, &status, true, 400);
  assert(!activity.receiving); // No new group: do not flash on a timer alone.
  status.usable_groups = 4;
  rds_activity_update(&activity, &status, true, 401);
  assert(activity.receiving);
  status.synchronized = false;
  rds_activity_update(&activity, &status, true, 402);
  assert(!activity.receiving);
  status.synchronized = true;
  rds_activity_update(&activity, &status, true, 403);
  assert(!activity.receiving); // Retained groups cannot flash on resynchronization.
  ++status.usable_groups;
  rds_activity_update(&activity, &status, true, 404);
  assert(activity.receiving);
  status.usable_groups = 0;
  rds_activity_update(&activity, &status, false, 405);
  assert(!activity.receiving); // Tuning, seeking and errors clear old activity.
  rds_activity_update(&activity, &status, true, 406);
  assert(!activity.receiving);
  ++status.usable_groups;
  rds_activity_update(&activity, &status, true, UINT32_MAX - 70);
  assert(activity.receiving);
  rds_activity_update(&activity, &status, true, 79);
  assert(!activity.receiving); // Millisecond wrap still expires the pulse.
  status.usable_groups = UINT32_MAX;
  rds_activity_update(&activity, &status, false, 100);
  status.usable_groups = 0;
  rds_activity_update(&activity, &status, true, 101);
  assert(activity.receiving); // Counter wrap still detects new data.
  puts("RDS activity: data-only pulses, rate limit, sync/tuning reset and timer/counter wrap passed");
  return 0;
}
