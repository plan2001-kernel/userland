#!/usr/bin/env python3
"""Build the same whole-object workload for Linux and the direct native ABI."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--kernel',type=Path,default=Path(__file__).resolve().parents[3]/'kernel');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
k=a.kernel.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);here=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('abi',k/'tools/host/syscall_abi.py');abi=importlib.util.module_from_spec(spec);spec.loader.exec_module(abi)
wanted={'OBJCREATE':'objcreate','OBJOPEN':'objopen','OBJCLOSE':'objclose','OBJINVOKE':'objinvoke','SYSCTL':'sysctl','_NSEC':'_nsec','EXITS':'_exits','ERRSTR':'errstr'}
lines=['.text'];found=set()
for name,number,reserved in abi.syscall_entries():
 if name not in wanted or reserved:continue
 sym=wanted[name];found.add(name)
 lines += [f'.globl {sym}',f'{sym}:','subq $56,%rsp','movq %rbp,0(%rsp)','movq %rdi,8(%rsp)','movq %rsi,16(%rsp)','movq %rdx,24(%rsp)','movq %rcx,32(%rsp)','movq %r8,40(%rsp)',f'movq ${number},%rbp','syscall','movq 0(%rsp),%rbp','addq $56,%rsp','ret']
if found!=set(wanted):raise SystemExit('required native syscalls not present in kernel ABI: '+str(set(wanted)-found))
lines.append('.section .note.GNU-stack,"",@progbits');(out/'stubs.S').write_text('\n'.join(lines)+'\n')
def run(cmd):subprocess.run(list(map(str,cmd)),check=True)
flags=['gcc','-m64','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fno-builtin']
run(flags+[here/'core.c',here/'linux.c','-o',out/'geobench-linux'])
objects=[]
for source in [here/'core.c',here/'native.c',k/'micropython/gcc/startup.S',out/'stubs.S']:
 obj=out/(source.stem+'.o');run(flags+['-ffreestanding','-fno-stack-protector','-fno-pie','-fno-pic','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-nostdinc','-I'+str(k/'runtime/abi'),'-c',source,'-o',obj]);objects.append(obj)
run(['ld','-static','-z','max-page-size=4096','--build-id=none','-T',k/'micropython/gcc/user.ld','-o',out/'geobench.elf',*objects])
undefined=subprocess.check_output(['nm','-u',out/'geobench.elf'],text=True)
if undefined:raise SystemExit('unexpected runtime dependencies: '+undefined)
print(out/'geobench.elf')
