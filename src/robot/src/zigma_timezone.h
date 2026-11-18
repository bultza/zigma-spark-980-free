#ifndef ZIGMA_TIMEZONE_H
#define ZIGMA_TIMEZONE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static inline const char *zigma_zone_dir(void) {
 return access("/root/zigma-free/share/zoneinfo/zone.tab",R_OK)==0 ?
  "/root/zigma-free/share/zoneinfo" : "/usr/share/zoneinfo";
}
static inline void zigma_zone_apply(const char *zone) {
 char path[256];snprintf(path,sizeof(path),"%s/%s",zigma_zone_dir(),zone);
 setenv("TZ",path,1);tzset();
}
#endif
