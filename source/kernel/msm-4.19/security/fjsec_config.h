/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
/*
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
#include <linux/ptrace.h>
#include <crypto/sha.h>
//#include "../../fjfeat/FjFeatMac.h"
#include <linux/kdev_t.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>

#include <linux/msm_ion.h>

//#define DEBUG_COPY_GUARD
//#define DEBUG_ACCESS_CONTROL
//#define DEBUG_KITTING_ACCESS_CONTROL
//#define DEBUG_SETID_ACCESS_CONTROL

#define CURRENT_UID  (current->cred->uid.val % 100000)// for Multi User
#define AID_ROOT              0  /* traditional unix root user */
#define AID_SYSTEM         1000  /* system server */
#define AID_RADIO          1001  /* telephony subsystem, RIL */
#define AID_BLUETOOTH      1002  /* bluetooth subsystem */
#define AID_GRAPHICS       1003  /* graphics */
#define AID_AUDIO          1005  /* audio devices */
#define AID_CAMERA         1006
#define AID_WIFI           1010  /* wifi subsystem */
#define AID_MEDIA          1013
#define AID_VPN            1016  /* vpn system */
#define AID_KEYSTORE       1017  /* keystore */
#define AID_DRM            1019  /* drm server */
#define AID_GPS 1021             /* GPS daemon */
#define AID_MEDIA_RW 1023        /* internal media storage write access */
#define AID_NFC            1027  /* nfc subsystem */
#define AID_AUDIOSERVER   1041  /* audioserver process */
#define AID_DEBUGGERD     1045  /* debuggerd unprivileged user */
#define AID_MEDIA_CODEC   1046  /* mediacodec process */
#define AID_CAMERASERVER  1047  /* cameraserver process */
#define AID_SHELL         2000  /* adb and debug shell user */
#define AID_NET_ADMIN      3005
#define AID_CASDRM        4005
#define AID_LIFELOG       5050
#define AID_PREINST_FELC  5003
#define AID_GROUP_FELC    5003
#define AID_MMP           5034   /* fj mmp */
#define AID_LEGIBLE_FONT  5035
#define AID_CAMERA_APP    5021
#define AID_NOBODY         9999
#define PRNAME_NO_CHECK 0
#define UID_NO_CHECK -1
#define SUPL_NO_CHECK -1
#define SUPL_NO_HASHCHECK -2
#define SUPL_CHK_ALT_ADDR_IONDMA  -10000

#define NO_CHECK_STR "no_check"
#define ISEMPTY "isEmpty"

#define JAVA_APP_PROCESS_PATH32 "/system/bin/app_process32"
#define JAVA_APP_PROCESS_PATH64 "/system/bin/app_process64"
#define CHECK_COMMON		"*"
#define SYSTEM_PROCESS_PATH "/system/bin/"
#define VENDOR_PROCESS_PATH "/vendor/bin/"
#define HCE_PROCESS_PATH "/sbin/healthd"


#define SECURITY_FJSEC_RECOVERY_PROCESS_NAME "recovery"
#define SECURITY_FJSEC_RECOVERY_PROCESS_PATH "/sbin/recovery"

#define S2B(x) ((loff_t)x * SECTOR_SIZE)
#define ADDR2PFN(x) (x >> PAGE_SHIFT)

#define INIT_PROCESS_NAME "init"
#define INIT_PROCESS_PATH "/system/bin/init"

#define BOOT_ARGS_MODE_FOTA "fotamode"
#define BOOT_ARGS_MODE_OSUPDATE "osupdatemode"
#define BOOT_ARGS_MODE_RECOVERYMENU "recoverymenu"
#define BOOT_ARGS_MODE_MASTERCLEAR "masterclear"
#define BOOT_ARGS_MODE_CHARGER "charger"
#define BOOT_ARGS_MODE_REPAIR "repair"

#define OFFSET_BYTE_LIMIT LLONG_MAX

#ifdef DEBUG_ACCESS_CONTROL
#define TEST_PTN_PATH "/dev/block/mmcblk0p22"
#define TEST_MOUNT_POINT "/fota2/"
#define TEST_DIRS_PATH TEST_MOUNT_POINT "*"
#endif /* DEBUG_ACCESS_CONTROL */

#define SIGNAL_CATCHER_PROCESS_NAME "Signal Catcher"
#define CTS_BIONIC_PROCESS_NAME "CtsBionicTestCa"

/* for CTS */
#define CTS_TEST
/* for VTS */
#define VTS_TEST

