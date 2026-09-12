/* identity.c — User/group/service database records and hostname for the WASTE
 * guest libc. */

#include "common.h"

typedef struct WastePasswd { char *name; char *password; u32 uid; u32 gid; char *gecos; char *directory; char *shell; } WastePasswd;
typedef struct WasteGroup { char *name; char *password; u32 gid; char **members; } WasteGroup;
typedef struct WasteService { char *name; char **aliases; i32 port; char *protocol; } WasteService;
static u32 real_uid, effective_uid, real_gid, effective_gid;
static u32 *supplementary_groups; static u32 supplementary_count;
static WastePasswd passwd_record; static WasteGroup group_record; static WasteService service_record;
static i32 passwd_cursor, group_cursor, service_cursor;
static char *host_name;

void waste_identity_set(u32 uid, u32 euid, u32 gid, u32 egid, char *hostname) {
  real_uid=uid; effective_uid=euid; real_gid=gid; effective_gid=egid; host_name=hostname;
}
void waste_passwd_set(char *name,char *password,u32 uid,u32 gid,char *gecos,char *directory,char *shell) {
  passwd_record.name=name; passwd_record.password=password; passwd_record.uid=uid; passwd_record.gid=gid;
  passwd_record.gecos=gecos; passwd_record.directory=directory; passwd_record.shell=shell; passwd_cursor=0;
}
void waste_group_set(char *name,char *password,u32 gid,char **members) {
  group_record.name=name; group_record.password=password; group_record.gid=gid; group_record.members=members; group_cursor=0;
}
void waste_service_set(char *name,char **aliases,i32 port,char *protocol) {
  service_record.name=name; service_record.aliases=aliases; service_record.port=port; service_record.protocol=protocol; service_cursor=0;
}
i32 waste_groups_set(u32 count, const u32 *groups) {
  u32 *copy=malloc(count*4); if (count && !copy) return -1; if (count) bytes_copy(copy,groups,count*4);
  if (supplementary_groups) free(supplementary_groups); supplementary_groups=copy; supplementary_count=count; return 0;
}
u32 getuid(void){return real_uid;} u32 geteuid(void){return effective_uid;}
u32 getgid(void){return real_gid;} u32 getegid(void){return effective_gid;}
i32 setuid(u32 uid){if(effective_uid && uid!=real_uid&&uid!=effective_uid){*__errno_location()=1;return -1;}real_uid=effective_uid=uid;return 0;}
i32 setgid(u32 gid){if(effective_uid && gid!=real_gid&&gid!=effective_gid){*__errno_location()=1;return -1;}real_gid=effective_gid=gid;return 0;}
i32 getgroups(i32 capacity,u32 *groups){if(!capacity)return (i32)supplementary_count;if(capacity<(i32)supplementary_count){*__errno_location()=22;return -1;}bytes_copy(groups,supplementary_groups,supplementary_count*4);return (i32)supplementary_count;}
WastePasswd *getpwuid(u32 uid){return passwd_record.name&&passwd_record.uid==uid?&passwd_record:0;}
WastePasswd *getpwnam(const char *name){return passwd_record.name&&!c_compare(passwd_record.name,name)?&passwd_record:0;}
void setpwent(void){passwd_cursor=0;} WastePasswd *getpwent(void){if(passwd_cursor++||!passwd_record.name)return 0;return &passwd_record;} void endpwent(void){passwd_cursor=1;}
void setgrent(void){group_cursor=0;} WasteGroup *getgrent(void){if(group_cursor++||!group_record.name)return 0;return &group_record;} void endgrent(void){group_cursor=1;}
void setservent(i32 stayopen){(void)stayopen;service_cursor=0;} WasteService *getservent(void){if(service_cursor++||!service_record.name)return 0;return &service_record;} void endservent(void){service_cursor=1;}
i32 gethostname(char *destination,u32 capacity){if(!host_name||!capacity){*__errno_location()=22;return -1;}u32 n=c_length(host_name);if(n>=capacity){*__errno_location()=36;return -1;}bytes_copy(destination,host_name,n+1);return 0;}
