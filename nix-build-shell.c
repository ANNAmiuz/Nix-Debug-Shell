#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

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

void err_report(bool condition, const char *errmsg)
{
    if (condition)
    {
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}

/*   ** Function return value meaning
 * -1 cannot open source file
 * -2 cannot open destination file
 * 0 Success
 */
int File_Copy(char FileSource[], char FileDestination[])
{
    int c;
    FILE *stream_R;
    FILE *stream_W;

    stream_R = fopen(FileSource, "r");
    if (stream_R == NULL)
        return -1;
    stream_W = fopen(FileDestination, "w"); // create and write to file
    if (stream_W == NULL)
    {
        fclose(stream_R);
        return -2;
    }
    while ((c = fgetc(stream_R)) != EOF)
        fputc(c, stream_W);
    fclose(stream_R);
    fclose(stream_W);

    return 0;
}

void write_to_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w+");
    fprintf(fp, content);
    return;
}

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

void get_abs_path_name(const char *root_path, char *path_to_add, char *ret)
{
    memset(ret, 0, sizeof(char) * MAX_LINE);
    strcpy(ret, root_path);
    strcat(ret, path_to_add);
    return;
}

// sandbox_root_path: a tmp dir as / in the sandbox env
// shell_path: parsed from build_path/env-vars
// build_path: 2nd argument passed to nix-build-shell program
void mountns_prepare(const char *sandbox_root_path, const char *shell_path, const char *build_path)
{
    char target_path[MAX_LINE];
    int mount_ret, mkret;

    /// nix
    get_abs_path_name(sandbox_root_path, "/nix", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /nix failure");
    mount_ret = mount("/nix", target_path, 0, MS_REC | MS_PRIVATE | MS_BIND, NULL);
    err_report(mount_ret == -1, "mount nix failure");

    /// build
    get_abs_path_name(sandbox_root_path, "/build", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /build failure");
    mount_ret = mount(build_path, target_path, 0, MS_REC | MS_PRIVATE | MS_BIND, NULL);
    err_report(mount == -1, "mount /build failure");

    /// bin
    get_abs_path_name(sandbox_root_path, "/bin", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /bin failure");
    get_abs_path_name(sandbox_root_path, "/bin/sh", target_path);
    mknod(target_path, S_IFREG | 0666, 0);
    mount_ret = mount(shell_path, target_path, 0, MS_REC | MS_PRIVATE | MS_BIND, NULL);
    err_report(mount_ret == -1, "mount /bin/sh failure");
    
    /// etc
    get_abs_path_name(sandbox_root_path, "/etc", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /etc failure");
    get_abs_path_name(sandbox_root_path, "/etc/group", target_path);
    mknod(target_path, S_IFREG | 0666, 0);
    write_to_file(target_path, "root:x:0:\nnixbld:!:100:\nnogroup:x:65534:\n");
    get_abs_path_name(sandbox_root_path, "/etc/passwd", target_path);
    mknod(target_path, S_IFREG | 0666, 0);
    write_to_file(target_path, "root:x:0:0:Nix build user:/build:/noshell\nnixbld:x:1000:100:Nix build user:/build:/noshell\nnobody:x:65534:65534:Nobody:/:/noshell\n");
    get_abs_path_name(sandbox_root_path, "/etc/hosts", target_path);
    mknod(target_path, S_IFREG | 0666, 0);
    write_to_file(target_path, "127.0.0.1 localhost\n::1 localhost\n");

    /// dev
    get_abs_path_name(sandbox_root_path, "/dev", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /dev failure");
    mount_ret = mount("/dev", target_path, 0, MS_REC | MS_PRIVATE | MS_BIND, NULL);
    get_abs_path_name(sandbox_root_path, "/dev/fd", target_path);
    symlink("/proc/self/fd", target_path);
    get_abs_path_name(sandbox_root_path, "/dev/stdin", target_path);
    symlink("/proc/self/fd/0", target_path);
    get_abs_path_name(sandbox_root_path, "/dev/stdout", target_path);
    symlink("/proc/self/fd/1", target_path);
    get_abs_path_name(sandbox_root_path, "/dev/stderr", target_path);
    symlink("/proc/self/fd/2", target_path);

    /// etc
    get_abs_path_name(sandbox_root_path, "/tmp", target_path);
    mkret = mkdir(target_path, 0777);

    /// proc
    get_abs_path_name(sandbox_root_path, "/proc", target_path);
    mkret = mkdir(target_path, 0700);
    err_report(mkret == -1, "mkdir /proc failure");
    mount_ret = mount("/proc", target_path, "proc", MS_REC | MS_BIND, "");
    err_report(mkret == -1, "mkdir /proc failure");
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
    int ns_ret = unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWNS);
    if (ns_ret != 0)
    {
        perror("unshare failure");
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

    /*--------------------------------mountnamespace--------------------------------------*/
    // create tmp dir for sandbox env
    char name[] = "/tmp/sandboxXXXXXX";
    char *sandbox_root_path = mkdtemp(name);
    if (sandbox_root_path == NULL)
    {
        perror("mkdtemp failure");
        exit(1);
    }
    mountns_prepare(sandbox_root_path, shell_path, build_dir);
    int chr_ret = chroot(sandbox_root_path);
    if (chr_ret == -1)
    {
        perror("chroot failure");
        exit(1);
    }
    int ch_dir_ret = chdir("/");
    if (ch_dir_ret == -1)
    {
        perror("chdir failure");
        exit(1);
    }

    /*--------------------------------execute the basic command--------------------------------------*/
    char *exec_argv[3 + argc];
    exec_argv[0] = shell_path;
    exec_argv[1] = "-c";
    // exec_argv[2] = "source /tmp/nix-build-hello.drv-3/env-vars; exec \"$@\""; // change to /build/env-vars after successful setup mountns
    exec_argv[2] = "source /build/env-vars; exec \"$@\"";
    exec_argv[3] = "--";
    for (int i = 0; i < argc - 2; i++)
        exec_argv[4 + i] = argv[2 + i];
    exec_argv[2 + argc] = (char *)0;

    execv(shell_path, exec_argv);
}
