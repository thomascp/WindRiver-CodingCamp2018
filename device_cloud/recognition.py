#!/usr/bin/env python

import argparse
import errno
import random
import signal
import sys
import os
import face_recognition
from update_version import authdata_file
from capture import unknown_path
from capture import image_name
import device_cloud as iot
 
def recognition_person(client, authdata_database, authdata_dataname) :
    unknown_picture = face_recognition.load_image_file(unknown_path + '/' + image_name)
    face_locations = face_recognition.face_locations(unknown_picture)
    if len(face_locations) == 0 :
        client.info ('No one '); 
        return "noone"
    unknown_face_encoding = face_recognition.face_encodings(unknown_picture)[0]
 
    for i in range(len(authdata_database)):
        client.info ('checking ' + authdata_dataname[i]);
        results = face_recognition.compare_faces([authdata_database[i]], unknown_face_encoding, tolerance=0.4)
        
        if results[0] == True:
            client.info ('checking pass, welcome ' + authdata_dataname[i]);
            return authdata_dataname[i]

    return "unknown"
    
        