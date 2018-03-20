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
Wind River Helix Device Cloud Python-based Device Manager. This module provides
multiple device management methods and attributes that allow for management of
a connected device.
"""

from collections import namedtuple
import errno
import json
import os
from os.path import abspath
import pkg_resources
import platform
import signal
import sys
from time import sleep
import uuid
import argparse
import tarfile
import socket

from device_cloud import osal
from device_cloud import ota_handler
from device_cloud import relay
import device_cloud as iot

running = True

# whether to reset with systemd or not
systemd_controlled = False

default_cfg_dir = "."
app_id = "device_manager_py"
config_file = "iot-connect.cfg"

def sighandler(signum, frame):
    """
    Signal handler for exiting app.
    """
    global running
    print("Received signal {}, stopping application...".format(signum))
    running = False

def ack_messages(client, path):
    """
    Check for a pending message to acknowledge and, if present, send the mailbox
    acknowledgement to the cloud.
    """
    if os.path.isfile(path):
        with open(path, 'r') as id_file:
            msg_id = id_file.read()
            if msg_id:
                client.action_acknowledge(msg_id)

        os.remove(path)


def action_register_conditional(client, name, callback, enabled, \
                                user_data=None):
    """
    Register an action with the HDC client if it is enabled, otherwise register
    it using a generic "not implemented" callback.
    """
    return client.action_register_callback(name, callback \
            if enabled else method_not_implemented, user_data)

def agent_reset(client, params, user_data, request):
    """
    Callback function for the "reset_agent" method. Will stop and restart the
    device manager app.
    """
    global running
    running = False

    path = os.path.join(user_data[0], "message_ids")
    with open(path, 'w') as id_file:
        id_file.write(request.request_id)

    user_data[1].join()
    client.disconnect(wait_for_replies=True)
    if systemd_controlled != False:
        osal.execl("systemctl", "restart device-manager")
    else:
        app_id, default_cfg_dir, config_file = user_data[2]
        osal.execl("python", "device_manager.py", "-c"+default_cfg_dir,
                                        "-f"+config_file, "-i"+app_id)

    # If this return is hit, then the device manager did not restart properly
    return (iot.STATUS_FAILURE, "Device Manager Failed to Restart!")

def config_load(cfg_dir=default_cfg_dir, cfg_name="iot.cfg"):
    """
    Open and read configuration information from iot.cfg
    """
    config_data = None
    try:
        with open(os.path.join(cfg_dir, cfg_name), 'r') as cfg_file:
            config_data = json.load(cfg_file, \
                object_hook=lambda d: namedtuple('X', d.keys())(*d.values()))
    except IOError as error:
        print("Error parsing JSON from iot.cfg")
        print(error)
    return config_data

def device_decommission(client, params, user_data):
    """
    Callback for the "decommission_device" method that will remove HDC config
    files, preventing the device from reconnecting to the cloud. The device
    manager app will then shut down.
    """
    global running, default_cfg_dir

    # TODO: fix this for paths use default_cfg_dir and
    # default_runtime_dir
    files_to_remove = [
        default_cfg_dir + "/" + "iot.cfg",
        default_cfg_dir + "/" + "iot-connect.cfg",
        default_cfg_dir + "/" + "device_id"
        ]

    directories_to_remove = [
        os.path.join(user_data[0], "upload"),
        os.path.join(user_data[0], "download"),
        user_data[0]
        ]

    client.log(iot.LOGINFO, "Decommissioning Device!")

    for f in files_to_remove:
        try:
            os.remove(f)
        except OSError:
            client.log(iot.LOGWARNING, "Failed to remove {}".format(f))

    for directory in directories_to_remove:
        try:
            os.rmdir(directory)
        except OSError:
            client.log(iot.LOGWARNING, "Failed to remove {}".format(directory))

    client.log(iot.LOGINFO, "Device decommissioned! Stopping device manager.")
    running = False

    return (iot.STATUS_SUCCESS, "")

def device_reboot():
    """
    Callback for the "reboot_device" method which reboots the system.
    """
    retval = osal.system_reboot()

    if retval == 0:
        status = iot.STATUS_SUCCESS
        message = ""
    elif retval == osal.NOT_SUPPORTED:
        status = iot.STATUS_NOT_SUPPORTED
        message = "Reboot not supported on this platform!"
    else:
        status = iot.STATUS_FAILURE
        message = "Reboot failed with return code: " + str(retval)

    return (status, message)

def device_shutdown():
    """
    Callback for the "reboot_device" method which shuts-down the system.
    """
    retval = osal.system_shutdown()

    if retval == 0:
        status = iot.STATUS_SUCCESS
        message = ""
    elif retval == osal.NOT_SUPPORTED:
        status = iot.STATUS_NOT_SUPPORTED
        message = "Shutdown not supported on this platform!"
    else:
        status = iot.STATUS_FAILURE
        message = "Shutdown failed with return code: " + str(retval)

    return (status, message)


def file_download(client, params, user_data):
    """
    Callback for the "file_download" method which downloads a file from the
    cloud to the local system.
    """
    file_name = None
    file_path = None
    result = None
    timeout = 15
    blocking = user_data[1]
    if hasattr(config, "download_timeout"):
        timeout = config.download_timeout

    if params:
        file_name = params.get("file_name")
        file_path = params.get("file_path")

        if file_name and not file_path:
            file_path = abspath(os.path.join(user_data[0], "download",
                                             file_name))
        if file_path and not file_name:
            file_name = os.path.basename(file_path)
        if file_path.startswith('~'):
            result = iot.STATUS_BAD_PARAMETER
            message = "Paths cannot use '~' to reference a home directory"
        elif not os.path.isabs(file_path):
            file_path = abspath(os.path.join(user_data[0], "download",
                                             file_path))

        file_global = params.get("use_global_store", False)

    if result is None:
        if file_name and file_path:
            dir_path = os.path.dirname(file_path)
            if not os.path.exists(dir_path):
                try:
                    os.makedirs(dir_path)
                except (OSError, IOError) as e:
                    result = iot.STATUS_IO_ERROR
                    message = ("Destination directory does not exist and could "
                               "not be created!")
                    client.error(message)
                    print(e)

            if result is None:
                client.log(iot.LOGINFO, "Downloading")
                result = client.file_download(file_name, file_path, \
                                              blocking=blocking, timeout=timeout, \
                                              file_global=file_global)
                if result == iot.STATUS_SUCCESS:
                    message = ""
                else:
                    message = iot.status_string(result)

        else:
            result = iot.STATUS_BAD_PARAMETER
            message = "No file name or destination given"

    return (result, message)


def file_upload(client, params, user_data):
    """
    Callback for the "file_upload" method which uploads a file from the
    cloud to the local system. Wildcards in the file name are supported.
    """
    file_name = None
    file_path = None
    result = None
    blocking = user_data[3]
    if params:
        file_name = params.get("file_name")
        file_path = params.get("file_path")

        if file_name and not file_path:
            file_path = abspath(os.path.join(user_data[0], "upload", file_name))
        if file_path and not file_name:
            file_name = os.path.basename(file_path)
        if file_path and os.path.isdir(file_path):
            file_path, file_name = file_upload_dir(user_data, file_path, file_name)
        if not file_name and not file_path:
            file_path, file_name = file_upload_dir(user_data, file_path, file_name)
        if file_path.startswith('~'):
            result = iot.STATUS_BAD_PARAMETER
            message = "Paths cannot use '~' to reference a home directory"
        elif not os.path.isabs(file_path):
            file_path = abspath(os.path.join(user_data[0], "upload",
                                             file_path))

        file_global = params.get("use_global_store", False)

    if result is None:
        if file_name and file_path:
            client.log(iot.LOGINFO, "Uploading {}".format(file_name))
            result = client.file_upload(file_path, upload_name=file_name, \
                                        blocking=blocking, timeout=240, \
                                        file_global=file_global)
            if result == iot.STATUS_SUCCESS:
                message = ""
                # If upload_tar_file, delete tar file that was created
                if user_data[2]:
                    os.remove(file_path)
                if user_data[1] and "upload" in file_path:
                    try:
                        # If upload_tar_file, delete associated files
                        if user_data[2]:
                            base = os.path.dirname(file_path)
                            for fn in os.listdir(base):
                                os.remove(base+os.sep+fn)
                        # If all of runtime_dir/upload uploaded, delete all files
                        elif file_name == "upload":
                            for fn in os.listdir(file_path):
                                os.remove(file_path+os.sep+fn)
                        else:
                            os.remove(file_path)

                    except (OSError, IOError) as err:
                        error = str(err)
                        print(error+". Unable to remove file.")
            else:
                message = iot.status_string(result)
        else:
            result = iot.STATUS_BAD_PARAMETER
            message = "No file name or location given"

    return (result, message)

def file_upload_dir(user_data, file_path, file_name):
    """
    Return upload file_path and file_name for when they are not specified
    (i.e. upload entire runtime directory) or the file_path is a directory. 
    """
    if not file_path:
        file_path = abspath(os.path.join(user_data[0], "upload"))
    if not file_name:
        file_name = os.path.basename(file_path)
    # If upload_tar_file, create tar file
    if user_data[2]:
        if sys.platform.startswith("win"):
            file_name = (file_path+".tar").replace("\\", "-")[3:]
        else:
            file_name = (file_path+".tar").replace("/", "-")[1:]
        with tarfile.open((file_path+os.sep+file_name), "w") as tar:
            for fn in os.listdir(file_path):
                tar.add((file_path+os.sep+fn), arcname=fn)
            file_path = abspath(os.path.join(file_path, file_name))
    print (file_path, file_name)
    return (file_path, file_name)

def get_adapter_mac():
    """
    Returns the MAC address for the current network adapter
    """
    mac = "00:00:00:00:00:00"

    if sys.platform.startswith("win"):
        addresses = []
        for line in os.popen("ipconfig /all"):
            if line.strip().startswith("Physical Address"):
                info = line.split(":")
                address = info[1].replace("-", ":").strip()
                if not address.startswith("00:00:00:00:00:00"):
                    addresses.append(address)
        for i in range(len(addresses)):   # Format array to string
            if i==0:
                mac = addresses[0]
            else:
                mac = mac+"; "+addresses[i]
    else:
        rmac = uuid.getnode()
        # Check if valid MAC or a fake one before proceeding
        if (rmac >> 40)%2:
            pass
        else:
            # Convert MAC into typical hex notation
            mac = hex(rmac).replace('0x', '')[:-1].upper().rjust(12, '0')
            for i in range(0, 5):
                ii = 2*(i+1)+i
                mac = mac[:ii] + ":" + mac[ii:]
    return mac

def method_not_implemented():
    """
    Callback for disabled methods that simply tells the cloud that the requested
    method is not enabled for this device
    """
    return (iot.STATUS_NOT_SUPPORTED, \
            "This method is disabled by its iot.cfg setting")

def publish_platform_info(client, attr_file_dir=default_cfg_dir, attr_file_name="attributes.cfg"):
    """
    Function to publish information about the current platform (device) to the
    cloud as attributes.
    """
    client.log(iot.LOGINFO, "Publishing platform Info")

    try:
        hdc_version = pkg_resources.get_distribution("device_cloud").version
    except pkg_resources.DistributionNotFound:
        hdc_version = "RC-17.00.00"

    client.attribute_publish("os_name", osal.os_name())
    client.attribute_publish("os_version", osal.os_version())
    client.attribute_publish("architecture", platform.machine())
    client.attribute_publish("hostname", platform.node())
    client.attribute_publish("kernel", osal.os_kernel())
    client.attribute_publish("hdc_version", hdc_version)
    client.attribute_publish("mac_address", get_adapter_mac())

    try:
        with open(os.path.join(attr_file_dir, attr_file_name), 'r') as attr_file:
            attribute_data = json.load(attr_file)
        attr_list = attribute_data["publish_attribute"]

        for key, value in attr_list.items():
            if key == "hdc_version":
                client.log(iot.LOGERROR, "Cannot set hdc_version")
            else:
                client.attribute_publish(key, value)
    except:
        pass

def quit_me():
    """
    Callback for the "quit" method which exits the device manager app.
    """
    global running
    running = False
    return (iot.STATUS_SUCCESS, "")

def check_listening_port(host, port):
    result = False
    check = False
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        check = sock.connect( (host, port) )
        print("Port %d listening" % port)
        result = True
        sock.close()
    except Exception as error:
        print("Port %d is not listening on %s" % (port,host))

    return result

def remote_access(client, params):
    """
    Start relay which will attempt to connect to a Cloud service and a local
    service and securely tunnel information between the two. Used primarily for
    Telnet.
    """
    url = params["url"]
    host = params["host"]
    protocol = int(params["protocol"])

    if not check_listening_port(host, protocol):
        return (iot.STATUS_FAILURE, "Error: port (%d) not listening" % protocol)

    secure = client.config.validate_cloud_cert is not False
    try:
        relay.create_relay(url, host, protocol, secure=secure,
                           log_func=client.log)
        return (iot.STATUS_SUCCESS, "")
    except Exception as error:
        client.error(str(error))
        return (iot.STATUS_FAILURE, str(error))

if __name__ == "__main__":
    signal.signal(signal.SIGINT, sighandler)
    if osal.POSIX:
        signal.signal(signal.SIGQUIT, sighandler)

    # Parse command line arguments
    parser = argparse.ArgumentParser(description="Device Manager for Python HDC")
    parser.add_argument("-i", "--app_id", help="Custom app id")
    parser.add_argument("-c", "--default_cfg_dir", help="Custom config directory")
    parser.add_argument("-f", "--config_file", help="Custom config file name (in config directory)")
    parser.add_argument("-l", "--log_to_file", help="Log to file", action="store_true")
    args = parser.parse_args(sys.argv[1:])

    # Check for command line arguments
    if args.app_id:
        app_id = args.app_id
    if args.default_cfg_dir:
        default_cfg_dir = args.default_cfg_dir
    if args.config_file:
        config_file = args.config_file

    # Initialize client called 'device_manager_py'
    client = iot.Client(app_id)
    client.config.config_dir = default_cfg_dir
    client.config.config_file = default_cfg_dir + "/" + config_file

    # handle logging to file
    if args.log_to_file:
        client.config.log_file = "device_manager.log"
    client.initialize()

    config = config_load(default_cfg_dir)
    runtime_dir = config.runtime_dir

    if hasattr(config, "log_level"):
        client.log_level(config.log_level)

    upload_dir = os.path.join(runtime_dir, "upload")
    download_dir = os.path.join(runtime_dir, "download")

    try:
        if not os.path.isdir(runtime_dir):
            os.mkdir(runtime_dir)

        if not os.path.isdir(upload_dir):
            os.mkdir(upload_dir)

        if not os.path.isdir(download_dir):
            os.mkdir(download_dir)

    except (OSError, IOError) as e:
        print(e)
        client.log(iot.LOGERROR, ("Could not create one or more runtime "
                                  "directories! Did you run the device manager "
                                  "with sufficient priviliges?"))

    # Setup an OTA Handler
    ota = ota_handler.OTAHandler()

    # Set action callbacks, if enabled in iot.cfg
    action_register_conditional(client, "file_download", file_download, \
                                config.actions_enabled.file_transfers, \
                                (runtime_dir, config.wait_for_file_transfer))
    action_register_conditional(client, "file_upload", file_upload, \
                                config.actions_enabled.file_transfers, \
                                (runtime_dir, config.upload_remove_on_success,
                                config.upload_tar_file, config.wait_for_file_transfer))

    action_register_conditional(client, "shutdown_device", device_shutdown, \
                                config.actions_enabled.shutdown_device)

    action_register_conditional(client, "reboot_device", device_reboot, \
                                config.actions_enabled.reboot_device)

    action_register_conditional(client, "remote-access", remote_access, \
                                config.actions_enabled.remote_login)

    action_register_conditional(client, "decommission_device", \
                                device_decommission, \
                                config.actions_enabled.decommission_device, \
                                (runtime_dir,))

    action_register_conditional(client, "software_update", \
                                ota.update_callback, \
                                config.actions_enabled.software_update,
                                (runtime_dir,))

    action_register_conditional(client, "reset_agent", agent_reset, \
                                config.actions_enabled.reset_agent, \
                                (runtime_dir, ota, [app_id, default_cfg_dir, config_file]))

    action_register_conditional(client, "quit", quit_me, \
                                config.actions_enabled.reset_agent)

    # Connect to Cloud
    if client.connect(timeout=10) != iot.STATUS_SUCCESS:
        client.error("Failed")
        sys.exit(1)

    ack_messages(client, os.path.join(runtime_dir, "message_ids"))

    # Publish system details
    publish_platform_info(client, default_cfg_dir)

    if os.path.isfile(os.path.join(runtime_dir, ".otalock")):
        try:
            os.remove(os.path.join(runtime_dir, ".otalock"))
        except (OSError, IOError) as err:
            error = str(err)
            print(error+". This file blocks OTA operations. Please remove it.")

    while running and client.is_alive():

        # Wrap sleep with an exception handler to fix SIGINT handling on Windows
        try:
            sleep(1)
        except IOError as err:
            if err.errno != errno.EINTR:
                raise

    # Stop remote access
    relay.stop_relays()

    # Wait for any OTA operations to finish
    if ota.is_running():
        client.log(iot.LOGINFO, "Waiting for OTA to finish...")
        ota.join()

    client.disconnect(wait_for_replies=True)

