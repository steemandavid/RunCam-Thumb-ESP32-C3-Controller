#!/usr/bin/env python3
"""Convert index.html to a C PROGMEM header file."""
import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: html_to_progmem.py <input.html> <output.h>")
        sys.exit(1)

    with open(sys.argv[1], 'r') as f:
        html = f.read()

    with open(sys.argv[2], 'w') as out:
        out.write('#pragma once\n')
        out.write('const char WEB_UI[] PROGMEM = {\n')
        for i, ch in enumerate(html):
            if i > 0:
                out.write(',')
            out.write(f'\n  0x{ord(ch):02X}')
        out.write(', 0x00\n};\n')

if __name__ == '__main__':
    main()
