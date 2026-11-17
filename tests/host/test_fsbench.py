"""Compile the shared source and exercise its Linux completion policies."""
import csv
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]

@unittest.skipUnless(shutil.which('gcc'),'GCC is required for the host smoke test')
class BenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='fsbench-host-');cls.dir=Path(cls.temp.name)
        cls.exe=cls.dir/'fsbench'
        subprocess.run(['gcc','-O0','-std=gnu11','-Wall','-Wextra','-Werror',str(ROOT/'tools/fsbench/fsbench.c'),'-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_bench(self,*args):return subprocess.run([str(self.exe),*map(str,args)],capture_output=True,text=True)
    def test_both_linux_policies_and_exclusive_work_directories(self):
        paths=[]
        for mode in ('buffered','durable'):
            r=self.run_bench('--dir',self.dir/'data','--mode',mode,'--smoke')
            self.assertEqual(r.returncode,0,r.stderr);self.assertIn('# PASS verified all file contents;',r.stdout)
            rows=list(csv.DictReader(io.StringIO('\n'.join(line for line in r.stdout.splitlines() if not line.startswith('#')))))
            self.assertEqual(len(rows),5);self.assertEqual({x['mode'] for x in rows},{mode})
            self.assertEqual({x['kind'] for x in rows},{'smoke'})
            self.assertTrue(all(int(x['elapsed_us'])>0 for x in rows))
            path=next(line.split('working_directory=')[1] for line in r.stdout.splitlines() if 'working_directory=' in line)
            self.assertEqual(list(Path(path).iterdir()),[]);paths.append(path)
        self.assertNotEqual(paths[0],paths[1])
    def test_invalid_rounds_do_not_start_a_workload(self):
        target=self.dir/'invalid'
        r=self.run_bench('--dir',target,'--rounds','0')
        self.assertEqual(r.returncode,2);self.assertFalse(target.exists())
    def test_existing_file_is_never_truncated(self):
        target=self.dir/'sentinel';target.write_text('preserve')
        r=self.run_bench('--dir',target,'--smoke')
        self.assertEqual(r.returncode,1);self.assertIn('INVALID',r.stderr);self.assertEqual(target.read_text(),'preserve')
    def test_comparison_rejects_smoke_results(self):
        r=self.run_bench('--dir',self.dir/'comparison','--smoke');self.assertEqual(r.returncode,0,r.stderr)
        path=self.dir/'smoke.csv';path.write_text(r.stdout)
        result=subprocess.run(['python3',str(ROOT/'tools/fsbench/compare.py'),str(path)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('not a benchmark result',result.stderr)

if __name__=='__main__':unittest.main()
