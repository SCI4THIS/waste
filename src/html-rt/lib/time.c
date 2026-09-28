/* time.c — Time conversion and formatting for the WASTE guest libc. */

#include "include/helper.h"

typedef struct WasteTm{i32 sec,min,hour,mday,mon,year,wday,yday,isdst;}WasteTm;
typedef struct WasteTimeval{i64 sec;i32 usec,padding;}WasteTimeval;
extern i32 waste_kernel_realtime_v1(i64 *seconds, i32 *nanoseconds)
  __attribute__((import_module("waste_kernel"), import_name("realtime_v1")));

i64 time(i64 *result) {
  i64 seconds = 0; i32 nanoseconds = 0;
  i32 status = waste_kernel_realtime_v1(&seconds, &nanoseconds);
  if(status < 0){*__errno_location()=-status;return -1;}
  if(result)*result=seconds;
  return seconds;
}

i32 gettimeofday(WasteTimeval *value, void *timezone) {
  i64 seconds = 0; i32 nanoseconds = 0;
  (void)timezone;
  if(!value){*__errno_location()=14;return -1;}
  i32 status=waste_kernel_realtime_v1(&seconds,&nanoseconds);
  if(status<0){*__errno_location()=-status;return -1;}
  value->sec=seconds;value->usec=nanoseconds/1000;value->padding=0;return 0;
}
i32 settimeofday(const WasteTimeval *value, const void *timezone) {
  (void)value;(void)timezone;*__errno_location()=1;return -1;
}
static WasteTm tm_value;
static i32 leap(i32 y){return y%4==0&&(y%100!=0||y%400==0);}
static WasteTm *localtime_core(i64 timer) {
  i64 seconds=timer;
  i64 days=seconds/86400,rest=seconds%86400;
  if(rest<0){rest+=86400;days--;}
  tm_value.hour=(i32)(rest/3600);tm_value.min=(i32)((rest/60)%60);tm_value.sec=(i32)(rest%60);
  tm_value.wday=(i32)((days+4)%7);if(tm_value.wday<0)tm_value.wday+=7;
  i32 year=1970;
  while(days>=(leap(year)?366:365))days-=leap(year++)?366:365;
  while(days<0){year--;days+=leap(year)?366:365;}
  tm_value.year=year-1900;tm_value.yday=(i32)days;
  static const unsigned char month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  i32 month=0;while(month<11){i32 n=month_days[month]+(month==1&&leap(year));if(days<n)break;days-=n;month++;}
  tm_value.mon=month;tm_value.mday=(i32)days+1;tm_value.isdst=0;
  return &tm_value;
}
WasteTm *localtime(const i64*timer){return localtime_core(*timer);}
WasteTm *localtime_r(const i64*timer,WasteTm*result){WasteTm*r=localtime_core(*timer);*result=*r;return result;}
WasteTm *gmtime(const i64*timer){return localtime_core(*timer);}
WasteTm *gmtime_r(const i64*timer,WasteTm*result){return localtime_r(timer,result);}
WasteTm *localtime_rz(void*tz,const i64*timer,WasteTm*result){(void)tz;return localtime_r(timer,result);}
void *tzalloc(const char*name){(void)name;return (void*)1;}
void tzfree(void*tz){(void)tz;}
void tzset(void){}
static i64 mktime_core(const WasteTm*tm){
  i32 y=tm->year+1900,m=tm->mon,d=tm->mday-1;
  i64 days=0;for(i32 i=1970;i<y;i++)days+=leap(i)?366:365;
  static const unsigned char md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  for(i32 i=0;i<m;i++)days+=md[i]+(i==1&&leap(y));
  days+=d;return days*86400+tm->hour*3600+tm->min*60+tm->sec;
}
i64 mktime(WasteTm*tm){return mktime_core(tm);}
i64 mktime_z(void*tz,WasteTm*tm){(void)tz;return mktime_core(tm);}
static void two_digits(char*out,i32 v){out[0]=(char)('0'+v/10%10);out[1]=(char)('0'+v%10);}
static u32 strftime_core(char*out,u32 capacity,const char*format,const WasteTm*tm){
  static const char*abmon[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  static const char*abday[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  u32 n=0;for(u32 i=0;format[i];i++){
    if(format[i]!='%'){if(n+1>=capacity)return 0;out[n++]=format[i];continue;}
    char c=format[++i];
    if(c=='Y'){i32 y=tm->year+1900;if(n+4>=capacity)return 0;out[n++]=(char)('0'+y/1000%10);out[n++]=(char)('0'+y/100%10);out[n++]=(char)('0'+y/10%10);out[n++]=(char)('0'+y%10);}
    else if(c=='m'||c=='d'||c=='H'||c=='M'||c=='S'||c=='e'){
      if(n+2>=capacity)return 0;
      i32 v=c=='m'?tm->mon+1:c=='d'||c=='e'?tm->mday:c=='H'?tm->hour:c=='M'?tm->min:tm->sec;
      if(c=='e'){out[n++]=v<10?' ':(char)('0'+v/10);out[n++]=(char)('0'+v%10);}
      else{two_digits(out+n,v);n+=2;}
    }
    else if(c=='b'||c=='h'){const char*s=abmon[tm->mon%12];if(n+3>=capacity)return 0;out[n++]=s[0];out[n++]=s[1];out[n++]=s[2];}
    else if(c=='a'){const char*s=abday[tm->wday%7];if(n+3>=capacity)return 0;out[n++]=s[0];out[n++]=s[1];out[n++]=s[2];}
    else if(c=='%'){if(n+1>=capacity)return 0;out[n++]='%';}
    else if(c=='-'){/* skip padding modifier, process next */}
    else return 0;
  }if(capacity)out[n]=0;return n;
}
u32 strftime(char*out,u32 capacity,const char*format,const WasteTm*tm){return strftime_core(out,capacity,format,tm);}
u32 strftime_z(void*tz,char*out,u32 capacity,const char*format,const WasteTm*tm){(void)tz;return strftime_core(out,capacity,format,tm);}
