"""Build/audit the libc-free native ELF and exercise the shared Linux core."""
import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
KERNEL=ROOT.parent/'kernel'

class DirectBenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='geobench-');cls.path=Path(cls.temp.name)
        cls.build=cls.path/'build'
        subprocess.run([sys.executable,str(ROOT/'tools/geobench/build.py'),'--kernel',str(KERNEL),'--output',str(cls.build)],check=True,stdout=subprocess.DEVNULL)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_case(self,smoke):
        destination=Path(tempfile.mkdtemp(prefix='smoke-' if smoke else 'full-',dir=self.path))
        command=[str(self.build/'geobench-linux'),str(destination)]+(['smoke'] if smoke else [])
        r=subprocess.run(command,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
        rows=list(csv.DictReader(x for x in r.stdout.splitlines() if not x.startswith('#')))
        self.assertEqual(len(rows),7 if smoke else 21)
        self.assertTrue(all(int(x['elapsed_ns'])>0 for x in rows))
        self.assertTrue(all(int(x['stored_bytes'])//int(x['operations'])<=1024*1024 for x in rows))
        self.assertIn('# PASS direct whole-object workload;',r.stdout)
        return destination,r.stdout,rows
    def test_native_has_no_unix_or_libc_link_dependencies(self):
        elf=self.build/'geobench.elf'
        self.assertEqual(subprocess.check_output(['nm','-u',str(elf)]),b'')
        symbols=subprocess.check_output(['nm',str(elf)],text=True).splitlines()
        forbidden={'open','read','write','malloc','free','pj_read','pj_write','uxfs_read','p9_file_attach','p9_process_startup'}
        self.assertFalse(forbidden & {line.split()[-1] for line in symbols})
        headers=subprocess.check_output(['readelf','-l',str(elf)],text=True)
        self.assertEqual(headers.count(' LOAD '),2);self.assertNotIn('INTERP',headers)
    def test_smoke_frames_and_integrity(self):
        destination,text,rows=self.run_case(True)
        sys.path.insert(0,str(KERNEL/'tools/host'))
        from object_format import unpack,decode
        objects=list(destination.glob('geobench-*/object-*'));self.assertEqual(len(objects),3)
        sizes=[]
        for p in objects:
            raw,meta=unpack(p.read_bytes());sizes.append(len(raw))
            self.assertEqual(decode(meta),{'profile':'plan2001.geobench.v1'})
        self.assertEqual(sorted(sizes),[4096,4096,65536])
        self.assertEqual({r['kind'] for r in rows},{'smoke'})
    def test_full_result_comparison_and_batch_boundary(self):
        _,text,rows=self.run_case(False)
        result=self.path/'linux.csv';result.write_text(text)
        r=subprocess.run([sys.executable,str(ROOT/'tools/geobench/compare.py'),str(result)],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(len(r.stdout.splitlines()),8)
        batch=[x for x in rows if x['phase']=='small_replace_batch']
        self.assertTrue(all(int(x['completion_calls'])==32 for x in batch))
    def test_comparison_rejects_smoke(self):
        _,text,_=self.run_case(True)
        path=self.path/'smoke.csv';path.write_text(text)
        r=subprocess.run([sys.executable,str(ROOT/'tools/geobench/compare.py'),str(path)],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0)

if __name__=='__main__':unittest.main()
