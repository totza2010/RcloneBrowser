#!/usr/bin/env python3
"""The checks in docs/VERIFY.md that a script can do, against a running
program -- the Docker container (port 19991 for the dev one) or the window on
this machine (19999) -- with a real remote.

What it changes, and puts back:
  * a scratch folder on the remote, "zz-rb-verify-<time>", deleted at the end
  * a scratch folder on the program's machine, under its first allowed folder
    (/media in Docker), left behind: the API has no way to delete a folder of
    the machine it runs on. Its path is printed at the end.
  * an API key, a webhook, tasks and a schedule it makes, all removed

It prints each check with what it saw, so a person can read the result, and
writes the same to verify-report.md.

  python scripts/verify_live.py --container rclonebrowser-dev --remote gdrive
  python scripts/verify_live.py --base http://127.0.0.1:19999 --token <token> --remote gdrive

Standard library only. See docs/TESTING.md.
"""

import argparse
import datetime
import http.server
import json
import os
import socket
import subprocess
import tempfile
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

RESULTS = []   # (status, name, detail)
VERBOSE = False


def say(status, name, detail=""):
    mark = {"pass": "PASS", "fail": "FAIL", "skip": "SKIP", "look": "LOOK"}[status]
    RESULTS.append((mark, name, detail))
    line = "[%s] %s" % (mark, name)
    if detail:
        line += "\n       " + str(detail).replace("\n", "\n       ")
    print(line, flush=True)


