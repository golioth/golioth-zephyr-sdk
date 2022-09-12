import sys
import os
import serial
from time import time
import re
import yaml

def wait_for_regex_in_line(ser, regex, timeout_s=200, log=True):
    start_time = time()
    while True:
        line = ser.readline().decode('utf-8', errors='replace').replace("\r\n", "")
        if line != "" and log:
            print(line)
        if time() - start_time > timeout_s:
            raise RuntimeError('Timeout')
        regex_search = re.search(regex, line)
        if regex_search:
            return regex_search

def set_setting(ser, key, value):
    ser.write('\r\n'.encode())
    wait_for_regex_in_line(ser, 'uart:', log=False)
    ser.write('settings set {} {}\r\n'.format(key, value).encode())
    wait_for_regex_in_line(ser, 'saved', log=False)

def set_credentials(ser):
    with open('credentials.yml', 'r') as f:
        credentials = yaml.safe_load(f)

    print('===== Setting credentials via CLI (logging disabled) ====')
    settings = credentials['settings']
    for key, value in settings.items():
        set_setting(ser, key, value)

def reset(ser):
    ser.write('\r\n'.encode())
    wait_for_regex_in_line(ser, 'uart:')
    ser.write('kernel reboot cold\r\n'.encode())
    # Wait for string that prints on next boot
    wait_for_regex_in_line(ser, 'Booting Zephyr OS')
    wait_for_regex_in_line(ser, 'uart:')

def green_print(s):
    green = '\033[92m'
    resetcolor = '\033[0m'
    print(green + s + resetcolor)

def red_print(s):
    red = '\033[31m'
    resetcolor = '\033[0m'
    print(red + s + resetcolor)

def main():
    if len(sys.argv) != 2:
        print('usage: {} <port>'.format(sys.argv[0]))
        sys.exit(-1)
    port = sys.argv[1]

    # Connect to the device over serial and use the shell CLI to interact and run tests
    print("Opening serial port: {}".format(port))
    ser = serial.Serial(port, 115200, timeout=1, writeTimeout=1)

    # Set Golioth credentials over device shell CLI
    set_credentials(ser)
    reset(ser)

    regex_search = wait_for_regex_in_line(ser, 'test_golioth.*(failed|succeeded)')
    passed = (regex_search.groups()[0] == 'succeeded')

    if passed:
        green_print('---------------------------')
        print('')
        green_print('  ✓ All Tests Passed 🎉')
        print('')
        green_print('---------------------------')
        sys.exit(0)
    else:
        red_print('---------------------------')
        print('')
        red_print('  ✗ Failed Tests')
        print('')
        red_print('---------------------------')
        sys.exit(1)

if __name__ == "__main__":
    main()
