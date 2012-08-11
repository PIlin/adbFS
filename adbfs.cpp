/**
   @file
   @author  Calvin Tee (collectskin.com)
   @author  Sudarshan S. Chawathe (eip10.org)
   @version 0.1

   @section License

   BSD; see comments in main source files for details.

   @section Description

   A FUSE-based filesystem using the Android ADB interface.

   @mainpage

   adbFS: A FUSE-based filesystem using the Android ADB interface.

   Usage: To mount use

   @code adbfs mountpoint @endcode

   where mountpoint is a suitable directory. To unmount, use

   @code fusermount -u mountpoint @endcode

   as usual for FUSE.

   The above assumes you have a fairly standard Android development
   setup, with adb in the path, busybox available on the Android
   device, etc.  Everything is very lightly tested and a work in
   progress.  Read the source and use with caution.

*/

/*
 *      Software License Agreement (BSD License)
 *
 *      Copyright (c) 2010-2011, Calvin Tee (collectskin.com)
 *
 *      2011-12-25 Updated by Sudarshan S. Chawathe (chaw@eip10.org).
 *                 Fixed some problems due to filenames with spaces.
 *                 Added comments and miscellaneous small changes.
 *
 *      All rights reserved.
 *
 *      Redistribution and use in source and binary forms, with or without
 *      modification, are permitted provided that the following conditions are
 *      met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following disclaimer
 *        in the documentation and/or other materials provided with the
 *        distribution.
 *      * Neither the name of the  nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 *      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *      A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *      OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define FUSE_USE_VERSION 26
#include "utils.h"

#include "adb_interface.h"

using namespace std;

void shell_escape_command(string&);
void adb_shell_escape_command(string&);
queue<string> adb_shell(const string);
queue<string> shell(const string);
void clearTmpDir();

map<string,fileCache> fileData;
map<int,bool> filePendingWrite;
map<string,bool> fileTruncated;

/**
   Return the result of executing the given command string, using
   exec_command, on the local host.

   @param command the command to execute.
   @see exec_command.
 */
queue<string> shell(const string command)
{
    string actual_command;
    actual_command.assign(command);
    shell_escape_command(actual_command);
    return exec_command(actual_command);
}

/**
   Return the result of executing the given command on the Android
   device using adb.

   The given string command is prefixed with "adb shell busybox " to
   yield the adb command line.

   @param command the command to execute.
   @see exec_command.
   @todo perhaps avoid use of local shell to simplify escaping.
 */
queue<string> adb_shell(const string command)
{
    string actual_command;
    actual_command.assign(command);
    adb_shell_escape_command(actual_command);
    actual_command.insert(0, "adb shell busybox ");
    return exec_command(actual_command);
}

/**
   Modify, in place, the given string by escaping characters that are
   special to the shell.

   @param cmd the string to be escaped.
   @see adb_shell_escape_command.
   @todo check/simplify escaping.
 */
void shell_escape_command(string& cmd)
{
    string_replacer(cmd,"\\","\\\\");
    string_replacer(cmd,"'","\\'");
    string_replacer(cmd,"`","\\`");
}

/**
   Modify, in place, the given string by escaping characters that are
   special to the adb shell.

   @param cmd the string to be escaped.
   @see shell_escape_command.
   @todo check/simplify escaping.
 */
void adb_shell_escape_command(string& cmd)
{
    string_replacer(cmd,"\\","\\\\");
    string_replacer(cmd,"(","\\(");
    string_replacer(cmd,")","\\)");
    string_replacer(cmd,"'","\\'");
    string_replacer(cmd,"`","\\`");
    string_replacer(cmd,"|","\\|");
    string_replacer(cmd,"&","\\&");
    string_replacer(cmd,";","\\;");
    string_replacer(cmd,"<","\\<");
    string_replacer(cmd,">","\\>");
    string_replacer(cmd,"*","\\*");
    string_replacer(cmd,"#","\\#");
    string_replacer(cmd,"%","\\%");
    string_replacer(cmd,"=","\\=");
    string_replacer(cmd,"~","\\~");
    string_replacer(cmd,"/[0;0m","");
    string_replacer(cmd,"/[1;32m","");
    string_replacer(cmd,"/[1;34m","");
    string_replacer(cmd,"/[1;36m","");
}