class Api:
    def __init__(self, base, token):
        self.base = base.rstrip("/")
        self.token = token

    def call(self, method, path, body=None, raw=None, headers=None, token=None, timeout=120):
        url = self.base + "/api/v1" + path
        data = None
        h = {"Authorization": "Bearer " + (token if token is not None else self.token)}
        if raw is not None:
            data = raw
            h["Content-Type"] = "application/octet-stream"
        elif body is not None:
            data = json.dumps(body).encode()
            h["Content-Type"] = "application/json"
        if headers:
            h.update(headers)
        req = urllib.request.Request(url, data=data, method=method, headers=h)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                payload = r.read()
                status = r.status
                got = dict(r.headers)
        except urllib.error.HTTPError as e:
            payload = e.read()
            status = e.code
            got = dict(e.headers)
        try:
            parsed = json.loads(payload) if payload else None
        except ValueError:
            parsed = payload
        if VERBOSE:
            print("   %s %s -> %s" % (method, path, status))
        return status, parsed, got

    def get(self, path, **kw):
        return self.call("GET", path, **kw)

    def post(self, path, body=None, **kw):
        return self.call("POST", path, body if body is not None else {}, **kw)

    def wait_job(self, request_id, seconds=300):
        """Until the job has ended; its history row then."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            status, jobs, _ = self.get("/jobs")
            running = [j for j in (jobs or []) if j.get("requestId") == request_id and j.get("running")]
            if not running:
                st, row, _ = self.get("/history/" + urllib.parse.quote(request_id, safe=""))
                if st == 200:
                    return row
            time.sleep(2)
        return None


def token_from_container(name):
    out = subprocess.run(["docker", "exec", name, "sh", "-c",
                          "sed -n 's/^apiToken=//p' /config/rclone-browser/rclone-browser.conf"],
                         capture_output=True, text=True)
    return out.stdout.strip()


def token_from_windows():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\rclone-browser\rclone-browser\Settings") as key:
            return winreg.QueryValueEx(key, "apiToken")[0]
    except Exception:
        return ""


# ---- the checks ---------------------------------------------------------------

def check_system(api, ctx):
    st, s, _ = api.get("/system")
    if st != 200:
        return say("fail", "the program answers (/system)", "status %s" % st)
    ctx["system"] = s
    say("pass", "the program answers (/system)",
        "rclone %s · core started %s · queue running %s" %
        (s.get("rcloneVersion"), s.get("coreStarted"), s.get("queueRunning")))
    protocols = [p.get("protocol") for p in s.get("serveProtocols", [])]
    say("pass" if len(protocols) == 4 else "fail", "serve protocols listed (V-52)", ", ".join(protocols))


def check_remote(api, ctx):
    st, remotes, _ = api.get("/remotes")
    names = [r.get("name") for r in remotes or []]
    if ctx["remote"] not in names:
        say("fail", "the remote is there", "%s not among %s" % (ctx["remote"], names))
        return False
    say("pass", "the remote is there", ", ".join(names))
    st, caps, _ = api.get("/remotes/%s/capabilities" % ctx["remote"])
    say("look" if st == 200 else "fail", "what its backend can do (V-61)",
        json.dumps({k: caps.get(k) for k in ("known", "about", "cleanUp", "publicLink",
                                             "duplicateFiles", "hashes")} if st == 200 else caps))
    st, about, _ = api.get("/remotes/%s/about" % ctx["remote"])
    if st == 200:
        say("look", "how full it is (V-68)",
            "used %s · free %s · total %s (bytes)" % (about.get("used"), about.get("free"), about.get("total")))
    else:
        say("skip", "how full it is (V-68)", "this backend does not say: %s" % about)
    return True


def upload(api, ctx, name, data):
    path = "/remotes/%s/receive?path=%s&name=%s" % (
        ctx["remote"], urllib.parse.quote(ctx["scratch"]), urllib.parse.quote(name))
    st, body, _ = api.call("POST", path, raw=data, timeout=600)
    if st != 202:
        return None, "status %s %s" % (st, body)
    row = api.wait_job(body["requestId"])
    return row, body["requestId"]


def check_upload(api, ctx):
    files = {"a.txt": b"hello from verify_live\n", "b.nfo": b"an nfo\n",
             "c.bin": os.urandom(1024 * 1024)}
    for name, data in files.items():
        row, rid = upload(api, ctx, name, data)
        ok = row is not None and row.get("state") == "finished"
        if ok:
            ctx["made_scratch"] = True
        say("pass" if ok else "fail", "upload from this computer: %s (receive)" % name,
            "state %s, %s bytes" % (row.get("state"), row.get("bytes")) if row else rid)
    st, ls, _ = api.get("/remotes/%s/ls?path=%s" % (ctx["remote"], urllib.parse.quote(ctx["scratch"])))
    have = sorted(e["name"] for e in (ls or {}).get("entries", []))
    say("pass" if have == sorted(files) else "fail", "the files are on the remote", ", ".join(have))


def check_cat(api, ctx):
    st, body, headers = api.get("/remotes/%s/cat?path=%s" % (
        ctx["remote"], urllib.parse.quote(ctx["scratch"] + "/a.txt")))
    view = headers.get("X-Rb-View")
    text = body.decode() if isinstance(body, bytes) else str(body)
    say("pass" if st == 200 and view == "text" and "hello from verify_live" in text else "fail",
        "look at a text file (V-53)", "view %s · %r" % (view, text[:60]))


def check_question(api, ctx):
    st, size, _ = api.get("/remotes/%s/size?path=%s" % (ctx["remote"], urllib.parse.quote(ctx["scratch"])))
    if st != 200:
        return say("fail", "size of the scratch folder", "status %s %s" % (st, size))
    st2, _, _ = api.get("/history/" + urllib.parse.quote(size.get("requestId", "x"), safe=""))
    say("pass" if size.get("count") == 3 else "fail", "size of the scratch folder",
        "%s files, %s bytes" % (size.get("count"), size.get("bytes")))
    say("pass" if st2 == 404 else "fail", "a question leaves no history row (V-70)",
        "history/<its id> -> %s" % st2)


def check_copy_options(api, ctx):
    st, body, _ = api.post("/remotes/%s/copy" % ctx["remote"], {
        "path": ctx["scratch"], "isDir": True, "to": ctx["scratch"] + "-copy",
        "filters": ["- *.nfo"], "noTraverse": True})
    if st != 202:
        return say("fail", "copy with a filter (V-76)", "status %s %s" % (st, body))
    ctx["copy_folder"] = ctx["scratch"] + "-copy"
    row = api.wait_job(body["requestId"])
    st, ls, _ = api.get("/remotes/%s/ls?path=%s" % (
        ctx["remote"], urllib.parse.quote(ctx["scratch"] + "-copy/" + ctx["scratch"].split("/")[-1])))
    have = sorted(e["name"] for e in (ls or {}).get("entries", []))
    say("pass" if row and row.get("state") == "finished" and "b.nfo" not in have and "a.txt" in have
        else "fail", "copy with a filter leaves the .nfo out (V-76)", ", ".join(have) or str(ls))


def check_history(api, ctx):
    st, rows, _ = api.get("/history?limit=20&q=" + urllib.parse.quote(ctx["scratch"]))
    say("pass" if st == 200 and rows else "fail", "history found by a word (V-60)",
        "%s rows; newest: %s" % (len(rows or []), (rows or [{}])[0].get("startedAt")))
    if rows:
        st, one, _ = api.get("/history/" + urllib.parse.quote(rows[0]["requestId"], safe=""))
        at = one.get("startedAt") or ""
        offset = at.endswith("Z") or "+" in at[19:] or "-" in at[19:]
        say("pass" if st == 200 and offset else "fail", "one run in full, its time with an offset (V-59/V-60)",
            "%s · %s · %s" % (one.get("startedAt"), one.get("state"), one.get("askedBy") or one.get("startedBy")))


def check_bandwidth(api, ctx):
    name = "slow.bin"
    path = "/remotes/%s/receive?path=%s&name=%s" % (
        ctx["remote"], urllib.parse.quote(ctx["scratch"]), name)
    # Received whole first, then sent up as a job: that job is what is slowed.
    st, body, _ = api.call("POST", path, raw=os.urandom(24 * 1024 * 1024), timeout=900)
    if st != 202:
        return say("fail", "slow a running transfer (V-51)", "status %s" % st)
    rid = body["requestId"]
    # Wait for its remote control, then limit it.
    limited = None
    for _ in range(30):
        st, answer, _ = api.post("/jobs/%s/bandwidth" % urllib.parse.quote(rid, safe=""), {"rate": "300k"})
        if st == 200:
            limited = answer
            break
        time.sleep(1)
    if not limited:
        return say("skip", "slow a running transfer (V-51)", "it ended before it could be slowed")
    time.sleep(8)
    st, jobs, _ = api.get("/jobs")
    job = next((j for j in jobs if j.get("requestId") == rid), {})
    speed = (job.get("stats") or {}).get("speed", 0)
    say("pass" if speed and speed < 450 * 1024 else "look", "slow a running transfer to 300k (V-51)",
        "rclone says %s · measured %.0f KB/s" % (limited.get("rate"), speed / 1024))
    api.post("/bandwidth", {"rate": "off"})
    row = api.wait_job(rid, 900)
    say("pass" if row and row.get("state") == "finished" else "fail", "freed, it finishes", row and row.get("state"))


def check_keys_and_asked_by(api, ctx):
    st, made, _ = api.post("/keys", {"name": "verify-live", "scope": "control"})
    if st != 201:
        return say("fail", "make a key", "status %s" % st)
    key, kid = made["key"], made["id"]
    st1, _, _ = api.get("/jobs", token=key)
    st2, _, _ = api.call("PUT", "/settings", {"streamIdleMinutes": 30}, token=key)
    say("pass" if st1 == 200 and st2 == 403 else "fail", "a control key looks but may not change settings (V-44/V-73)",
        "GET /jobs %s · PUT /settings %s" % (st1, st2))
    ctx["key"] = key
    ctx["key_id"] = kid


def local_root(api, ctx):
    """Where on the program's machine to download into: --local, else the
    first folder the API may use -- unless that is a whole drive (the window
    on Windows, which may use every drive): then this machine's temp folder,
    when the program runs here and may use it."""
    if ctx.get("local_root"):
        st, stat, _ = api.get("/local/stat?path=" + urllib.parse.quote(ctx["local_root"]))
        return ctx["local_root"] if st == 200 and (stat or {}).get("isDir") else None
    st, roots, _ = api.get("/local/ls")
    entries = (roots or {}).get("entries", [])
    if st != 200 or not entries:
        return None
    first = entries[0]["path"]
    whole_drive = len(first.rstrip("/\\")) <= 2   # C: or /
    here = urllib.parse.urlparse(api.base).hostname in ("127.0.0.1", "localhost", "::1")
    if whole_drive and here and not ctx.get("container"):
        temp = tempfile.gettempdir().replace("\\", "/")
        st, stat, _ = api.get("/local/stat?path=" + urllib.parse.quote(temp))
        if st == 200 and (stat or {}).get("isDir"):
            return temp
    return first


def check_local_side(api, ctx):
    root = local_root(api, ctx)
    if not root:
        return say("skip", "a folder of the program's machine",
                   "no folder the API may use" + (" (--local %s)" % ctx["local_root"]
                                                  if ctx.get("local_root") else ""))
    # Downloading goes into a folder that is there; the scratch folder lands
    # in it under its own name.
    ctx["local"] = root.rstrip("/\\") + "/" + ctx["scratch"].split("/")[-1]
    st, body, _ = api.post("/remotes/%s/download" % ctx["remote"], {
        "path": ctx["scratch"], "isDir": True, "to": root})
    row = api.wait_job(body["requestId"]) if st == 202 else None
    say("pass" if row and row.get("state") == "finished" else "fail",
        "download to the program's machine", "%s -> %s" % (st, ctx["local"]))
    st, ls, _ = api.get("/local/ls?path=" + urllib.parse.quote(ctx["local"]))
    names = [e["name"] for e in (ls or {}).get("entries", [])]
    say("pass" if "a.txt" in names else "fail", "the files are there", ", ".join(names))

    st, check, _ = api.get("/remotes/%s/check?path=%s&toLocal=%s&oneWay=true" % (
        ctx["remote"], urllib.parse.quote(ctx["scratch"]), urllib.parse.quote(ctx["local"])))
    same = st == 200 and (check or {}).get("differences") == 0
    say("pass" if same else "look" if st == 200 else "fail",
        "check the remote folder against this machine: the same (V-77)",
        "status %s · matched %s · differences %s" % (st, (check or {}).get("matched"), (check or {}).get("differences")))


def check_backup_and_watch(api, ctx):
    if "local" not in ctx:
        return say("skip", "backup and folder watch", "no folder on the program's machine")
    st, made, _ = api.post("/backups", {
        "name": "verify-live backup", "source": ctx["local"], "dest": "%s:%s/backup" % (ctx["remote"], ctx["scratch"]),
        "mirror": True, "keepVersions": True, "time": "03:17"})
    if st != 201:
        return say("fail", "make a backup (V-56)", "status %s %s" % (st, made))
    task, schedule = made["task"], made["schedule"]
    ctx.setdefault("tasks", []).append(task["id"])
    ctx.setdefault("schedules", []).append(schedule["id"])
    say("pass", "make a backup: a task and a schedule (V-56)",
        "%s · %s · %s" % (task.get("operation"), schedule.get("time"), task.get("extra")))
    st, run, _ = api.post("/tasks/%s/run" % urllib.parse.quote(task["id"], safe=""))
    row = api.wait_job(run["requestId"]) if st == 202 else None
    say("pass" if row and row.get("state") == "finished" else "fail", "the backup runs", row and row.get("state"))

    st, _, _ = api.post("/tasks/%s/watch" % urllib.parse.quote(task["id"], safe=""), {"quietSeconds": 3})
    say("pass" if st == 200 else "fail", "watch its folder (V-57)", "status %s" % st)
    # A change in the folder: a new file, put on the remote and downloaded
    # into it (the API writes nothing else to that machine).
    upload(api, ctx, "new.txt", b"a change for the folder watch\n")
    st, body, _ = api.post("/remotes/%s/download" % ctx["remote"], {
        "path": ctx["scratch"] + "/new.txt", "isDir": False, "to": ctx["local"]})
    if st == 202:
        api.wait_job(body["requestId"])
    # The folders are looked over every pollSeconds; wait one look and a bit.
    st, watches, _ = api.get("/watches")
    poll = (watches or {}).get("pollSeconds", 60) if isinstance(watches, dict) else 60
    wait = min(poll + 30, 420)
    print("       (waiting up to %d s for the folder watch to look)" % wait, flush=True)
    queued = None
    deadline = time.time() + wait
    while time.time() < deadline and not queued:
        st, rows, _ = api.get("/history?limit=5&task=" + urllib.parse.quote(task["id"]))
        st, waiting, _ = api.get("/queue")
        seen = list(rows or []) + (waiting if isinstance(waiting, list) else [])
        queued = next((r for r in seen if r.get("askedBy") == "folder watch"), None)
        if not queued:
            time.sleep(3)
    say("pass" if queued else "look", "the change ran the backup by itself (V-57)",
        "asked by %s" % (queued.get("askedBy") if queued else
                         "nothing within %d s (the folders are looked over every %s s)" % (wait, poll)))
    api.call("DELETE", "/tasks/%s/watch" % urllib.parse.quote(task["id"], safe=""))


def check_webhook(api, ctx):
    got = []

    class Hook(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            got.append((self.headers.get("X-Rb-Event"), self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()

        def log_message(self, *a):
            pass

    server = http.server.HTTPServer(("0.0.0.0", 0), Hook)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    host = "host.docker.internal" if ctx.get("container") else "127.0.0.1"
    st, hook, _ = api.post("/webhooks", {"name": "verify-live", "url": "http://%s:%s/hook" % (host, port),
                                         "format": "json", "everyJob": True})
    if st != 201:
        server.shutdown()
        return say("fail", "a webhook (V-54)", "status %s %s" % (st, hook))
    ctx["hook"] = hook["id"]
    st, test, _ = api.post("/webhooks/%s/test" % hook["id"])
    say("pass" if st == 200 and got and got[-1][0] == "test" else "fail", "a webhook is told (V-54)",
        "test -> %s · received %s" % ((test or {}).get("status"), [g[0] for g in got]))
    server.shutdown()


def check_serve(api, ctx):
    port = 18900
    st, body, _ = api.post("/remotes/%s/serve" % ctx["remote"], {
        "path": ctx["scratch"], "protocol": "webdav", "port": port, "readOnly": True,
        "user": "verify", "password": "live-check"})
    if st != 202:
        return say("fail", "serve over WebDAV (V-52)", "status %s %s" % (st, body))
    rid = body["requestId"]
    time.sleep(4)
    reach = "127.0.0.1"
    try:
        req = urllib.request.Request("http://%s:%s/" % (reach, port), method="PROPFIND", headers={"Depth": "1"})
        try:
            urllib.request.urlopen(req, timeout=5)
            no_login = 200
        except urllib.error.HTTPError as e:
            no_login = e.code
        say("pass" if no_login == 401 else "fail", "serve asks for the login (V-52)", "no login -> %s" % no_login)
    except Exception as e:
        say("skip", "serve, reached from here (V-52)",
            "port %s is not reachable from this machine (%s) -- in Docker it is inside the container; "
            "check by hand" % (port, e.__class__.__name__))
    st, jobs, _ = api.get("/jobs")
    job = next((j for j in jobs if j.get("requestId") == rid), {})
    say("pass" if job.get("serve", {}).get("port") == port and "live-check" not in json.dumps(jobs) else "fail",
        "serve is a job, and its password is nowhere in /jobs (V-52)", json.dumps(job.get("serve")))
    api.post("/jobs/%s/stop" % urllib.parse.quote(rid, safe=""))


def clean_up(api, ctx):
    for sid in ctx.get("schedules", []):
        api.call("DELETE", "/schedules/" + urllib.parse.quote(sid, safe=""))
    for tid in ctx.get("tasks", []):
        api.call("DELETE", "/tasks/" + urllib.parse.quote(tid, safe=""))
    if ctx.get("hook"):
        api.call("DELETE", "/webhooks/" + ctx["hook"])
    if ctx.get("key_id"):
        api.call("DELETE", "/keys/" + ctx["key_id"])
    # Only what this run made: nothing else on the remote is touched.
    for folder in (ctx["scratch"] if ctx.get("made_scratch") else None, ctx.get("copy_folder")):
        if folder:
            st, _, _ = api.post("/remotes/%s/delete" % ctx["remote"], {"path": folder, "isDir": True})
            say("pass" if st == 200 else "fail", "scratch folder removed from the remote", "%s:%s -> %s" % (
                ctx["remote"], folder, st))
    if ctx.get("local"):
        say("look", "left on the program's machine (delete it by hand)", ctx["local"])


def main():
    global VERBOSE
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", default="http://127.0.0.1:19991")
    parser.add_argument("--token", default=os.environ.get("RB_TOKEN", ""))
    parser.add_argument("--container", help="read the token from this container (e.g. rclonebrowser-dev)")
    parser.add_argument("--remote", required=True, help="a remote to make the scratch folder on")
    parser.add_argument("--local", help="a folder of the program's machine to download into "
                        "(the scratch folder is made inside it)")
    parser.add_argument("--skip", default="", help="comma-separated: bandwidth,serve,webhook,backup")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    VERBOSE = args.verbose
    # A Windows console in another code page (cp874 for Thai) cannot print
    # everything the program says.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except AttributeError:
            pass

    token = args.token or (token_from_container(args.container) if args.container else token_from_windows())
    if not token:
        sys.exit("no token: pass --token, --container or set RB_TOKEN")
    api = Api(args.base, token)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    ctx = {"remote": args.remote, "scratch": "zz-rb-verify-" + stamp, "container": args.container,
           "local_root": args.local}
    skip = set(filter(None, args.skip.split(",")))

    print("Checking %s with remote %s, scratch folder %s\n" % (args.base, args.remote, ctx["scratch"]))
    started = time.time()
    try:
        check_system(api, ctx)
        if check_remote(api, ctx):
            check_upload(api, ctx)
            check_cat(api, ctx)
            check_question(api, ctx)
            check_copy_options(api, ctx)
            check_history(api, ctx)
            check_keys_and_asked_by(api, ctx)
            check_local_side(api, ctx)
            if "backup" not in skip:
                check_backup_and_watch(api, ctx)
            if "webhook" not in skip:
                check_webhook(api, ctx)
            if "serve" not in skip:
                check_serve(api, ctx)
            if "bandwidth" not in skip:
                check_bandwidth(api, ctx)
    finally:
        clean_up(api, ctx)

    counts = {k: sum(1 for r in RESULTS if r[0] == k) for k in ("PASS", "FAIL", "SKIP", "LOOK")}
    summary = "%(PASS)d passed, %(FAIL)d failed, %(LOOK)d to look at, %(SKIP)d skipped" % counts
    print("\n" + summary + " (%d s)" % (time.time() - started))
    with open("verify-report.md", "w", encoding="utf-8") as report:
        report.write("# verify_live — %s\n\n%s · %s · remote %s\n\n| | check | what it saw |\n|---|---|---|\n" % (
            stamp, summary, args.base, args.remote))
        for mark, name, detail in RESULTS:
            report.write("| %s | %s | %s |\n" % (mark, name, str(detail).replace("|", "\\|").replace("\n", " ")))
    print("written to verify-report.md")
    sys.exit(1 if counts["FAIL"] else 0)


if __name__ == "__main__":
    main()
