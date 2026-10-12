import copy
import importlib.util
import json
import pathlib
import tempfile
import threading
import unittest
from unittest import mock
import urllib.parse
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer

ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("clock_link",ROOT/"tools/clock_link.py")
link=importlib.util.module_from_spec(spec);spec.loader.exec_module(link)
SAMPLE={"schema":1,"board":"esp32-devkitc-32e-rev20","device_id":"123456789abc","firmware":"1.0.11",
        "uptime_ms":200,"snapshot_age_ms":0,"accepted_quotes":123,"nixie_frames":23,"refresh_ms":500,"boot_confirmed":True,
        "dials":{"frequency":{"mv":309,"raw":100,"decoded":0,"selected":0,"initialized":True},
                 "range":{"mv":926,"raw":1200,"decoded":2,"selected":2,"initialized":True}}}
class Handler(BaseHTTPRequestHandler):
    sample=SAMPLE;commands=[];fail=False;oversize=False
    def log_message(self,*args):pass
    def send(self,value,status=200):
        body=json.dumps(value).encode();self.send_response(status);self.send_header("Content-Type","application/json");self.send_header("Content-Length",str(len(body)));self.end_headers();self.wfile.write(body)
    def do_GET(self):
        if self.path=="/telemetry":
            self.send({"error":"retry"},503) if self.fail else self.send({"data":"x"*17000}) if self.oversize else self.send(self.sample)
        elif self.path=="/control-token":self.send({"token":"a"*32})
        else:self.send({},404)
    def do_POST(self):
        fields=urllib.parse.parse_qs(self.rfile.read(int(self.headers["Content-Length"])).decode())
        if fields.get("token")!=["a"*32]:self.send({},403);return
        self.commands.append(fields["action"][0]);self.send({"message":"queued"},202)

class LinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=ThreadingHTTPServer(("127.0.0.1",0),Handler)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
    @classmethod
    def tearDownClass(cls):cls.server.shutdown();cls.server.server_close();cls.thread.join()
    def setUp(self):
        Handler.sample=copy.deepcopy(SAMPLE);Handler.fail=Handler.oversize=False;Handler.commands=[]
        self.clock=link.Clock(["127.0.0.2","127.0.0.1"],self.server.server_port,timeout=.1)
    def test_real_http_control_and_reconnect(self):
        self.assertEqual(self.clock.snapshot(),SAMPLE);self.assertEqual(self.clock.last_host,"127.0.0.1")
        self.assertEqual(self.clock.command("ota"),{"message":"queued"});self.assertEqual(Handler.commands,["ota"])
        Handler.fail=True
        with self.assertRaises(RuntimeError):self.clock.snapshot()
        Handler.fail=False;self.assertEqual(self.clock.snapshot()["refresh_ms"],500)
    def test_bad_and_stale_responses_are_rejected(self):
        for path,value in (("schema",9),("snapshot_age_ms",5001),("device_id","wrong"),("dials",[])):
            Handler.sample=copy.deepcopy(SAMPLE);Handler.sample[path]=value
            with self.assertRaises(RuntimeError):self.clock.snapshot()
        Handler.sample=copy.deepcopy(SAMPLE);Handler.sample["dials"]["frequency"]["raw"]=4096
        with self.assertRaises(RuntimeError):self.clock.snapshot()
        Handler.oversize=True
        with self.assertRaises(RuntimeError):self.clock.snapshot()
        with self.assertRaises(ValueError):link.Clock(["http://not-a-host/"])
    def test_discovery_reuses_validated_ip_and_falls_back_on_identity_change(self):
        Handler.sample["ip"]="127.0.0.1"
        clock=link.Clock(["localhost"],self.server.server_port,timeout=.1)
        with mock.patch.object(clock,"request",wraps=clock.request) as requests:
            clock.snapshot();self.assertEqual(clock.last_host,"localhost")
            clock.snapshot();self.assertEqual(clock.last_host,"127.0.0.1")
            self.assertEqual([call.args[0] for call in requests.call_args_list],["localhost","127.0.0.1"])
        request=clock.request
        def changed_address(host,path,fields=None):
            if host=="127.0.0.1":
                other=copy.deepcopy(Handler.sample);other["device_id"]="abcdef123456";return other
            return request(host,path,fields)
        with mock.patch.object(clock,"request",side_effect=changed_address):
            clock.command("ota")
            self.assertEqual(clock.last_host,"localhost")
            self.assertEqual(clock.device_id,SAMPLE["device_id"])
            self.assertEqual(Handler.commands,["ota"])
        for address in ("http://other/", "8.8.8.8", "0.0.0.0"):
            Handler.sample["ip"]=address
            fresh=link.Clock(["localhost"],self.server.server_port,timeout=.1)
            fresh.snapshot();self.assertIsNone(fresh.cached_ip)
    def test_collection_labels_rotation_and_atomic_records(self):
        with tempfile.TemporaryDirectory() as folder:
            out=pathlib.Path(folder)
            link.collect(self.clock,out,.25,duration=.7)
            rows=[json.loads(line) for line in (out/"samples.jsonl").read_text().splitlines()]
            self.assertGreaterEqual(len(rows),3)
            self.assertTrue(json.loads((out/"connection.json").read_text())["connected"])
            self.assertEqual(json.loads((out/"latest.json").read_text())["telemetry"],SAMPLE)
            link.record(out,SAMPLE,"frequency-position-1")
            self.assertEqual(json.loads((out/"samples.jsonl").read_text().splitlines()[-1])["label"],"frequency-position-1")
            threads=[threading.Thread(target=link.record,args=(out,SAMPLE,str(i))) for i in range(10)]
            for thread in threads:thread.start()
            for thread in threads:thread.join()
            self.assertTrue(json.loads((out/"latest.json").read_text())["telemetry"])
            with (out/"samples.jsonl").open("a") as stream:stream.truncate(10*1024*1024+1)
            link.record(out,SAMPLE)
            self.assertTrue((out/"samples.previous.jsonl").exists())
            self.assertEqual(len((out/"samples.jsonl").read_text().splitlines()),1)
            self.assertNotIn("token",(out/"samples.jsonl").read_text())

if __name__=="__main__":
    unittest.main()
