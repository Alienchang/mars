#!/usr/bin/env python

import shutil
import os
import sys
import glob

from mars_utils import *

NDK_BUILD_CMD = "ndk-build NDK_DEBUG=0 -j -B SDK=0 LIBPREFIX=mars -C "
BUILD_XLOG_PATHS = ("openssl", "comm", "log")
COPY_XLOG_FILES = {"../log/crypt/log_crypt.h": "jni/log_crypt.h",
                "../log/crypt/decode_mars_log_file.py": "jni/decode_mars_log_file.py.rewriteme",
                "../mk_template/JNI_OnLoad.cpp": "jni/JNI_OnLoad.cc"
                }

BUILD_MARS_PATHS = ("openssl", "comm", "baseevent", "log", "app", "sdt", "stn")

COPY_MARS_FILES = {"../stn/proto/longlink_packer.h": "jni/longlink_packer.h",
               "../stn/proto/shortlink_packer.h": "jni/shortlink_packer.h",
                "../stn/proto/longlink_packer.cc": "jni/longlink_packer.cc.rewriteme",
                "../stn/proto/shortlink_packer.cc": "jni/shortlink_packer.cc.rewriteme",
                "../log/crypt/log_crypt.h": "jni/log_crypt.h",
                "../log/crypt/log_crypt.cc": "jni/log_crypt.cc.rewriteme",
                "../log/crypt/decode_mars_log_file.py": "jni/decode_mars_log_file.py.rewriteme",
                "../mk_template/JNI_OnLoad.cpp": "jni/JNI_OnLoad.cc"
                }


def build_android_xlog_static_libs(_path="mars_xlog_sdk"):
    libs_save_path = _path + "/mars_libs"
    src_save_path = _path + "/"


    shutil.rmtree(libs_save_path, True)
    for i in range(0, len(BUILD_XLOG_PATHS)):
        shutil.rmtree("../" + BUILD_XLOG_PATHS[i] + "/mars_libs", True)
        shutil.rmtree("../" + BUILD_XLOG_PATHS[i] + "/obj", True)

    gen_revision_file(libs_save_path, libs_save_path)

    for i in range(0, len(BUILD_XLOG_PATHS)):
        if not os.path.exists("../" + BUILD_XLOG_PATHS[i] + "/jni"):
            continue
        if 0 != os.system(NDK_BUILD_CMD + "../" + BUILD_XLOG_PATHS[i]):
            return -1


    for i in range(len(BUILD_XLOG_PATHS)-1, -1, -1):
        if not os.path.exists("../" + BUILD_XLOG_PATHS[i] + "/jni"):
            continue
        
        files = os.listdir("../" + BUILD_XLOG_PATHS[i] + "/libs")
        for f in files:
            if os.path.isfile(f):
                continue

            cpu_libs = os.path.join(libs_save_path, f)
            cpu_symbols = os.path.join(cpu_libs, "symbols")
            print cpu_libs, cpu_symbols
            if not os.path.exists(cpu_libs):
                os.makedirs(cpu_libs)
            if not os.path.exists(cpu_symbols):
                os.makedirs(cpu_symbols)

            for lib in glob.glob("../" + BUILD_XLOG_PATHS[i] + "/libs/" + f + "/*.so"):
                shutil.copy(lib, cpu_libs)
            for lib in glob.glob("../" + BUILD_XLOG_PATHS[i] + "/obj/local/" + f + "/*.a"):
                shutil.copy(lib, cpu_libs)
            
            for lib in glob.glob("../" + BUILD_XLOG_PATHS[i] + "/obj/local/" + f + "/*.so"):
                shutil.copy(lib, cpu_symbols)



    for (src, dst) in COPY_XLOG_FILES.items():
        dst_path = src_save_path + dst[:dst.rfind("/")]
        if not os.path.exists(dst_path):
            os.makedirs(dst_path)
        if os.path.isfile(src):
            shutil.copy(src, src_save_path + dst)

    print("build succeed!")
    return 0

def build_android_xlog_shared_libs(_path="mars_xlog_sdk"):

    if 0 != build_android_xlog_static_libs("mars_xlog_sdk"):
        print("build static libs fail!!!")
        return -1


    shutil.rmtree(_path + "/libs", True)
    shutil.rmtree(_path + "/obj", True)


    if 0 != os.system(NDK_BUILD_CMD + _path):
        print("build fail!!!")
        return -1

    print("build succeed!")
    return 0