enum boot_mode_type {
	BOOT_MODE_NONE = 0,
	BOOT_MODE_FOTA = 1,
	BOOT_MODE_SDDOWNLOADER = 2,
//	BOOT_MODE_RECOVERY = 3,
	BOOT_MODE_MASTERCLEAR = 3,
//	BOOT_MODE_MAKERCMD = 4,
	BOOT_MODE_OSUPDATE = 5,
	BOOT_MODE_CHARGER = 6,
};

#define ROOT_DIR "/"

enum result_mount_point {
	MATCH_SYSTEM_MOUNT_POINT,
	MATCH_VENDOR_MOUNT_POINT,
#ifdef CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE
	MATCH_SECURE_STORAGE_MOUNT_POINT,
#endif /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */
#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING
	MATCH_KITTING_MOUNT_POINT,
#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */
	MATCH_MOUNT_POINT,
	MATCH_ROOT_MOUNT_POINT,
	MISMATCH_MOUNT_POINT,
	UNRELATED_MOUNT_POINT,
};

//#define LSM_FLG_OFF
//#define LSM_PAGEACL_AUTOREGIST
#define LSM_PAGEACL_DUMPSTACK
//#define LSM_PAGEACL_PERFDBG
//#define LSM_PAGEACL_SSRDBG



#define BOOT_ARGS_FJLSM_ENFORCING   "enforcing"
#define BOOT_ARGS_FJLSM_PERMISSIVE  "permissive"
#define BOOT_ARGS_FJLSM_OFF         "off"

#define APNV_SECURITY_LSM_FLAG_I	49000 
#define FJSEC_LSM_DISABLED			0x00
#define FJSEC_LSM_ENABLED			0x01
#define FJSEC_LSM_PERMISSIVE		0x07	// 0x07  LSM ON(Permissive)

#define FJSEC_LSM_NV_BEFORE			0x00
#define FJSEC_LSM_NV_AFTER			0x01


#define INIT_PID 1
#define SYSROOT_DEV_PATH "/dev/root"

/**
 * Limitation of device file.
 */
