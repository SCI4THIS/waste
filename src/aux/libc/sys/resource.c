/* sys/resource.c — Resource limits for the WASTE guest libc. */

#include "../include/helper.h"
#include <sys/resource.h>
#include <errno.h>

static i64 resource_limits[32][2];
static i32 resources_ready;
static void init_resources(void){if(resources_ready)return;for(u32 i=0;i<32;i++)resource_limits[i][0]=resource_limits[i][1]=0x7fffffffffffffffLL;resources_ready=1;}
int getrlimit(int resource, struct rlimit *out) {
  init_resources();
  if (resource < 0 || resource >= 32) { errno = EINVAL; return -1; }
  out->rlim_cur = resource_limits[resource][0];
  out->rlim_max = resource_limits[resource][1];
  return 0;
}
int setrlimit(int resource, const struct rlimit *in) {
  init_resources();
  if (resource < 0 || resource >= 32 || in->rlim_cur > in->rlim_max) {
    errno = EINVAL; return -1;
  }
  resource_limits[resource][0] = in->rlim_cur;
  resource_limits[resource][1] = in->rlim_max;
  return 0;
}
int getrusage(int who, struct rusage *usage) {
  /* The runtimes supply wall/deadline clocks, not guest CPU accounting.
   * Do not fabricate usage or touch a buffer for an unsupported operation. */
  (void)usage;
  errno = who == RUSAGE_SELF || who == RUSAGE_CHILDREN ? ENOSYS : EINVAL;
  return -1;
}
