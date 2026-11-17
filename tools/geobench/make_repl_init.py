#!/usr/bin/env python3
"""Bind installed native executable/result identities into a REPL initializer."""
import argparse
from pathlib import Path

def oid(text):
    value=int(text,0)
    if not 0<value<1<<64:raise argparse.ArgumentTypeError('OID must be a nonzero unsigned 64-bit integer')
    return value

def source(program,result):
    return f'PROGRAM_OID={program:#x}\nRESULT_OID={result:#x}\n'+Path(__file__).with_name('repl.py').read_text()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('program',type=oid);p.add_argument('result',type=oid);p.add_argument('output',type=Path);a=p.parse_args()
    a.output.write_text(source(a.program,a.result))