struct fs_path_config {
	mode_t		mode;
	uid_t		uid;
	gid_t		gid;
	dev_t		rdev;
	const char	*prefix;
	// *NEW* add the mount point info to check the path
	const char *mnt_pnt;
};
static struct fs_path_config devs[] = {
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 0), "/dev/block/sda", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 1), "/dev/block/sda1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 2), "/dev/block/sda2", "/mnt/vendor/persist" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 3), "/dev/block/sda3", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 4), "/dev/block/sda4", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 5), "/dev/block/sda5", "/misc" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 6), "/dev/block/sda6", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_SYSTEM, AID_SYSTEM, MKDEV(8, 7), "/dev/block/sda7", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 8), "/dev/block/sda8", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 9), "/dev/block/sda9", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 10), "/dev/block/sda10", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 11), "/dev/block/sda11", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 12), "/dev/block/sda12", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 13), "/dev/block/sda13", "/metadata" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 14), "/dev/block/sda14", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 15), "/dev/block/sda15", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 0), "/dev/block/sda16", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 1), "/dev/block/sda17", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 2), "/dev/block/sda18", "/mnt/makerlog" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 3), "/dev/block/sda19", "/mnt/persist2" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 4), "/dev/block/sda20", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 5), "/dev/block/sda21", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 6), "/dev/block/sda22", "/data" },

    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 16), "/dev/block/sdb", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 17), "/dev/block/sdb1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 18), "/dev/block/sdb2", NULL },

    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 32), "/dev/block/sdc", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 33), "/dev/block/sdc1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 34), "/dev/block/sdc2", NULL },

    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 48), "/dev/block/sdd", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 49), "/dev/block/sdd1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 50), "/dev/block/sdd2", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 51), "/dev/block/sdd3", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 52), "/dev/block/sdd4", NULL },

    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 64), "/dev/block/sde", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 65), "/dev/block/sde1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 66), "/dev/block/sde2", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 67), "/dev/block/sde3", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 68), "/dev/block/sde4", "/vendor/firmware_mnt" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 69), "/dev/block/sde5", "/vendor/bt_firmware" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 70), "/dev/block/sde6", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 71), "/dev/block/sde7", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 72), "/dev/block/sde8", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 73), "/dev/block/sde9", "/vendor/dsp" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 74), "/dev/block/sde10", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 75), "/dev/block/sde11", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 76), "/dev/block/sde12", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 77), "/dev/block/sde13", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 78), "/dev/block/sde14", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 79), "/dev/block/sde15", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 7), "/dev/block/sde16", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 8), "/dev/block/sde17", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 9), "/dev/block/sde18", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 10), "/dev/block/sde19", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 11), "/dev/block/sde20", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 12), "/dev/block/sde21", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 13), "/dev/block/sde22", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 14), "/dev/block/sde23", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 15), "/dev/block/sde24", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 16), "/dev/block/sde25", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 17), "/dev/block/sde26", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 18), "/dev/block/sde27", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 19), "/dev/block/sde28", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 20), "/dev/block/sde29", "/vendor/firmware_mnt" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 21), "/dev/block/sde30", "/vendor/bt_firmware" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 22), "/dev/block/sde31", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 23), "/dev/block/sde32", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 24), "/dev/block/sde33", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 25), "/dev/block/sde34", "/vendor/dsp" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 26), "/dev/block/sde35", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 27), "/dev/block/sde36", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 28), "/dev/block/sde37", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 29), "/dev/block/sde38", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 30), "/dev/block/sde39", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 31), "/dev/block/sde40", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 32), "/dev/block/sde41", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 33), "/dev/block/sde42", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 34), "/dev/block/sde43", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 35), "/dev/block/sde44", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 36), "/dev/block/sde45", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 37), "/dev/block/sde46", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 38), "/dev/block/sde47", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 39), "/dev/block/sde48", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 40), "/dev/block/sde49", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 41), "/dev/block/sde50", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 42), "/dev/block/sde51", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 43), "/dev/block/sde52", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 44), "/dev/block/sde53", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 45), "/dev/block/sde54", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 46), "/dev/block/sde55", "/mnt/vendor/spunvm" },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 47), "/dev/block/sde56", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 48), "/dev/block/sde57", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 49), "/dev/block/sde58", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 50), "/dev/block/sde59", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 51), "/dev/block/sde60", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 52), "/dev/block/sde61", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 53), "/dev/block/sde62", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 54), "/dev/block/sde63", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(259, 55), "/dev/block/sde64", NULL },

    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 80), "/dev/block/sdf", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 81), "/dev/block/sdf1", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 82), "/dev/block/sdf2", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 83), "/dev/block/sdf3", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 84), "/dev/block/sdf4", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 85), "/dev/block/sdf5", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 86), "/dev/block/sdf6", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 87), "/dev/block/sdf7", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 88), "/dev/block/sdf8", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 89), "/dev/block/sdf9", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 90), "/dev/block/sdf10", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 91), "/dev/block/sdf11", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 92), "/dev/block/sdf12", NULL },
    { (S_IRUSR | S_IWUSR | S_IFBLK), AID_ROOT, AID_ROOT, MKDEV(8, 93), "/dev/block/sdf13", "/mnt/vendor/sst" },

    { (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IFCHR), AID_NFC, AID_NFC, MKDEV(10, 254), "/dev/cxd224x-i2c", NULL },
    { 0, 0, 0, 0, 0, 0 },
};


struct read_write_access_control_process {
	char *process_name;
	char *process_path;
	uid_t uid;
};

static struct read_write_access_control_process vdsp_process_list[] = {
	{ "wild", "/system/bin/wild", UID_NO_CHECK },
	{ "ueventd", INIT_PROCESS_NAME, UID_NO_CHECK },
	{ 0, 0, 0 },
};

static struct read_write_access_control_process geomag_process_list_bin[] = {
    { "sensors_server", "/system/bin/sensors_server", AID_ROOT },
    { 0, 0, 0 },
};

static struct read_write_access_control_process adsp_process_list_bin[] = {
	{ "init", "/init", AID_ROOT },
	{ "ueventd", "/init", AID_ROOT },
	{ 0, 0, 0 },
};