/**
   Modify, in place, the given path string by escaping special characters.

   @param path the string to modify.
   @see shell_escape_command.
   @todo check/simplify escaping.
 */
void shell_escape_path(string &path)
{
  string_replacer(path, " ", "\\ ");
}

/**
   Recursively delete /tmp/adbfs on the local host and then recreate
   it with 0755 permissions flags.
   @todo Should probably use mkstemp or friends.
 */
void clearTmpDir(){
    shell("rm -rf /tmp/adbfs");
    mkdir("/tmp/adbfs/",0755);
}

/////// getattr


/**
   adbFS implementation of FUSE interface function fuse_operations.getattr.
   @todo check shell escaping.
 */
static int adb_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (i_adb_stat(path, stbuf)) {
        return -ENOENT;
    }

    return 0;
}

/////// readdir

typedef struct readdir_cb_info {
    void* buf;
    fuse_fill_dir_t filler;
} readdir_cb_info;

static void readdir_cb(char const* name, struct stat const* st, void* cookie)
{
    readdir_cb_info* rcbi = (readdir_cb_info*)cookie;
    rcbi->filler(rcbi->buf, name, st, 0);
}

/**s
   adbFS implementation of FUSE interface function fuse_operations.readdir.
   @todo check shell escaping.
 */
static int adb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    readdir_cb_info rcbi = {.buf = buf, .filler = filler };

    int res = i_adb_ls(path, readdir_cb, &rcbi);
    if (res <= 0)
    {
        return -ENOENT;
    }

    return 0;
}


/////// open

static int adb_open(const char *path, struct fuse_file_info *fi)
{
    string path_string;
    string local_path_string;
    path_string.assign(path);
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);
    cout << "-- " << path_string << " " << local_path_string << "\n";

    if (!fileTruncated[path_string]) {

        if (i_adb_stat(path, NULL)) {
            return -ENOENT;
        }

        path_string.assign(path);
        local_path_string.assign("/tmp/adbfs/");
        string_replacer(path_string,"/","-");
        local_path_string.append(path_string);
        shell_escape_path(local_path_string);
        path_string.assign(path);

        i_adb_pull(path_string.c_str(), local_path_string.c_str());
    }
    else {
        fileTruncated[path_string] = false;
    }

    fi->fh = open(local_path_string.c_str(), fi->flags);

    return 0;
}

static int adb_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
    int fd;
    int res;
    fd = fi->fh; //open(local_path_string.c_str(), O_RDWR);
    if(fd == -1)
        return -errno;
    res = pread(fd, buf, size, offset);
    //close(fd);
    if(res == -1)
        res = -errno;

    return size;
}

static int adb_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    //local_path_string.assign("/tmp/adbfs/");
    //local_path_string.append(path_string);
    shell_escape_path(local_path_string);

    int fd = fi->fh; //open(local_path_string.c_str(), O_CREAT|O_RDWR|O_TRUNC);

    filePendingWrite[fd] = true;

    int res = pwrite(fd, buf, size, offset);
    close(fd);

    i_adb_push(local_path_string.c_str(), path_string.c_str());
    adb_shell("sync");
    if (res == -1)
        res = -errno;
    return res;
}


static int adb_flush(const char *path, struct fuse_file_info *fi) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);
    int flags = fi->flags;
    int fd = fi->fh;
    cout << "flag is: "<< flags <<"\n";
    if (filePendingWrite[fd]) {
        filePendingWrite[fd] = false;
        i_adb_push(local_path_string.c_str(), path_string.c_str());
        adb_shell("sync");
    }
    return 0;
}

static int adb_release(const char *path, struct fuse_file_info *fi) {
    int fd = fi->fh;
    filePendingWrite.erase(filePendingWrite.find(fd));
    close(fd);
    return 0;
}

static int adb_access(const char *path, int mask) {
    //###cout << "###access[path=" << path << "]" <<  endl;
    return 0;
}

static int adb_utimens(const char *path, const struct timespec ts[2]) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);

    queue<string> output;
    string command = "touch \"";
    command.append(path_string);
    command.append("\"");
    cout << command<<"\n";
    adb_shell(command);

    return 0;
}