def build_android_mars_static_libs(_path="mars_android_sdk", _arch=""):
    libs_save_path = _path + "/mars_libs"
    src_save_path = _path + "/"
    
    if _arch != "":
        global NDK_BUILD_CMD
        NDK_BUILD_CMD = "ndk-build _ARCH_=" + _arch + " NDK_DEBUG=0 -j -B SDK=0 LIBPREFIX=mars -C "
        print(NDK_BUILD_CMD)

    shutil.rmtree(libs_save_path, True)
    for i in range(0, len(BUILD_MARS_PATHS)):
        shutil.rmtree("../" + BUILD_MARS_PATHS[i] + "/mars_libs", True)
        shutil.rmtree("../" + BUILD_MARS_PATHS[i] + "/obj", True)

    gen_revision_file(libs_save_path, sys.argv[2] if 3 <= len(sys.argv) else "default")

    for i in range(0, len(BUILD_MARS_PATHS)):
        if not os.path.exists("../" + BUILD_MARS_PATHS[i] + "/jni"):
            continue
        if 0 != os.system(NDK_BUILD_CMD + "../" + BUILD_MARS_PATHS[i]):
            return -1


    for i in range(len(BUILD_MARS_PATHS)-1, -1, -1):
        if not os.path.exists("../" + BUILD_MARS_PATHS[i] + "/jni"):
            continue
        
        files = os.listdir("../" + BUILD_MARS_PATHS[i] + "/libs")
        for f in files:
            if os.path.isfile(f):
                continue

            cpu_libs = os.path.join(libs_save_path, f)
            cpu_symbols = os.path.join(cpu_libs, "symbols")
            if not os.path.exists(cpu_libs):
                os.makedirs(cpu_libs)
            if not os.path.exists(cpu_symbols):
                os.makedirs(cpu_symbols)

            for lib in glob.glob("../" + BUILD_MARS_PATHS[i] + "/libs/" + f + "/*.so"):
                if os.path.isfile(lib):
                    shutil.copy(lib, cpu_libs)
            for lib in glob.glob("../" + BUILD_MARS_PATHS[i] + "/obj/local/" + f + "/*.a"):
                if os.path.isfile(lib):
                    shutil.copy(lib, cpu_libs)
            
            for lib in glob.glob("../" + BUILD_MARS_PATHS[i] + "/obj/local/" + f + "/*.so"):
                if os.path.isfile(lib):
                    shutil.copy(lib, cpu_symbols)



    for (src, dst) in COPY_MARS_FILES.items():
        dst_path = src_save_path + dst[:dst.rfind("/")]
        if not os.path.exists(dst_path):
            os.makedirs(dst_path)
        if os.path.isfile(src):
            shutil.copy(src, src_save_path + dst)

    print("build succeed!")
    return 0

def build_android_mars_shared_libs(_path="mars_android_sdk"):

    if 0 != build_android_mars_static_libs():
        print("build static libs fail!!!")
        return -1


    shutil.rmtree(_path + "/libs", True)
    shutil.rmtree(_path + "/obj", True)


    if 0 != os.system(NDK_BUILD_CMD + _path):
        print("build fail!!!")
        return -1

    print("build succeed!")
    return 0


    
def main():
    if not check_env():
        return

    while True:
        if len(sys.argv) >=2 and len(sys.argv[1])==1 and sys.argv[1] >="1" and sys.argv[1] <="5":
            num = sys.argv[1]
            platforms = ['x86', 'x86_64', 'armeabi', 'arm64-v8a', 'armeabi-v7a', 'mips', 'mips64']
            if len(sys.argv) >=3 and sys.argv[2] in platforms:
                global NDK_BUILD_CMD
                NDK_BUILD_CMD = "ndk-build _ARCH_=" + sys.argv[2] + " NDK_DEBUG=0 -j -B SDK=0 LIBPREFIX=mars -C "
                print(NDK_BUILD_CMD)
        else:
            num = raw_input("Enter menu:\n1. build mars static libs.\n2. build mars shared libs.\n3. build xlog static libs.\n4. build xlog shared libs.\n5. exit.\n")

        if "1" == num:
            return build_android_mars_static_libs()
        elif "2" == num:
            return build_android_mars_shared_libs()
        elif "3" == num:
            return build_android_xlog_static_libs()
        elif "4" == num:
            return build_android_xlog_shared_libs()
        elif "5" ==num:
            return 0
        else:
            pass


if __name__ == "__main__":
    main()
