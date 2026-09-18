/* time.c — Time conversion and formatting for the WASTE guest libc. */

#include "include/helper.h"

typedef struct WasteTm{i32 sec,min,hour,mday,mon,year,wday,yday,isdst;}WasteTm;
static WasteTm tm_value;
static i32 leap(i32 y){return y%4==0&&(y%100!=0||y%400==0);}
WasteTm *localtime(const i64*timer){i64 seconds=*timer,days=seconds/86400,rest=seconds%86400;if(rest<0){rest+=86400;days--;}tm_value.hour=rest/3600;tm_value.min=(rest/60)%60;tm_value.sec=rest%60;tm_value.wday=(i32)((days+4)%7);if(tm_value.wday<0)tm_value.wday+=7;i32 year=1970;while(days>=(leap(year)?366:365))days-=leap(year++)?366:365;while(days<0){year--;days+=leap(year)?366:365;}tm_value.year=year-1900;tm_value.yday=(i32)days;static const unsigned char month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};i32 month=0;while(month<11){i32 n=month_days[month]+(month==1&&leap(year));if(days<n)break;days-=n;month++;}tm_value.mon=month;tm_value.mday=(i32)days+1;tm_value.isdst=0;return &tm_value;}
void tzset(void){}
static void two_digits(char*out,i32 v){out[0]=(char)('0'+v/10%10);out[1]=(char)('0'+v%10);}
u32 strftime(char*out,u32 capacity,const char*format,const WasteTm*tm){u32 n=0;for(u32 i=0;format[i];i++){if(format[i]!='%'){if(n+1>=capacity)return 0;out[n++]=format[i];continue;}char c=format[++i];if(c=='Y'){i32 y=tm->year+1900;if(n+4>=capacity)return 0;out[n++]=(char)('0'+y/1000%10);out[n++]=(char)('0'+y/100%10);out[n++]=(char)('0'+y/10%10);out[n++]=(char)('0'+y%10);}else if(c=='m'||c=='d'||c=='H'||c=='M'||c=='S'){if(n+2>=capacity)return 0;i32 v=c=='m'?tm->mon+1:c=='d'?tm->mday:c=='H'?tm->hour:c=='M'?tm->min:tm->sec;two_digits(out+n,v);n+=2;}else if(c=='%'){if(n+1>=capacity)return 0;out[n++]='%';}else return 0;}if(capacity)out[n]=0;return n;}
