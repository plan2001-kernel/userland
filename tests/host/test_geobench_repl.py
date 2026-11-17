"""Installed one-command REPL launcher: setup is idle and grants stay explicit."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('make_repl',ROOT/'tools/geobench/make_repl_init.py');builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
sys.path.insert(0,str(ROOT.parent/'kernel/tools/host'))
from object_format import pack,cbor

class ReplLauncherTests(unittest.TestCase):
    def test_startup_only_defines_function(self):
        events=[]
        env={'print':lambda *args:events.append(args)}
        exec(builder.source(0x24d,0x24b),env)
        self.assertTrue(callable(env['geobench']))
        self.assertEqual(events,[('Native GE/O benchmark ready: geobench()',)])
    def run_launcher(self,status='',bad=False):
        events=[];p=types.ModuleType('plan9');space=types.ModuleType('space')
        p.obj_open_volume=lambda:events.append(('volume',)) or 41
        p.obj_open_ge_oid=lambda v,o:events.append(('result',v,o)) or 42
        p.process_wait=lambda h,t:events.append(('wait',h,t)) or (99,status)
        p.obj_read=lambda h,n:pack(b'bad' if bad else b'geobench_v1,example\n# PASS direct whole-object workload; rounds=3;\n',cbor({'profile':'plan2001.geobench.v1'}))
        p.obj_close=lambda h:events.append(('close',h))
        def spawn(oid,**kw):events.append(('spawn',oid,kw));return 43
        space.spawn=spawn
        saved={name:sys.modules.get(name) for name in ('plan9','space')}
        sys.modules['plan9']=p;sys.modules['space']=space
        env={'print':lambda *args:events.append(('print',args))};exec(builder.source(0x24d,0x24b),env)
        try:
            if status:
                with self.assertRaises(RuntimeError):env['geobench']()
            elif bad:
                with self.assertRaises(ValueError):env['geobench']()
            else:env['geobench']()
        finally:
            for name,value in saved.items():
                if value is None:sys.modules.pop(name,None)
                else:sys.modules[name]=value
        self.assertEqual([e[1] for e in events if e[0]=='close'],[43,42,41])
        return events
    def test_one_call_spawns_native_full_benchmark(self):
        events=self.run_launcher()
        spawn=next(e for e in events if e[0]=='spawn')
        self.assertEqual(spawn,('spawn',0x24d,{'handle':41,'argv':('geobench',),'vhandles':(41,42)}))
    def test_failed_child_closes_capabilities(self):self.run_launcher(status='GEOBENCH INVALID')
    def test_incomplete_result_is_rejected(self):self.run_launcher(bad=True)

if __name__=='__main__':unittest.main()
