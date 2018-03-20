#!/usr/bin/env python

'''
    Copyright (c) 2016-2017 Wind River Systems, Inc.
    
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at:
    http://www.apache.org/licenses/LICENSE-2.0
    
    Unless required by applicable law or agreed to in writing, software  distributed
    under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
    OR CONDITIONS OF ANY KIND, either express or implied.
'''

"""
Simple app that demonstrates the telemetry APIs in the HDC Python library
"""

import argparse
import errno
import random
import signal
import sys
import os
import time
import serial
from time import sleep
from datetime import datetime
import update_version
import capture
import recognition
import face_recognition
from update_version import authdata_file


head, tail = os.path.split(os.path.dirname(os.path.realpath(__file__)))
sys.path.insert(0, head)

import device_cloud as iot

running = True
sending_telemetry = False

# Return status once the cloud responds
cloud_response = False

# Second intervals between telemetry
TELEMINTERVAL = 4

authdata_database = []
authdata_dataname = []

def sighandler(signum, frame):
    """
    Signal handler for exiting app
    """
    global running
    if signum == signal.SIGINT:
        print("Received SIGINT, stopping application...")
        running = False


def quit_me():
    """
    Quits application (callback)
    """
    global running
    running = False
    return (iot.STATUS_SUCCESS, "")

def permit():
    """
    Quits application (callback)
    """
    client.info ('permit ... ')
    return (iot.STATUS_SUCCESS, "")

def update_authdata_base():
    files= os.listdir('./' + authdata_file)
 
    for file in files:
        full_file = './' + authdata_file + '/' + file
        client.info ('checking ' + full_file);
        auth_picture = face_recognition.load_image_file(full_file)
        auth_encoding = face_recognition.face_encodings(auth_picture)[0]
        authdata_database.append(auth_encoding)
        authdata_dataname.append(os.path.splitext(file)[0])
    return True
    
if __name__ == "__main__":
    signal.signal(signal.SIGINT, sighandler)

    # Initialize client default called 'python-demo-app'
    app_id = "Kongfu"
    client = iot.Client(app_id)

    # Use the demo-connect.cfg file inside the config directory
    # (Default would be python-demo-app-connect.cfg)
    config_file = "Kongfu.cfg"
    client.config.config_file = config_file

    # Look for device_id and demo-connect.cfg in this directory
    # (This is already default behaviour)
    config_dir = "."
    client.config.config_dir = config_dir

    # Finish configuration and initialize client
    client.initialize()
    client.log_level("INFO")

    # Set action callbacks
    client.action_register_callback("quit_me", quit_me)
    client.action_register_callback("permit", permit)

    # Connect to Cloud
    if client.connect(timeout=10) != iot.STATUS_SUCCESS:
        client.error("Failed")
        sys.exit(1)

    update_version.update_authdata(client)
    update_authdata_base()

    ser = serial.Serial('/dev/ttyUSB1', 115200, timeout=10)

    while running == True and client.is_alive():
        # Wrap sleep with an exception handler to fix SIGINT handling on Windows
        try:
            sleep (1)
        except IOError as err:
            if err.errno != errno.EINTR:
                raise

        s = ser.read(1)
        client.info ('get ' + s)
        if s != 'D' :
            if s == '' :
                client.info ('no signal ...')
            else :
                client.info ('get wrong signal, continue...')
            continue        

        capture.capture_image()

        reco_ret = recognition.recognition_person(client, authdata_database, authdata_dataname)
        if "unknown" == reco_ret:
            ser.write('F\n')
            client.info ("warning, unknown person\n");
            
            client.info ('uploading file ... ')
            file_name = os.path.abspath('.') + '/' + \
                        capture.unknown_path + '/' + capture.image_name
            upload_file_prefix = time.strftime("%Y_%m_%d_%H_%M_%S");
            upload_file_name = upload_file_prefix + '.jpg'
            client.info (file_name + ' ' + upload_file_name)
            result = client.file_upload(\
                            file_name, \
                            upload_name=upload_file_name, \
                            blocking=True, timeout=240, \
                            file_global=False)
            client.event_publish("warning, unknown person " + upload_file_prefix)
            client.attribute_publish("sec_record", "warning, unknown person " + upload_file_prefix)
            client.alarm_publish("unknown-person", 0)
 
            if result == iot.STATUS_SUCCESS:
                client.info (upload_file_name + "upload success\n");
            else:
                client.info (upload_file_name + "upload fail\n");
        elif "noone" == reco_ret:
            ser.write('F\n')
            client.alarm_publish("unknown-person", 1)
        else:
            ser.write('T\n')
            client.info ('hello, welcome %s\n', reco_ret);
            client.event_publish("hello, welcome " + \
                                time.strftime("%Y_%m_%d_%H_%M_%S") + " " + reco_ret)
            client.attribute_publish("sec_record", "hello, welcome " + \
                                time.strftime("%Y_%m_%d_%H_%M_%S") + " " + reco_ret)
            client.alarm_publish("unknown-person", 1) 

    ser.close()
    client.disconnect(wait_for_replies=True)

