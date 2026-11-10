/* PS4 bootstrap. Libc lock adapter adapted from ScummVM-PS4 (GPL-3.0). */
#include "libc_threading.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
extern int psydoomLogVulkanCapabilities(void);
extern int psydoomFindDisc(const char* const*, size_t, char*, size_t, FILE*);
extern int __real_main(int, const char *const *);
extern void vk_ps4_log_open(const char *);
extern void __real__exit(int) __attribute__((noreturn));
void __wrap__exit(int status) {
    sceSystemServiceLoadExec("exit", NULL);
    __real__exit(status);
}
int __wrap_main(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    ps4EnableLibcLocks();
    if (mkdir("/data/psydoom",0777)<0 && errno!=EEXIST) return 2;
    /* This SDK's freopen calls __dup3, which is an ENOSYS stub on PS4.
     * Keep console streams intact and own a separate diagnostic stream. */
    FILE *log=fopen("/data/psydoom/bootstrap.log","w");
    if (!log) return 2;
    setvbuf(log,NULL,_IONBF,0);
    fprintf(log,"PsyDoom PS4 Vulkan test 0.01: bootstrap entered\n");
    /* Verify that elapsed time includes sleep, unlike the SDK's old CPU clock. */
    struct timespec before, after;
    if (clock_gettime(CLOCK_MONOTONIC, &before) != 0) { fclose(log); return 5; }
    sceKernelUsleep(200000);
    if (clock_gettime(CLOCK_MONOTONIC, &after) != 0) { fclose(log); return 5; }
    long long elapsed_ns = (after.tv_sec-before.tv_sec)*1000000000LL + after.tv_nsec-before.tv_nsec;
    fprintf(log, "Monotonic clock sleep probe: %lld us (requested 200000)\n", elapsed_ns/1000);
    if (elapsed_ns < 150000000LL) { fprintf(log,"Clock failed elapsed-time check\n"); fclose(log); return 5; }

    int module=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    int user=sceUserServiceInitialize(NULL);
    fprintf(log,"SystemService load=%d UserService initialize=%d\n",module,user);
    vk_ps4_log_open("/data/psydoom/driver.log");
    if (psydoomLogVulkanCapabilities()) { fprintf(log,"Vulkan discovery failed; stopping\n"); fclose(log); return 4; }
    const char *roots[]={"/data/psydoom/disc", "/mnt/usb0/psydoom/disc", "/mnt/usb1/psydoom/disc"};
    char cue[1024];
    if (!psydoomFindDisc(roots,3,cue,sizeof(cue),log)) { fclose(log); return 3; }
    fprintf(log,"Disc present; entering game with Vulkan required\n");
    const char *args[]={"PsyDoom","-cue",cue,"-nolauncher",NULL};
    int result=__real_main(4,args);
    fprintf(log,"Game returned %d\n",result);
    fclose(log);
    return result;
}
