#!/usr/bin/env python

import argparse
import errno
import random
import signal
import sys
import os
from time import sleep
from datetime import datetime

unknown_path = './unknown'
image_name = 'unknown-person.jpg'

def capture_image ():
    os.system('./uvc ')
    os.system('rm -rf ' + unknown_path + ' && mkdir ' + unknown_path)
    os.system('mv ' + image_name + ' ' + unknown_path)