static struct read_write_access_control_process preinrestore_process_list_bin[] = {
    { "init_preinresto", "/vendor/bin/init_preinrestore.sh", AID_ROOT },
    { "init_preinresto", "/vendor/bin/init_preinrestoreOSV.sh", AID_SHELL },
    { "init_preinresto", "/system/bin/init_preinrestore.sh", AID_ROOT },
    { "init_preinresto", "/system/bin/init_preinrestoreOSV.sh", AID_SHELL },
    { "init_preinresto", "/product/bin/init_preinrestore.sh", AID_ROOT },
    { "init_preinresto", "/product/bin/init_preinrestoreOSV.sh", AID_ROOT },
    { "init_preinresto", "/system/bin/sh", AID_ROOT },
    { "init_preinresto", "/system/bin/sh", AID_SHELL },
    { "main", JAVA_APP_PROCESS_PATH32, AID_SHELL },
    { "main", JAVA_APP_PROCESS_PATH64, AID_SHELL },
    { "main", JAVA_APP_PROCESS_PATH32, AID_ROOT },
    { "main", JAVA_APP_PROCESS_PATH64, AID_ROOT },
    { INIT_PROCESS_NAME, INIT_PROCESS_PATH, AID_ROOT },
    { "touch", "/system/bin/toybox", AID_ROOT },
    { "touch", "/system/bin/toybox", AID_SHELL },
    { 0, 0, 0 },
};

static struct read_write_access_control_process nfc_process_list_lib[] = {
    { 0, 0, 0 },
};

static struct read_write_access_control_process mmp_process_list_lib[] = {
    { PRNAME_NO_CHECK, "/system/bin/fjmmpd", AID_MMP }, // temporary fix
    { PRNAME_NO_CHECK, "/vendor/bin/fjmmpd", AID_MMP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_MMP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_MMP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_CAMERA_APP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_CAMERA_APP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_LEGIBLE_FONT },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_LEGIBLE_FONT },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { 0, 0, 0 },
};
static struct read_write_access_control_process mmp2_process_list_lib[] = {
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_MMP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_MMP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_CAMERA_APP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_CAMERA_APP },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
    { PRNAME_NO_CHECK, JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { 0, 0, 0 },
};

static struct read_write_access_control_process hdcp2_process_list[] = {
    { "qc_prov_hdcp2", "/vendor/bin/qc_prov_hdcp2", AID_SYSTEM },
    { 0, 0, 0 },
};

static struct read_write_access_control_process felica_sec_process_list[] = {
    { "sec_service_tz", "/vendor/bin/sec_service_tz", AID_RADIO },
    { 0, 0, 0 },
};

static struct read_write_access_control_process dspg_process_list[] = {
    { "ueventd", "/init", AID_ROOT },
    { 0, 0, 0 },
};

static struct read_write_access_control_process sec_service_process_list[] = {
    { "sec_service", "/vendor/bin/sec_service", AID_RADIO },
    { 0, 0, 0 },
};

static struct read_write_access_control_process hdcp_sec_process_list[] = {
    { "hdcp_provd", "/vendor/bin/hdcp_provd", AID_SYSTEM },
    { 0, 0, 0 },
};

static struct read_write_access_control_process key_sec_process_list[] = {
    { "Keymasterkey_provd", "/system/bin/Keymasterkey_provd", AID_ROOT },
    { 0, 0, 0 },
};

