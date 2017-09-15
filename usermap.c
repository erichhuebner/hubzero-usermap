/**
 * @package      hubzero-usermap
 * @file         usermap.c
 * @author       Rick Kennell <kennell@purdue.edu>
 * @author       Nicholas J. Kisseberth <nkissebe@purdue.edu>
 * @copyright    Copyright (c) 2005-2015 HUBzero Foundation, LLC.
 * @license      http://opensource.org/licenses/MIT MIT
 *
 * Copyright (c) 2005-2015 HUBzero Foundation, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * HUBzero is a registered trademark of HUBzero Foundation, LLC.
 */


#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <getopt.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <stdarg.h>

static int xmp_getattr(const char *path, struct stat *stbuf)
{
    int res;

    res = lstat(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_access(const char *path, int mask)
{
    int res;

    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    int res;

    res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    if (S_ISREG(mode)) {
        res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(path, mode);
    else
        res = mknod(path, mode, rdev);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    int res;

    res = mkdir(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_unlink(const char *path)
{
    int res;

    res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rmdir(const char *path)
{
    int res;

    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
    int res;

    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rename(const char *from, const char *to)
{
    int res;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_link(const char *from, const char *to)
{
    int res;

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
    int res;

    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
    int res;

    res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_utime(const char *path, struct utimbuf *buf)
{
    int res;

    res = utime(path, buf);
    if (res == -1)
        return -errno;

    return 0;
}


static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    int res;

    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    close(res);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) fi;
    return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
                    size_t size)
{
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
    .getattr	= xmp_getattr,
    .access	= xmp_access,
    .readlink	= xmp_readlink,
    .readdir	= xmp_readdir,
    .mknod	= xmp_mknod,
    .mkdir	= xmp_mkdir,
    .symlink	= xmp_symlink,
    .unlink	= xmp_unlink,
    .rmdir	= xmp_rmdir,
    .rename	= xmp_rename,
    .link	= xmp_link,
    .chmod	= xmp_chmod,
    .chown	= xmp_chown,
    .truncate	= xmp_truncate,
    .utime	= xmp_utime,
    .open	= xmp_open,
    .read	= xmp_read,
    .write	= xmp_write,
    .statfs	= xmp_statfs,
    .release	= xmp_release,
    .fsync	= xmp_fsync,
#ifdef HAVE_SETXATTR
    .setxattr	= xmp_setxattr,
    .getxattr	= xmp_getxattr,
    .listxattr	= xmp_listxattr,
    .removexattr= xmp_removexattr,
#endif
};

char *fuse_options = NULL;
char source_user[100];
int  source_uid = 0;
char source_group[100];
int  source_gid = 0;
char user[100];
int uid;
char group[100];
int gid;
int do_debug;
int do_foreground;
int no_mtab = 0;
int sloppy_mount_options = 0;

void error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if (do_debug) {
        vfprintf(stderr, format, ap);
	fprintf(stderr,"\n");
    } else {
        vsyslog(LOG_ERR, format, ap);
    }
    va_end(ap);
}

#define PRINT_HELP 1

void print_help(char *argv[])
{
    error("usage: %s source_dir mountpoint [options]", argv[0]);
    error("options:");
    error("    -d                enable debug output");
    error("    -f                run in foreground");
    error("    -n                mount without writing in /etc/mtab");
    error("    -o opt,[opt...]   options");
}

void append_fuse_option(const char *opt)
{
    if (fuse_options == NULL) {
        fuse_options = malloc(strlen(opt) + 1);
        strcpy(fuse_options, opt);
    } else {
        // Allocate room for original string, comma, new string
        fuse_options = realloc(fuse_options, strlen(fuse_options) + 1
                                             + 1
                                             + strlen(opt) + 1);
        strcat(fuse_options, ",");
        strcat(fuse_options, opt);
    }
}

void parse_options(char *opt)
{
    char *cursor = NULL;
    char *ptr = NULL;

    ptr = strtok_r(opt, ",", &cursor);
    while(ptr) {
        if (strncmp(ptr,"rw",2) == 0) {
            // Ignore 'rw'.  It's inserted by autofs.
        } else if (strncmp(ptr,"source_user=",12) == 0) {
            strncpy(source_user,&ptr[12],sizeof(source_user));
            source_user[sizeof(source_user)-1] = 0;
        } else if (strncmp(ptr,"source_group=",13) == 0) {
            strncpy(source_group,&ptr[13],sizeof(source_group));
            source_group[sizeof(source_group)-1] = 0;
        } else if (strncmp(ptr,"user=",5) == 0) {
            strncpy(user,&ptr[5],sizeof(user));
            user[sizeof(user)-1] = 0;
        } else if (strncmp(ptr,"group=",6) == 0) {
            strncpy(group,&ptr[6],sizeof(group));
            group[sizeof(group)-1] = 0;
        } else {
            append_fuse_option(ptr);
        }
        ptr = strtok_r(NULL, ",", &cursor);
    }
}

int main(int argc, char *argv[])
{
    struct fuse *fuse;
    char *mountpoint;
    int multithreaded;
    int res;
    int fd;
    char * source_dir = strdup(argv[1]);
    const char * target_dir = argv[2];

    openlog("usermap", 0, LOG_DAEMON);

    if ((argc < 3) || (argv[1][0] == '-') || (argv[2][0] == '1')) {
        do_debug = 1;
        print_help(argv);
        return 1;
    }

    extern int optind, opterr, optopt;
    struct option long_options[] = {
        // name, has_arg, flag, value
        {"help", 0, 0, PRINT_HELP},
        {0,0,0,0},
    };

    while(1) {
        int c;
        int option_index;
        c = getopt_long(argc-2,&argv[2], "+sndhfo:", long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch(c) {
            case 0:
                break;
            case PRINT_HELP:
                print_help(argv);
                return 0;
            case 'n':
                no_mtab=1;
                break;
            case 'd':
                do_debug=1;
                do_foreground=1;
                break;
            case 'f':
                do_foreground=1;
                break;
            case 'o':
                parse_options(optarg);
                break;
            case 's':
                sloppy_mount_options=1;
                break;
            default:
                print_help(argv);
                return 1;
        }
    }

//#define DEBUG_ARGS
#ifdef DEBUG_ARGS
    error("source_dir = '%s'", source_dir);
    error("mountpoint = '%s'", target_dir);
    if (user[0] != '\0') error("user = '%s'", user);
    if (group[0] != '\0') error("group = '%s'", group);
    if (source_user[0] != '\0') error("source_user = '%s'", source_user);
    if (source_group[0] != '\0') error("source_group = '%s'", source_group);
    if (do_debug) error("debug");
    if (do_foreground) error("foreground");
#endif

    if (user[0] != '\0') {
        struct passwd *passwd = getpwnam(user);
        if (passwd == NULL) {
            error("User '%s' is not known.", user);
            return 1;
        }
        uid = passwd->pw_uid;
        gid = passwd->pw_gid;
    }

    if (group[0] != '\0') {
        const char *g = group;  // getgrnam expects a strict const char *.
        struct group *group = getgrnam(g);
        if (group == NULL) {
            error("Group '%s' is not known.", group);
            return 1;
        }
        gid = group->gr_gid;
    }

    if (uid != 0) {
        char buf[100];
        sprintf(buf,"uid=%d",uid);
        append_fuse_option(buf);
    }

    if (gid != 0) {
        char buf[100];
        sprintf(buf,"gid=%d",gid);
        append_fuse_option(buf);
    }

    char **new_argv = malloc(2*sizeof(char *));
    new_argv[0] = strdup(argv[0]);
    new_argv[1] = strdup(target_dir);
    int new_argc = 2;

    if (do_debug) {
        new_argv = realloc(new_argv, (new_argc+1) * sizeof(char *));
        new_argv[new_argc] = strdup("-d");
        new_argc++;
    }

    if (do_foreground) {
        new_argv = realloc(new_argv, (new_argc+1) * sizeof(char *));
        new_argv[new_argc] = strdup("-f");
        new_argc++;
    }

    if (fuse_options != NULL) {
        new_argv = realloc(new_argv, (new_argc+2) * sizeof(char *));
        new_argv[new_argc] = strdup("-o");
        new_argv[new_argc+1] = strdup(fuse_options);
        new_argc += 2;
    }

#ifdef DEBUG_ARGS
    int i;
    for(i=0; i<new_argc; i++) {
        error("%2d: '%s'", i, new_argv[i]);
    }
    return 0;
#endif

    if (source_dir[0] != '/') {
        // Treat the source_dir as a username.
        if (source_user[0] != '\0') {
            if (strcmp(source_user,source_dir) != 0) {
                error("source_user does not match source_dir.");
                return 1;
            }
        } else {
            strcpy(source_user, source_dir);
        }
    }

    struct passwd *passwd = getpwnam(source_user);
    if (passwd == NULL) {
        error("User '%s' is not known.", source_user);
        return 1;
    }
    source_uid = passwd->pw_uid;
    source_gid = passwd->pw_gid;
    if (source_dir[0] != '/') {
        free(source_dir);
        source_dir = strdup(passwd->pw_dir);
    }

    // Make sure that source_dir exists...
    struct stat stat_buf;
    res = stat(source_dir, &stat_buf);
    if (res < 0) {
        error("Directory '%s' does not exist.", source_dir);
        return 1;
    }

    if (S_ISDIR(stat_buf.st_mode) == 0) {
        error("'%s' is not a directory.", source_dir);
        return 1;
    }

    if (source_group[0] != '\0') {
        struct group *group = getgrnam(source_group);
        if (group == NULL) {
            error("Group '%s' is not known.", source_group);
            return 1;
        }
        source_gid = group->gr_gid;
    }

    //-------------------------------------------------------------------------
    // The regular fuse setup.
    //-------------------------------------------------------------------------
    fuse = fuse_setup(new_argc, new_argv, &xmp_oper, sizeof(xmp_oper),
                      &mountpoint, &multithreaded, &fd);

    if (fuse == NULL) {
        return 1;
    }

    umask(0);

    res = setfsuid(source_uid);
    if (res < 0) {
        perror("setfsuid");
        return 1;
    }

    // We must initialize the group list BEFORE chroot.
    // Otherwise we won't be able to see the authentication entities.
    res = initgroups(source_user, source_gid);
    if (res < 0) {
        perror("initgroups");
        return 1;
    }

    res = chroot(source_dir);
    if (res < 0) {
        perror("chroot");
        return 1;
    }

    res = setregid(source_gid,source_gid);
    if (res < 0) {
        perror("setregid");
        return 1;
    }

    res = setreuid(source_uid,source_uid);
    if (res < 0) {
        perror("setreuid");
        return 1;
    }

    // Single-threaded loop.
    res = fuse_loop(fuse);

    fuse_teardown(fuse, fd, mountpoint);
    if (res == -1)
        return 1;

    return 0;
}
