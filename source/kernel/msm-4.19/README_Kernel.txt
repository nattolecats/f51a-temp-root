#----------------------------------------------------------------------------
# (C) 2021 FCNT LIMITED
#----------------------------------------------------------------------------
1. How to Build
	(1)Toolchain
		Please download from codeaurora and etc.
        https://www.codeaurora.org/

	(2)defconfig merge
		$ cd kernel
		$ export KERNEL_CFG_MERGE_PARM1="vendor/auto-gen-perf_defconfig"
		$ export KERNEL_CFG_MERGE_PARM2="./arch/arm64/configs"
		$ export KERNEL_CFG_MERGE_PARM3="vendor/kona-perf_defconfig"
		$ export KERNEL_CFG_MERGE_PARM4="vendor/fjdev-perf_defconfig"
		$ ./scripts/fj_config_merge.sh

	(3)make
		$ cd kernel
		$ make O=<out folder path> ARCH=arm64 CROSS_COMPILE=<cross compiler path> auto-gen-perf_defconfig
          Ex) CROSS_COMPILE=/bld/toolchains/aarch64-linux-android-4.9/bin/aarch64-linux-android-
		$ cd <out folder path>
		$ make ARCH=arm64 CROSS_COMPILE=<cross compiler path>

2. How to Clean
	$ make clean