static struct read_write_access_control_process mms_sec_process_list[] = {
    { "com.android.phone", JAVA_APP_PROCESS_PATH32, AID_RADIO },
    { "com.android.phone", JAVA_APP_PROCESS_PATH64, AID_RADIO },
    { "com.android.mms", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { "com.android.mms", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { ".clmail_dl", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { ".clmail_dl", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { "jp.co.nttdocomo.carriermail", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { "jp.co.nttdocomo.carriermail", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { "system_server", JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
    { "system_server", JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { "dex2oat", "/system/bin/dex2oat", UID_NO_CHECK },
	{ 0, 0, 0 },
};

static struct read_write_access_control_process frauddetect_process_list[] = {
    { "com.fujitsu.mobile_phone.fjfrauddetectservice", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { "com.fujitsu.mobile_phone.fjfrauddetectservice", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { 0, 0, 0 },
};

static struct read_write_access_control_process wv_process_list[] = {
    { INIT_PROCESS_NAME, INIT_PROCESS_PATH, AID_ROOT },
    { INIT_PROCESS_NAME, INIT_PROCESS_PATH, AID_SYSTEM },
    { "sh", "/system/bin/sh", AID_ROOT },
    { 0, 0, 0 },
};

static struct read_write_access_control_process pwd_process_list[] = {
    { "com.fujitsu.mobile_phone.passwordmanager", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { "com.fujitsu.mobile_phone.passwordmanager", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { "com.fujitsu.mobile_phone.passwordmanagerwrapper", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
    { "com.fujitsu.mobile_phone.passwordmanagerwrapper", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
    { 0, 0, 0 },
};


#define NO_STRICT	-1
#define CHK_STRICT	1
struct read_write_access_control {
	char *prefix;
	struct read_write_access_control_process *process_list;
	int f_strict;
};

static struct read_write_access_control rw_access_control_list[] = {
	{ "/vendor/lib/libMmpAir.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpAirLut.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpAirWrap.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpMsr.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpFrc.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpDir8.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_2.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_3.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_4.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel2_4.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel2_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel2_2.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel2_3.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelA_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelA_2.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelB_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_10_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_10_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_11_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_11_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_13_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_13_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpAutoScale.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpLpfsr.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpLpfsrWrap.so", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_14_0.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernelC_14_1.o", mmp_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpStl.so", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpStlWrap.so", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libMmpStlLut.so", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libpxDir8.so", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libpxDir8tile.so", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_2_1.o", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_2_2.o", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_2_3.o", mmp2_process_list_lib,CHK_STRICT },
	{ "/vendor/lib/libkernel14_2_4.o", mmp2_process_list_lib,CHK_STRICT },
	{ "/system/etc/firmware/vw.bin", vdsp_process_list,NO_STRICT },
	{ "/system/etc/firmware/vw_th.bin", vdsp_process_list,NO_STRICT },
	{ "/system/etc/firmware/vp_vw.bin", vdsp_process_list,NO_STRICT },
	{ "/system/etc/firmware/vp_vw_th.bin", vdsp_process_list,NO_STRICT },
	{ "/system/etc/firmware/2b.bin", vdsp_process_list,NO_STRICT },
	{ "/system/lib64/libnfc-nci-mc.so", nfc_process_list_lib,NO_STRICT },
	{ "/system/lib/libnfc-nci-mc.so", nfc_process_list_lib,NO_STRICT },
	{ "/system/lib64/nfc_nci.cxd22xx-mc.default", nfc_process_list_lib,NO_STRICT },
	{ "/system/lib/nfc_nci.cxd22xx-mc.default", nfc_process_list_lib,NO_STRICT },
	{ "/system/lib64/libgeomag.so", geomag_process_list_bin, NO_STRICT },
	{ "/data/master/*", preinrestore_process_list_bin, CHK_STRICT },
	{ "/vendor/firmware_mnt/image/adsp.b*", adsp_process_list_bin, NO_STRICT },
	{ "/vendor/firmware_mnt/image/adsp.mdt", adsp_process_list_bin, NO_STRICT },
	{ "/vendor/lib64/libqc_prov_hdcp2_impl.so", hdcp2_process_list, CHK_STRICT },
	{ "/vendor/lib/libssrvtest_tz.so", felica_sec_process_list, CHK_STRICT },
	{ "/vendor/lib/libHdcpProvisioning.so", hdcp_sec_process_list, CHK_STRICT },
	{ "/system/lib64/libkeymasterkey_provd.so", key_sec_process_list, CHK_STRICT },
	{ "/system/framework/com.fujitsu.mobile_phone.spamjudge.jar", mms_sec_process_list, CHK_STRICT },
	{ "/vendor/lib64/libssrvtest.so", sec_service_process_list, CHK_STRICT },
	{ "/system/lib64/libxcwrapper_android_license.so", frauddetect_process_list, CHK_STRICT },
	{ "/vendor/firmware/asrp_params_aecnr.bin", dspg_process_list, CHK_STRICT },
	{ "/vendor/firmware/asrp_params_nr.bin", dspg_process_list, CHK_STRICT },
	{ "/vendor/firmware/asrp_params_voice_call.bin", dspg_process_list, CHK_STRICT },
	{ "/vendor/firmware/dbmd2_va_ve_fw.bin", dspg_process_list, CHK_STRICT },
	{ "/vendor/bin/wv_service", wv_process_list, CHK_STRICT },
	{ "/vendor/lib/libpwdwrap.so", pwd_process_list, CHK_STRICT },
	{ "/vendor/lib64/libpwdwrap.so", pwd_process_list, CHK_STRICT },
	{ 0, 0, 0 },
};

struct ac_config {
	char *prefix;
	enum boot_mode_type boot_mode;
	char *process_name;
	char *process_path;
};


#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING
struct kitting_directory_access_process {
	char *process_name;
	char *process_path;
	uid_t uid;
};

static struct kitting_directory_access_process kitting_directory_access_process_list[] = {
	{ "com.android.settings", JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
	{ "com.android.settings", JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
	{ "com.android.defcontainer", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK },
	{ "com.android.defcontainer", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK },
	{ "com.fujitsu.mobile_phone.home", JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
	{ "com.fujitsu.mobile_phone.home", JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { "system_server", JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
    { "system_server", JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { "com.fujitsu.mobile_phone.kitting", JAVA_APP_PROCESS_PATH32, AID_SYSTEM },
    { "com.fujitsu.mobile_phone.kitting", JAVA_APP_PROCESS_PATH64, AID_SYSTEM },
    { "bootanimation", "/system/bin/bootanimation", AID_GRAPHICS },
    { "com.fujitsu.mobile_phone.kitting", "/system/bin/busybox", AID_SYSTEM },
    { "main", JAVA_APP_PROCESS_PATH32, AID_ROOT },
    { "main", JAVA_APP_PROCESS_PATH64, AID_ROOT },
#ifdef CTS_TEST
	{ "ernalstorageapp", JAVA_APP_PROCESS_PATH32, UID_NO_CHECK  },
	{ "ernalstorageapp", JAVA_APP_PROCESS_PATH64, UID_NO_CHECK  },
#endif  /* CTS_TEST */
#ifdef DEBUG_KITTING_ACCESS_CONTROL
	{ "com.android.settings", "/system/bin/busybox", AID_SYSTEM },
#endif  /* DEBUG_KITTING_ACCESS_CONTROL */
	{ 0, 0, 0 },
};
#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */

struct ac_config_setid{
	char *process_name;
	char *process_path;
};

static struct ac_config_setid setid_acl[] = {
	{ "init", "/init"},
	{ "ueventd", "/init"},
	{ "adbd", "/sbin/adbd"},
    { "adbd", "/apex/com.android.adbd/bin/adbd"},
#ifdef DEBUG_SETID_ACCESS_CONTROL
	{ "setuid_test", "/data/lsm-test/setuid_test"},
	{ "setuid_test_NG", "/data/lsm-test-bin/setuid_test_NG"},
#endif /* DEBUG_SETID_ACCESS_CONTROL */
#ifdef DEBUG_KITTING_ACCESS_CONTROL
	{ "busybox", "/data/busybox/busybox"},
	{ "ndroid.settings", "/data/busybox/busybox"},
#endif /* DEBUG_KITTING_ACCESS_CONTROL */
    { "rmt_storage", "/vendor/bin/rmt_storage"},
    { "tftp_server", "/vendor/bin/tftp_server"},
    { "qseecomd", "/vendor/bin/qseecomd"},
    { "time_daemon", "/vendor/bin/time_daemon"},
    { "loc_launcher", "/vendor/bin/loc_launcher"},
    { "netmgrd", "/vendor/bin/netmgrd"},
    { "sensors.qcom", "/vendor/bin/sensors.qcom"},
#ifdef CTS_TEST
    { "CtsBionicTestCa", "/data/local/tmp/CtsBionicTestCases"},
#endif  /* CTS_TEST */
    { "fjdumpstate", "/product/bin/fjdumpstate"},
    { "dpmd", "/product/bin/dpmd"},
    { "diag_mdlog_syst", "/system_ext/bin/diag_mdlog_system"},
    { "dpmd", "/system_ext/bin/dpmd"},
	{ 0, 0 },
};

#define FJSEC_ACP_NAME_MAX 64
#define FJSEC_ACP_PATH_MAX 128
#define PG_ACL_CON	0xFF
#define PG_ACL_NCON	0x00
#define PG_ACL_ONE	0x01
#define PG_ACL_NCONS	0x02


struct ac_config_page_ext{
	char process_name[FJSEC_ACP_NAME_MAX];
	char process_path[FJSEC_ACP_PATH_MAX];
	char process_hash[SHA256_DIGEST_SIZE];
	uid_t uid;
	long supl1;
	long supl2;
};


#ifdef CTS_TEST
static struct ac_config_page_ext page_parent_acl_ext[] = {
    { "android.jni.cts", JAVA_APP_PROCESS_PATH32, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    { "android.jni.cts", JAVA_APP_PROCESS_PATH64, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    { "android.os.cts", JAVA_APP_PROCESS_PATH32, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    { "android.os.cts", JAVA_APP_PROCESS_PATH64, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    { "mmon.deviceinfo", JAVA_APP_PROCESS_PATH32, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    { "mmon.deviceinfo", JAVA_APP_PROCESS_PATH64, "", UID_NO_CHECK, SUPL_NO_HASHCHECK, SUPL_NO_CHECK},
    {"0", "0", {0} },
};
#endif


