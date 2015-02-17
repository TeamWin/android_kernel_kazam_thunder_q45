# Copyright Statement:
#
# This software/firmware and related documentation ("MediaTek Software") are
# protected under relevant copyright laws. The information contained herein
# is confidential and proprietary to MediaTek Inc. and/or its licensors.
# Without the prior written permission of MediaTek inc. and/or its licensors,
# any reproduction, modification, use or disclosure of MediaTek Software,
# and information contained herein, in whole or in part, shall be strictly prohibited.

# MediaTek Inc. (C) 2010. All rights reserved.
#
# BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
# THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
# RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
# AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
# NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
# SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
# SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
# THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
# THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
# CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
# SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
# STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
# CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
# AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
# OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
# MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
#
# The following software/firmware and/or related documentation ("MediaTek Software")
# have been modified by MediaTek Inc. All revisions are subject to any receiver's
# applicable license agreements with MediaTek Inc.

################################################################################

LOCAL_PATH := $(call my-dir)

################################################################################
#
################################################################################
include $(CLEAR_VARS)

#-----------------------------------------------------------
# version
MY_CUST_VERSION := $(shell date) rev:0.1.3

#-----------------------------------------------------------
#
MTKCAM_HAVE_LOCALTEST   := '0'  # test by local table if '1' ; otherwise custom table
#
ifeq "'1'" "$(strip $(MTKCAM_HAVE_LOCALTEST))"
    $(shell cd $(LOCAL_PATH); rm hal; ln -s config hal)
else
    $(call config-custom-folder,hal:hal)
endif
#
MY_CUST_FTABLE_PATH := hal/imgsensor hal/flashlight config/matv

#-----------------------------------------------------------
#
define my-all-config.ftbl-under
$(patsubst ./%,%, \
  $(shell cd $(LOCAL_PATH); \
          find $(1)  -maxdepth 1 \( -name "config.ftbl.*.h" \) -and -not -name ".*") \
 )
endef
#
# custom feature table file list
MY_CUST_FTABLE_FILE_LIST := $(call my-all-config.ftbl-under, $(MY_CUST_FTABLE_PATH))
#
# custom feature table all-in-one file
MY_CUST_FTABLE_FINAL_FILE := $(LOCAL_PATH)/custgen.config.ftbl.h
#
# generate all-in-one file
#$(shell echo '//this file is auto-generated; do not modify it!' > $(MY_CUST_FTABLE_FINAL_FILE) )
#$(shell echo '#define MY_CUST_VERSION "$(MY_CUST_VERSION)"' >> $(MY_CUST_FTABLE_FINAL_FILE) )
#$(shell echo '#define MY_CUST_FTABLE_FILE_LIST "$(MY_CUST_FTABLE_FILE_LIST)"' >> $(MY_CUST_FTABLE_FINAL_FILE) )
#
# write each custom feature table include path into all-in-one file.
#$(foreach file, $(MY_CUST_FTABLE_FILE_LIST), $(shell echo "#include <$(file)>" >> $(MY_CUST_FTABLE_FINAL_FILE)))
#
LOCAL_GENERATED_SOURCES += $(MY_CUST_FTABLE_FINAL_FILE)
$(MY_CUST_FTABLE_FINAL_FILE): $(addprefix $(LOCAL_PATH)/,$(MY_CUST_FTABLE_FILE_LIST))
	@echo '//this file is auto-generated; do not modify it!' > $@
	@echo '#define MY_CUST_VERSION "$(MY_CUST_VERSION)"' >> $@
	@echo '#define MY_CUST_FTABLE_FILE_LIST "$(MY_CUST_FTABLE_FILE_LIST)"' >> $@
	@for x in $(MY_CUST_FTABLE_FILE_LIST); do echo "#include <$$x>" >> $@; done
$(info "MY_CUST_FTABLE_FILE_LIST="$(MY_CUST_FTABLE_FILE_LIST))

#-----------------------------------------------------------
LOCAL_SRC_FILES += $(call all-c-cpp-files-under, .)

#-----------------------------------------------------------
LOCAL_C_INCLUDES += $(TOP)/$(MTK_ROOT)/hardware/camera/common/paramsmgr/inc
#
LOCAL_C_INCLUDES += $(TOP)/$(MTK_ROOT)/frameworks-ext/av/include
LOCAL_C_INCLUDES += $(TOP)/$(MTK_ROOT)/hardware/camera/inc
LOCAL_C_INCLUDES += $(TOP)/$(MTK_ROOT)/hardware/camera/inc/common
LOCAL_C_INCLUDES += $(MTK_PATH_CUSTOM)/hal/inc/aaa






#-----------------------------------------------------------
LOCAL_CFLAGS += -DCUSTOM_FLASHLIGHT_TYPE_$(CUSTOM_KERNEL_FLASHLIGHT)
$(info "CUSTOM_FLASHLIGHT_TYPE_$(CUSTOM_KERNEL_FLASHLIGHT)")

PLATFORM_VERSION_MAJOR := $(word 1,$(subst .,$(space),$(PLATFORM_VERSION)))
LOCAL_CFLAGS += -DPLATFORM_VERSION_MAJOR=$(PLATFORM_VERSION_MAJOR)

#-----------------------------------------------------------
LOCAL_WHOLE_STATIC_LIBRARIES +=
#
LOCAL_STATIC_LIBRARIES +=


#-----------------------------------------------------------
LOCAL_MODULE := libcam.paramsmgr.feature.custom

#-----------------------------------------------------------
include $(BUILD_STATIC_LIBRARY)

################################################################################
#
################################################################################
#include $(CLEAR_VARS)
#include $(call all-makefiles-under,$(LOCAL_PATH))
