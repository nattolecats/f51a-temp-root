/*
* (C) 2021 FCNT LIMITED
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; version 2
* of the License.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/pstore.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static struct proc_dir_entry *proc_entry;

static int summary_log_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    return ret;
}

ssize_t summary_log_write(struct file *fp, const char __user *buf, size_t len, loff_t *lof)
{
    int tid = 0;
    char *tmp = NULL;
    char msg[1024];
    char *pos = NULL;
    int msglen = 0;
    const int max_len = (sizeof(msg) - sizeof(tid));
    tmp = vmalloc(len + 1);
    memset(tmp, 0x00, len + 1);
    memcpy(tmp, buf, len);
    sscanf(tmp, "%d,%s", &tid, msg);
    memset(msg, 0x00, sizeof(msg));
    pos = strchr(tmp, (int)','); // find first comma
    msglen = strlen(pos + 1);
    if (msglen >= max_len) msglen = max_len;
    if (pos) memcpy(msg, (pos + 1), msglen);
    printk(KERN_INFO "summary_log_write: tid:%d msg:%s\n", tid, msg);
    pstore_write_summary_msg(tid, msg);
    vfree(tmp);
    return strlen(buf);
}

static struct file_operations summary_op = {
    .open = summary_log_open,
    .write = summary_log_write,
};

static int __init init_summary_log_module( void )
{
    int ret = 0;

    proc_entry = proc_create("summary_log", 0666, NULL, &summary_op);
    if (proc_entry == NULL) {
        ret = -ENOMEM;
        printk(KERN_INFO "summary_log: Couldn't create proc entry\n");
    } else {
        printk(KERN_INFO "summary_log: Module loaded.\n");
    }
    return ret;
}
module_init( init_summary_log_module );
