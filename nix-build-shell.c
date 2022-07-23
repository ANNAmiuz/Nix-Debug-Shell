#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

// #define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x) \
    do                 \
    {                  \
    } while (0)
#endif

#define MAX_LINE 256

void parse_env_vars_file(FILE *fp, char *shell_path, const char *match_pattern)
{
    ssize_t read = 0;
    char line[MAX_LINE];
    while ((read = fgets(&line, MAX_LINE, fp)))
    {
        // DEBUG_PRINT(("Retrieved line: "));
        // DEBUG_PRINT((line));
        if (strncmp(match_pattern, line, strlen(match_pattern)) == 0)
        {
            // DEBUG_PRINT((line));
            strcpy(shell_path, line + strlen(match_pattern) + 1);
            shell_path[strlen(shell_path) - 2] = '\0';
            DEBUG_PRINT((shell_path));
        }
    }
    return;
}

int main(int argc, const char **argv)
{
    // printf("Hello from %s. I got %d arguments\n", argv[0], argc);

    char *build_dir = argv[1];    // build_dir after nix-build command
    char env_vars_path[MAX_LINE]; // env_vars file path under build_dir
    strcpy(env_vars_path, build_dir);
    strcat(env_vars_path, "/env-vars");
    DEBUG_PRINT(("env_vars is of path: "));
    DEBUG_PRINT((env_vars_path));
    DEBUG_PRINT(("\n"));

    // parse the env_vars file: get the SHELL path
    FILE *env_vars_fp = fopen(env_vars_path, "r");
    if (env_vars_fp == NULL)
    {
        perror("env_vars file open failure");
        exit(1);
    }
    char shell_path[MAX_LINE];
    char *match_pattern = "declare -x SHELL=";
    parse_env_vars_file(env_vars_fp, shell_path, match_pattern);
    fclose(env_vars_fp);

    // namespace setting: user, uts, net
    int uid = getuid();
    int gid = getgid();
    int userns_ret = unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWNET);
    if (userns_ret != 0)
    {
        perror("failure in usernamespace unshare");
        exit(1);
    }

    /*-------------------------------usernamespace--------------------------------------*/
    // write deny to setgroups file
    FILE *setg_fp = fopen("/proc/self/setgroups", "w+");
    if (setg_fp == NULL)
    {
        perror("setgroups file open failure");
        exit(1);
    }
    fprintf(setg_fp, "deny");
    fclose(setg_fp);

    // write to /proc/self/uid_map and /proc/self/gid_map
    FILE *uid_map_fp = fopen("/proc/self/uid_map", "w+");
    if (uid_map_fp == NULL)
    {
        perror("uid_map file open failure");
        exit(1);
    }
    FILE *gid_map_fp = fopen("/proc/self/gid_map", "w+");
    if (gid_map_fp == NULL)
    {
        perror("gid_map file open failure");
        exit(1);
    }

    fprintf(uid_map_fp, "1000 %d 1", uid);
    fprintf(gid_map_fp, "100 %d 1", gid);
    fclose(uid_map_fp);
    fclose(gid_map_fp);

    /*--------------------------------utsnamespace--------------------------------------*/
    char *hostname = "localhost";
    sethostname(hostname, strlen(hostname));

    /*--------------------------------netnamespace--------------------------------------*/
    int fd = (socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
    struct ifreq ifr;
    strcpy(ifr.ifr_name, "lo");
    ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) == -1)
    {
        perror("Lo set flag failure");
        exit(1);
    }

    // execute the basic command
    char *exec_argv[3 + argc];
    exec_argv[0] = shell_path;
    exec_argv[1] = "-c";
    exec_argv[2] = "source /tmp/nix-build-hello.drv-0/env-vars; exec \"$@\""; // /build/env-vars after successful setup
    exec_argv[3] = "--";
    for (int i = 0; i < argc - 2; i++)
        exec_argv[4 + i] = argv[2 + i];
    exec_argv[2 + argc] = (char *)0;

    execv(shell_path, exec_argv);
}
