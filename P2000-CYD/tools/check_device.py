#!/usr/bin/env python3
"""Collect a bounded serial smoke test from P2000 CYD firmware 1.0.1+."""
import argparse
import time
import serial

parser=argparse.ArgumentParser()
parser.add_argument('--port',required=True)
parser.add_argument('--seconds',type=int,default=75)
parser.add_argument('--no-scan',action='store_true',help='Only observe normal startup/retry; do not trigger scan or API.')
args=parser.parse_args()
connection=serial.Serial(port=None,baudrate=115200,timeout=0.2)
connection.dtr=False
connection.rts=False
connection.port=args.port
connection.open()
schedule=[(20,b'status\n'),(25,b'scan\n'),(38,b'status\n'),(42,b'api\n'),(68,b'status\n')]
if args.no_scan:
    schedule=[(20,b'status\n'),(50,b'status\n')]
start=time.monotonic()
try:
    while time.monotonic()-start < args.seconds:
        elapsed=time.monotonic()-start
        if schedule and elapsed>=schedule[0][0]:
            _,command=schedule.pop(0)
            print('\n[controle: '+command.decode().strip()+']',flush=True)
            connection.write(command)
        data=connection.read(4096)
        if data:
            print(data.decode('utf-8',errors='replace'),end='',flush=True)
finally:
    connection.close()
