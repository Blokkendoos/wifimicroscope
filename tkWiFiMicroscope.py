#!/usr/bin/env python3

# MS5 WiFi Microscope Viewer
# march 2025

# Proof-of-concept code for reading data from a Wifi microscope.
# https://www.chzsoft.de/site/hardware/reverse-engineering-a-wifi-microscope/.

# Copyright (c) 2020, Christian Zietz <czietz@gmx.net>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from tkinter import Tk
from tkinter import ttk
from PIL import ImageTk, Image
from threading import Thread
import queue
import cv2
import sys
import time
import socket
import numpy as np


class ShowGui:
    def __init__(self, master, qstream, end_cmd):
        self.master = master
        self.queue = qstream
        # Create a frame
        master.geometry("800x500")
        frm = ttk.Frame(master, borderwidth=5)
        frm.grid()
        # Create a label in the frame
        self.lmain = ttk.Label(frm)
        self.lmain.grid()
        # Control
        btn = ttk.Button(master, text='Done', command=end_cmd)
        btn.grid()

    def process(self):
        try:
            frame = self.queue.get()
            img = cv2.cvtColor(frame, cv2.COLOR_BGR2RGBA)
            img = cv2.resize(img, (790, 450))
            img = Image.fromarray(img)
            imgtk = ImageTk.PhotoImage(image=img)
            self.lmain.imgtk = imgtk
            self.lmain.configure(image=imgtk)
        except queue.Empty:
            # print("Queue empty...")
            pass


class AsyncVideo:
    def __init__(self, master, host='192.168.29.1', sport=20000, rport=10900):
        self.master = master
        self.host = host  # Microscope
        self.sport = sport  # Microscope command port
        self.rport = rport  # Receive port for JPEG frames
        self.frame_rate = 25  # fps
        self.frame_duration = 1 / self.frame_rate  # seconds
        # GUI
        self.queue = queue.Queue(maxsize=self.frame_rate)
        self.gui = ShowGui(self.master, self.queue, self.end_application)
        # Asynchronous I/O thread
        self.running = True
        self.thread = Thread(target=self.worker_thread)
        self.thread.start()
        self.periodic_call()

    def periodic_call(self):
        """
        Check if there is something new in the queue.
        """
        self.gui.process()
        if not self.running:
            # This is the brutal stop of the system. You may want to do
            # some cleanup before actually shutting it down.
            sys.exit(1)
        self.master.after(50, self.periodic_call)

    def worker_thread(self):
        """
        This is where we handle the asynchronous I/O.
        One important thing to remember is that the thread has
        to yield control pretty regularly, by select or otherwise.
        """
        # Recipe according to:
        # https://code.activestate.com/recipes/82965-threads-tkinter-and-asynchronous-io

        # Open command socket for sending
        # with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        except socket.error as err:
            print(f"Command socket creation failed with error {err}")
            sys.exit(1)  # TODO graceful exit
        # s.settimeout(1.0)
        # s.sendto(b"JHCMD\xd0\x00", (self.host, self.sport))
        # Send commands like naInit_Re() would do
        s.sendto(b"JHCMD\x10\x00", (self.host, self.sport))
        s.sendto(b"JHCMD\x20\x00", (self.host, self.sport))
        # Heartbeat command, starts the transmission of data from the scope
        s.sendto(b"JHCMD\xd0\x01", (self.host, self.sport))
        s.sendto(b"JHCMD\xd0\x01", (self.host, self.sport))

        # Open receive socket and bind to receive port
        # with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as r:
        try:
            r = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        except socket.error as err:
            print(f"Stream socket creation failed with error {err}")
            sys.exit(1)
        # r.settimeout(1.0)
        r.bind(('', self.rport))
        r.setblocking(0)
        print(f"Listening on port {self.rport}")
        buffer = bytearray()
        last_time = time.time()
        while self.running:
            try:
                data = r.recv(1450)
                if len(data) > 8:
                    # Header
                    framecount = data[0] + data[1]*256
                    packetcount = data[3]
                    # Data
                    if packetcount == 0:
                        # A new frame has started
                        if framecount % 25 == 0:
                            # Send a heartbeat every x frames
                            # (arbitrary number) to keep data flowing
                            # print(f"Heartbeat frame {framecount}")
                            s.sendto(b"JHCMD\xd0\x01", (self.host, self.sport))
                        # Convert JPEG to Numpy array
                        if len(buffer) > 0:
                            img = cv2.imdecode(np.frombuffer(buffer,
                                                             dtype=np.uint8),
                                               cv2.IMREAD_UNCHANGED)
                            # Reduce framerate
                            if (time.time() - last_time) > self.frame_duration:
                                last_time = time.time()
                                if not self.queue.full():
                                    self.queue.put(img)
                            buffer = bytearray()
                    buffer.extend(data[8:])
            except socket.error:  # as err:
                # print(f"Packets lost: {err}")
                time.sleep(0.05)
        # Stop data command, like in naStop()
        print("Exiting...")
        s.sendto(b"JHCMD\xd0\x02", (self.host, self.sport))
        s.close()
        r.close()

    def end_application(self):
        self.running = False


if __name__ == '__main__':
    root = Tk()
    root.title("tkWiFiMicroscope")
    client = AsyncVideo(root)
    root.mainloop()
