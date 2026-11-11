#!/usr/bin/env python3
"""Compare a complete stopped trace after playback has drained.

No inference is made about packets missing before the SDK callback. Equal
fingerprints corroborate content continuity, not a collision-free identity.
Missing stages can include in-flight/cancelled packets: inspect session status.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


def analyze(text):
    stages={i:Counter() for i in range(1,5)}
    count=0;ended=False
    for line in text.splitlines():
        if line.startswith('AUDIO_TRACE_ROW '):
            if ended:raise ValueError('data after trace end')
            parts=line.split()
            if len(parts)!=12:raise ValueError('malformed trace row')
            values=list(map(int,parts[1:]))
            index,stage,generation,ts,length,media,flags,stream,hash_value,at,detail=values
            if index!=count or stage not in stages or not 0 < length <= 1500:
                raise ValueError('missing row, invalid stage or unsupported fingerprint')
            stages[stage][(generation,ts,length,media,flags,stream,hash_value)]+=1
            count+=1
        elif line.startswith('AUDIO_TRACE_END '):
            match=re.fullmatch(r'AUDIO_TRACE_END count=(\d+) lost=(\d+)',line)
            if ended or not match or int(match[1])!=count or int(match[2]):
                raise ValueError('incomplete trace or lost observations')
            ended=True
    if not ended or not count:raise ValueError('empty or unterminated trace')
    reference=stages[1]
    differences={}
    for i,name in ((2,'enqueue'),(3,'dequeue'),(4,'written')):
        differences[name]={'missing':sum((reference-stages[i]).values()),
                           'extra_or_changed':sum((stages[i]-reference).values())}
    return {'rows':count,'stage_counts':{name:sum(stages[i].values()) for i,name in
             enumerate(('callback','enqueue','dequeue','written'),1)},
            'all_stages_match':bool(reference) and all(reference==stages[i] for i in range(2,5)),
            'differences':differences}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path)
    args=parser.parse_args()
    print(json.dumps(analyze(args.log.read_text()),indent=2))
