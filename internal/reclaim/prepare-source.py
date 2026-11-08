"""Reuse the qualified state engine with a distinct discontiguous write policy."""
from pathlib import Path

def replace(path,old,new):
    p=Path(path);data=p.read_text();assert data.count(old)==1,(path,old)
    p.chmod(p.stat().st_mode|0o200);p.write_text(data.replace(old,new))

replace('state-core.h','if(block<2228||block>=4086)',
        'if(!sl_trial_erase_allowed((unsigned long long)block<<20,1048576))')
if Path('steamlink_slc_trial.h').exists():
    p=Path('steamlink_slc_trial.h');data=p.read_text();anchor='static const struct mtd_partition slc_trial_parts[]='
    assert data.count(anchor)==1 and data.rstrip().endswith('};')
    p.chmod(p.stat().st_mode|0o200);p.write_text(data.split(anchor)[0]+'#include "reclaim-parts.h"\n')
