#----------------------------------------------------------------------------
# COPYRIGHT(C) FCNT LMITED 2020
#----------------------------------------------------------------------------
#----------------------------------------------------------------------
# Kernel Make Env
#----------------------------------------------------------------------

ifeq ($(FJFEAT_AIRMONKEY), true)
TARGET_KERNEL_MAKE_ENV += FJFEAT_AIRMONKEY=1
endif

ifeq ($(LSM_OFF), 1)
TARGET_KERNEL_MAKE_ENV += LSM_OFF=1
endif

ifeq ($(FJ_PRODUCT_RELEASE), 1)
TARGET_KERNEL_MAKE_ENV += FJ_PRODUCT_RELEASE=1
endif

ifeq ($(FJSEC_BLDFLG), 1)
TARGET_KERNEL_MAKE_ENV += FJSEC_BLDFLG=1
endif

ifeq ($(DOCOMO_RELEASE), 1)
TARGET_KERNEL_MAKE_ENV += DOCOMO_RELEASE=1
endif

