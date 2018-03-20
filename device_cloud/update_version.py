#!/usr/bin/env python

import argparse
import errno
import random
import signal
import sys
import os
from time import sleep
from datetime import datetime
import device_cloud as iot

version_file = "version_file"
attributes = "version-number"
authdata_file = "authdata_file"
test_file = "test"

def download_authdata(client) :
    os.system('rm -rf ' + authdata_file)
    os.system('rm -rf ' + authdata_file + '.zip')
    os.system('ls -al')
    client.file_download(file_name=authdata_file + '.zip',
                         download_dest=".",
                         blocking=True, timeout=300, file_global=True)
    os.system('unzip ' + authdata_file + '.zip')

def update_authdata(client):
    client.info ("begin update_authdata\n")
    
    if os.path.exists(version_file):
        fo = open (version_file, "r")
        local_version = fo.read()
        fo.close()
    else:
        local_version = "invalid"

    read_complete = 0
    while read_complete == 0:
        status, remote_version, timestamp = client.attribute_read_last_sample(attributes)
        if status == iot.STATUS_SUCCESS:
            read_complete = 1
        sleep(0.5)

    client.info("version compare: local_version %s remote_version %s timestamp %s\n"
        %( local_version, remote_version, timestamp))

    if remote_version == local_version:
        client.info ("version same\n")
        return False
    else:
        client.info ("version different, downloading auth file ...\n");
        download_authdata(client)
        fo = open (version_file, "w")
        fo.write(remote_version)
        fo.close()
        return True

    