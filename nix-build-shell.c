#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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

    // usernamespace setting
    int userns_ret = unshare(CLONE_NEWUSER);
    if (userns_ret != 0)
        perror("failure in usernamespace unshare");
    // write to /proc/self/uid_map and /proc/self/gid_map
    FILE *uid_map_fp = fopen("/proc/self/uid_map", "r+");
    if (uid_map_fp == NULL)
    {
        perror("uid_map file open failure");
        exit(1);
    }
    FILE *gid_map_fp = fopen("/proc/self/gid_map", "r+");
    if (gid_map_fp == NULL)
    {
        perror("gid_map file open failure");
        exit(1);
    }
    // fputs("1000 0 4294967295", uid_map_fp);
    // fputs("100 0 4294967295", gid_map_fp);
    fclose(uid_map_fp);
    fclose(gid_map_fp);

    // execute the basic command
    char *exec_argv[3 + argc];
    exec_argv[0] = shell_path;
    exec_argv[1] = "-c";
    exec_argv[2] = "source /tmp/nix-build-hello.drv-0/env-vars; exec \"$@\""; // /build/env-vars after successful setup
    exec_argv[3] = "--";
    for (int i = 0; i < argc - 2; i++)
        exec_argv[4 + i] = argv[2 + i];
    exec_argv[2 + argc] = (char *)0;
    
    // debug
    // char* exec_argv[] = {"/nix/store/a4yw1svqqk4d8lhwinn9xp847zz9gfma-bash-4.4-p23/bin/bash", "-c", "source /tmp/nix-build-hello.drv-0/env-vars; exec \"$@\"", "--", "cat", "/proc/$$/uid_map", (char*)0};
    // for (int i = 0; i < argc + 2; i++)
    //     printf("%s\n",exec_argv[i]);
    // fflush( stdout );

    execv(shell_path, exec_argv);
}
