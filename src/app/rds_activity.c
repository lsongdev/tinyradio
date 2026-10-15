#include "rds_activity.h"
#include <string.h>

void rds_activity_update(rds_activity_t* activity, const si473x_rds_status_t* status, bool available, uint32_t now) {
  if (!available || !status->synchronized) {
    memset(activity, 0, sizeof *activity);
    activity->groups = status->usable_groups;
    return;
  }
  const bool received = status->usable_groups != activity->groups;
  activity->groups = status->usable_groups;
  if (received && (!activity->started || now - activity->started_at >= 400)) {
    activity->started = activity->receiving = true;
    activity->started_at = now;
  }
  if (activity->receiving && now - activity->started_at >= 150) activity->receiving = false;
}
