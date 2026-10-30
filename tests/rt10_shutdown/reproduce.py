"""Portable source-subset regression; creates a new output directory only."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from extractor import function
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--fixed',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);here=Path(__file__).resolve().parent
funcs={'destructor.inc':('vdma/vdma_config_core_op.cpp','VdmaConfigCoreOp::~VdmaConfigCoreOp'),'public-shutdown.inc':('vdma/vdma_config_core_op.cpp','VdmaConfigCoreOp::shutdown'),'manager.inc':('core_op/resource_manager/resource_manager.cpp','ResourcesManager::reset_state_machine'),'deactivate.inc':('vdma/vdma_config_core_op.cpp','VdmaConfigCoreOp::deactivate_impl'),'shutdown.inc':('vdma/vdma_config_core_op.cpp','VdmaConfigCoreOp::shutdown_impl')}
results={};commands=[]
def run(argv,label):
 p=subprocess.run(list(map(str,argv)),capture_output=True,timeout=40)
 (out/(label+'.stdout')).write_bytes(p.stdout);(out/(label+'.stderr')).write_bytes(p.stderr)
 commands.append({'argv':list(map(str,argv)),'returncode':p.returncode});(out/'commands.json').write_text(json.dumps(commands,indent=2));return p
for label,tree in [('baseline',a.baseline),('fixed',a.fixed)]:
 d=out/label;d.mkdir();src=tree.resolve()/'hailort/libhailort/src';hashes={}
 for inc,(rel,name) in funcs.items():
  p=src/rel;text=p.read_text();(d/inc).write_text(function(text,name));hashes[rel]=hashlib.sha256(p.read_bytes()).hexdigest()
 hpp=(src/'vdma/vdma_config_core_op.hpp').read_text()
 if label=='baseline':
  assert 'std::atomic_bool m_is_shutdown{false};' in hpp;state='std::atomic_bool m_is_shutdown{false};'
 else:
  start=hpp.index('    static constexpr int SHUTDOWN_NOT_STARTED');end=hpp.index('m_shutdown_status{SHUTDOWN_NOT_STARTED};')+len('m_shutdown_status{SHUTDOWN_NOT_STARTED};');state=hpp[start:end]
 (d/'fixture.cpp').write_text((here/'fixture.cpp').read_text().replace('PRIVATE_STATE',state))
 exe=d/'test';p=run(['g++','-std=c++14','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(tree.resolve()/'hailort/libhailort/include'),'-I'+str(d),d/'fixture.cpp','-o',exe],label+'-compile');assert p.returncode==0,p.stderr.decode()
 results[label]={'inputs_sha256':hashes,'cases':{}}
 for mode in ['minimal','core','extra','review']:
  p=run([exe,mode],label+'-'+mode);row=json.loads(next(s[7:] for s in p.stdout.decode().splitlines() if s.startswith('RESULT ')));row['returncode']=p.returncode;results[label]['cases'][mode]=row
(out/'results.json').write_text(json.dumps(results,indent=2))
expected={'baseline':{'minimal':(0,2),'core':(4,250),'extra':(1,16),'review':(7,4)},'fixed':{'minimal':(2,0),'core':(254,0),'extra':(17,0),'review':(11,0)}}
for label,modes in expected.items():
 for mode,(passed,failed) in modes.items():
  r=results[label]['cases'][mode];assert (r['passed'],r['failed'],r['skipped'],r['returncode'])==(passed,failed,0,1 if failed else 0),(label,mode,r)
print(json.dumps({k:v['cases'] for k,v in results.items()},indent=2))
