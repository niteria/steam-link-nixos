#!/usr/bin/env python3
"""Actual ARM diagnostic client against a file-protocol fixture; no sound/hardware."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time
import unittest

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--client",type=Path,required=True);parser.add_argument("--qemu",type=Path,required=True)
args,remaining=parser.parse_known_args()

class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir="/tmp" if not Path("/tmp/opencode").exists() else "/tmp/opencode")
        self.root=Path(self.temp.name);self.root.chmod(0o700)
        self.write("ready",json.dumps({"protocol":1,"audio_protocol":5},separators=(",",":")))
        self.requests=[];self.audio=True;self.sdk=False;self.running=True
        def server():
            while self.running:
                request=self.root/"audio-request"
                if request.exists():
                    data=request.read_text();request.unlink()
                    match=re.fullmatch(r"([0-9a-f]{16}) (sink|channels|sync)\n",data)
                    if not match:continue
                    nonce,mode=match.groups();self.requests.append(mode)
                    result={"request":nonce,"okay":True,"mode":mode,"lpcm_stereo_48k16":self.audio}
                    if self.sdk:result["sink_capability_source"]="sdk-at-test-start";result["lpcm_stereo_48k16"]=None
                    self.write("audio-result",json.dumps(result,separators=(",",":")))
                time.sleep(0.005)
        self.thread=threading.Thread(target=server,daemon=True);self.thread.start()
    def write(self,name,data):
        path=self.root/name;path.write_text(data);path.chmod(0o600)
    def tearDown(self):
        self.running=False;self.thread.join();self.temp.cleanup()
    def run_client(self,mode):
        return subprocess.run([str(args.qemu),str(args.client),mode,"--yes","--control",str(self.root)],capture_output=True,text=True,timeout=5)
    def test_sink_query_does_not_start_audio(self):
        result=self.run_client("sink");self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(self.requests,["sink"])
    def test_unsupported_sink_refuses_before_audio_request(self):
        self.audio=False;result=self.run_client("channels")
        self.assertEqual(result.returncode,3,result.stderr);self.assertEqual(self.requests,["sink"])
    def test_channels_and_sync_use_explicit_correlated_requests(self):
        for mode in ("channels","sync"):
            result=self.run_client(mode);self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])["mode"],mode)
        self.assertEqual(self.requests,["sink","channels","sink","sync"])
    def test_insecure_control_directory_refused(self):
        self.root.chmod(0o755);result=self.run_client("channels")
        self.assertNotEqual(result.returncode,0);self.assertEqual(self.requests,[])
    def test_unknown_edid_is_not_fabricated_as_a_capability_pass(self):
        self.sdk=True;result=self.run_client("channels");self.assertEqual(result.returncode,0,result.stderr)
        self.assertIsNone(json.loads(result.stdout.splitlines()[-1])["lpcm_stereo_48k16"])
        self.assertIn("fresh HDMI EDID is unqualified",result.stderr)
    def test_old_audio_revision_refused_before_request(self):
        for revision in (1,2,3,4):
            self.write("ready",json.dumps({"protocol":1,"audio_protocol":revision},separators=(",",":")))
            result=self.run_client("channels")
            self.assertNotEqual(result.returncode,0);self.assertEqual(self.requests,[])

if __name__=="__main__":unittest.main(argv=[__file__,*remaining])