static int adb_truncate(const char *path, off_t size) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);

    queue<string> output;
    string command = "stat -t \"";
    command.append(path_string);
    command.append("\"");
    cout << command<<"\n";
    output = adb_shell(command);
    vector<string> output_chunk = make_array(output.front());

    if (i_adb_stat(path, NULL)) {
        i_adb_pull(path_string.c_str(), local_path_string.c_str());
    }

    fileTruncated[path_string] = true;

    cout << "truncate[path=" << local_path_string << "][size=" << size << "]" << endl;

    return truncate(local_path_string.c_str(),size);
}

static int adb_mknod(const char *path, mode_t mode, dev_t rdev) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);

    cout << "mknod for " << local_path_string << "\n";
    mknod(local_path_string.c_str(),mode, rdev);
    i_adb_push(local_path_string.c_str(), path_string.c_str());
    adb_shell("sync");

    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;

    return 0;
}

static int adb_mkdir(const char *path, mode_t mode) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);
    string command;
    command.assign("mkdir '");
    command.append(path_string);
    command.append("'");
    adb_shell(command);
    return 0;
}

static int adb_rename(const char *from, const char *to) {
    string local_from_string,local_to_string ="/tmp/adbfs/";

    local_from_string.append(from);
    local_to_string.append(to);
    string command = "mv '";
    command.append(from);
    command.append("' '");
    command.append(to);
    command.append("'");
    cout << "Renaming " << from << " to " << to <<"\n";
    adb_shell(command);
    return 0;
}

static int adb_rmdir(const char *path) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);

    string command = "rmdir '";
    command.append(path_string);
    command.append("'");
    adb_shell(command);

    //rmdir(local_path_string.c_str());
    return 0;
}

static int adb_unlink(const char *path) {
    string path_string;
    string local_path_string;
    path_string.assign(path);
    fileData[path_string].timestamp = fileData[path_string].timestamp + 50;
    local_path_string.assign("/tmp/adbfs/");
    string_replacer(path_string,"/","-");
    local_path_string.append(path_string);
    path_string.assign(path);

    string command = "rm '";
    command.append(path_string);
    command.append("'");
    adb_shell(command);

    unlink(local_path_string.c_str());
    return 0;
}

static int adb_readlink(const char *path, char *buf, size_t size)
{
    string path_string(path);
    string_replacer(path_string,"'","\\'");
    queue<string> output;
    string command = "ls -l --color=none \"";
    command.append(path_string);
    command.append("\"");
    output = adb_shell(command);
    if(output.empty())
       return -EINVAL;
    string res = output.front();
    size_t pos = res.find(" -> ");
    if(pos == string::npos)
       return -EINVAL;
    pos+=4;
    while(res[pos] == '/')
       ++pos;
    size_t my_size = res.size() - pos;
    if(my_size >= size)
       return -ENOSYS;
    //cout << res << endl << pStart <<endl;
    memcpy(buf, res.c_str() + pos, my_size+1);
    return 0;
}

/**
   Main struct for FUSE interface.
 */
static struct fuse_operations adbfs_oper;

/**
   Set up the fuse_operations struct adbfs_oper using above adb_*
   functions and then call fuse_main to manage things.

   @see fuse_main in fuse.h.
 */
int main(int argc, char *argv[])
{
    clearTmpDir();
    memset(&adbfs_oper, sizeof(adbfs_oper), 0);
    adbfs_oper.readdir= adb_readdir;
    adbfs_oper.getattr= adb_getattr;
    adbfs_oper.access= adb_access;
    adbfs_oper.open= adb_open;
    adbfs_oper.flush = adb_flush;
    adbfs_oper.release = adb_release;
    adbfs_oper.read= adb_read;
    adbfs_oper.write = adb_write;
    adbfs_oper.utimens = adb_utimens;
    adbfs_oper.truncate = adb_truncate;
    adbfs_oper.mknod = adb_mknod;
    adbfs_oper.mkdir = adb_mkdir;
    adbfs_oper.rename = adb_rename;
    adbfs_oper.rmdir = adb_rmdir;
    adbfs_oper.unlink = adb_unlink;
    adbfs_oper.readlink = adb_readlink;
    return fuse_main(argc, argv, &adbfs_oper, NULL);
}
