#!/usr/bin/env python3
"""The web page, clicked as a person would, in a real browser.

scripts/verify_live.py proves the API; this proves the page in front of it.
It is what would have caught every confirm on the page failing with "no such
endpoint" while every other test passed (docs/WEB-API-PLAN.md, 2026-10-20).

A step fails on anything a person would see go wrong, as well as on what it
checks for itself:
  * the error bar (#toast) or an error in the result box
  * an /api/ request answered 4xx/5xx that the step did not expect
  * a JavaScript error, or an error in the browser console

It uses the Chrome already on this machine (Playwright's "chrome" channel),
so nothing else is downloaded. Playwright is a test dependency only:

  python -m venv tests/ui/.venv
  tests/ui/.venv/Scripts/python -m pip install -r tests/ui/requirements.txt

  tests/ui/.venv/Scripts/python tests/ui/check_ui.py --container rclonebrowser-dev --remote gdrive
  tests/ui/.venv/Scripts/python tests/ui/check_ui.py --base http://127.0.0.1:19999 --remote gdrive

What it changes, and puts back: a scratch folder "zz-rb-ui-<time>" on the
remote, a task it makes and queues, and whether the queue is running. See
docs/TESTING.md.
"""

import argparse
import datetime
import os
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import verify_live  # noqa: E402  the API client, the token readers and the report
from verify_live import Api, say, RESULTS  # noqa: E402

from playwright.sync_api import sync_playwright, TimeoutError as PlaywrightTimeout  # noqa: E402

WAIT = 20000  # ms: what rclone may take to answer a question


class Watch:
    """What went wrong on the page since the last look."""

    def __init__(self, page):
        self.problems = []
        self.allowed = set()   # (method, path part) a step expects to be refused
        page.on("pageerror", lambda e: self.problems.append("JavaScript error: %s" % e))
        page.on("console", self._console)
        page.on("response", self._response)
        # Asked and not yet answered: when a step waits in vain, these are
        # usually why.
        self.pending = {}
        page.on("request", lambda r: self.pending.__setitem__(id(r), r))
        page.on("requestfinished", lambda r: self.pending.pop(id(r), None))
        page.on("requestfailed", lambda r: self.pending.pop(id(r), None))

    def waiting(self):
        found = []
        for r in self.pending.values():
            path = urllib.parse.urlparse(r.url).path
            if "/api/v1/" in path and not path.endswith("/events"):
                found.append("%s %s" % (r.method, path))
        return found

    def _console(self, message):
        # A refused request is reported by the response itself, with whether
        # it was expected; the console's "Failed to load resource" line for
        # it says nothing more.
        if message.type == "error" and "Failed to load resource" not in message.text:
            self.problems.append("console: %s" % message.text)

    def _response(self, response):
        url = urllib.parse.urlparse(response.url)
        if "/api/v1/" not in url.path or response.status < 400:
            return
        method = response.request.method
        if any(method == m and part in url.path for m, part in self.allowed):
            return
        try:
            body = response.text()[:120]
        except Exception:
            body = ""
        self.problems.append("%s %s -> %s %s" % (method, url.path + ("?" + url.query if url.query else ""),
                                                 response.status, body))

    def take(self):
        found, self.problems = self.problems, []
        return found


def shown(page, selector):
    return page.locator(selector).is_visible()


def on_screen_errors(page, toast_ok=False):
    found = []
    if shown(page, "#toast") and not toast_ok:
        found.append("error bar: " + page.locator("#toast").inner_text().strip())
    if shown(page, "#resultbox"):
        for text in page.locator("#result-body .error").all_inner_texts():
            if text.strip():
                found.append("result box: " + text.strip())
    return found


def step(page, watch, name, action, allowed=(), toast_ok=False):
    """One thing a person does. PASS when it said so itself and nothing else
    went wrong on the way. toast_ok: the step ends with a message in the bar
    that is not a refusal ("webhook added")."""
    watch.take()
    watch.allowed = set(allowed)
    detail = ""
    try:
        detail = action() or ""
        page.wait_for_timeout(300)   # what the click set going settles
    except PlaywrightTimeout as e:
        watch.problems.append("waited too long: %s" % str(e).split("\n")[0])
    except AssertionError as e:
        watch.problems.append(str(e))
    said = page.locator("#toast").inner_text().strip() if shown(page, "#toast") else ""
    problems = watch.take() + on_screen_errors(page, toast_ok)
    watch.allowed = set()
    if toast_ok and said:
        detail = (detail + " · " if detail else "") + "said: " + said
    if problems and watch.waiting():
        problems.append("still unanswered: " + ", ".join(watch.waiting()))
    if problems:
        # What the page looked like, for whoever reads the report.
        os.makedirs("ui-failures", exist_ok=True)
        shot = os.path.join("ui-failures", "".join(c if c.isalnum() else "-" for c in name)[:60] + ".png")
        page.screenshot(path=shot)
        problems.append("picture: " + shot)
    say("fail" if problems else "pass", name, "\n".join(problems) if problems else detail)
    # Whatever is left open is closed, so the next step starts clean.
    for close in ("#ask-no", "#result-close"):
        if shown(page, close):
            page.click(close)
    page.evaluate("document.getElementById('toast').classList.add('hidden')")


def answer(page, title_has, value=None, yes=True):
    """The page's own question box: it is there, it asks what it should, and
    it is answered."""
    page.wait_for_selector("#askbox:not(.hidden)", timeout=5000)
    title = page.locator("#ask-title").inner_text()
    assert title_has.lower() in title.lower(), "asked %r, expected %r" % (title, title_has)
    if value is not None:
        page.fill("#ask-value", value)
    page.click("#ask-yes" if yes else "#ask-no")
    page.wait_for_selector("#askbox.hidden", state="attached", timeout=5000)
    return title


def rows(page, selector):
    return page.locator(selector).count()


def entry_row(page, path):
    return page.locator('#listing tr[data-entry="%s"]' % path)


def pick(page, path):
    # Clicking the row (not its name, which opens a folder) chooses it -- and
    # a second click on the only one chosen lets it go, so not when it is.
    row = entry_row(page, path)
    if not row.locator("input[data-pick]").is_checked():
        row.locator("td").last.click()


# ---- the steps ------------------------------------------------------------------

def check_tabs(page, watch):
    for name in ("jobs", "queue", "tasks", "schedules", "history", "api", "remotes"):
        def go(name=name):
            page.click('#tabs button[data-tab="%s"]' % name)
            page.wait_for_selector("#tab-%s.on" % name, timeout=5000)
            assert page.url.endswith("#/" + name) or name == "remotes", "address is " + page.url
            return "address " + page.url.split("#", 1)[-1]
        step(page, watch, "tab %s opens" % name, go)


def check_queue(page, watch, api, ctx):
    task = ctx["task"]
    for _ in range(3):
        api.post("/tasks/%s/enqueue" % urllib.parse.quote(task, safe=""))

    def open_queue():
        page.click('#tabs button[data-tab="queue"]')
        page.wait_for_selector("#tab-queue [data-unqueue]", timeout=10000)
        n = rows(page, "#tab-queue [data-unqueue]")
        assert n == 3, "%d rows to remove, expected 3" % n
        return "3 waiting, each with Remove"
    step(page, watch, "queue lists what waits", open_queue)

    def remove_one():
        page.locator("#tab-queue [data-unqueue]").first.click()
        page.wait_for_function("document.querySelectorAll('#tab-queue [data-unqueue]').length === 2",
                               timeout=10000)
        return "2 left"
    step(page, watch, "queue: Remove one", remove_one)

    def move_one():
        page.locator('#tab-queue [data-queue-move][data-to="up"]:not([disabled])').first.click()
        page.wait_for_timeout(500)
        return "moved up"
    step(page, watch, "queue: move one up", move_one)

    def clear_but_cancel():
        page.click("#tab-queue [data-queue-clear]")
        answer(page, "Take all", yes=False)
        page.wait_for_timeout(500)
        n = rows(page, "#tab-queue [data-unqueue]")
        assert n == 2, "Cancel left %d, expected 2" % n
        return "asked, Cancel kept both"
    step(page, watch, "queue: Clear, then Cancel (V-80)", clear_but_cancel)

    def clear():
        page.click("#tab-queue [data-queue-clear]")
        asked = answer(page, "Take all")
        page.wait_for_selector("#tab-queue p.empty", timeout=10000)
        st, waiting, _ = api.get("/queue")
        assert waiting == [], "the daemon still has %s" % waiting
        return "asked %r · empty on the page and in the daemon" % asked
    step(page, watch, "queue: Clear the queue (V-80)", clear)


def check_remote(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]

    def open_folder():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        entry_row(page, scratch + "/a.txt").wait_for(timeout=WAIT)
        return "a.txt listed"
    step(page, watch, "remote: open the scratch folder from its address (V-66)", open_folder)

    def mkdir():
        page.click("#remote-mkdir")
        answer(page, "Create folder", value="made-by-ui")
        entry_row(page, scratch + "/made-by-ui").wait_for(timeout=WAIT)
        return "made-by-ui listed"
    step(page, watch, "remote: New folder", mkdir)

    def rename():
        pick(page, scratch + "/made-by-ui")
        page.click("#remote-rename")
        answer(page, "New name", value="renamed-by-ui")
        entry_row(page, scratch + "/renamed-by-ui").wait_for(timeout=WAIT)
        return "renamed-by-ui listed"
    step(page, watch, "remote: Rename", rename)

    for button, title in (("#remote-size", "Size of"), ("#remote-tree", "Folders under"),
                          ("#remote-about", "About")):
        def question(button=button, title=title):
            page.click(button)
            page.wait_for_selector("#resultbox:not(.hidden)", timeout=5000)
            heading = page.locator("#result-title").inner_text()
            assert heading.startswith(title), "the box says %r" % heading
            page.wait_for_function(
                "!document.getElementById('result-body').innerText.includes('asking rclone')", timeout=WAIT)
            return "%s · %s" % (heading, page.locator("#result-body").inner_text().strip()[:80].replace("\n", " "))
        step(page, watch, "remote: %s (rclone's answer in the box)" % title.split()[0], question)

    def size_cancel():
        page.click("#remote-size")
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=5000)
        cancel = page.locator("#result-body [data-ask-cancel]")
        if cancel.count():
            cancel.click()
            page.wait_for_selector("#resultbox.hidden", state="attached", timeout=5000)
            return "taken back"
        return "answered before Cancel could be pressed"
    step(page, watch, "remote: Size, then Cancel (V-68)", size_cancel)

    def delete_cancel():
        pick(page, scratch + "/renamed-by-ui")
        page.click("#remote-delete")
        answer(page, "Delete the folder", yes=False)
        page.wait_for_timeout(500)
        assert entry_row(page, scratch + "/renamed-by-ui").count() == 1, "gone after Cancel"
        return "asked, Cancel kept it"
    step(page, watch, "remote: Delete, then Cancel", delete_cancel)

    def delete():
        pick(page, scratch + "/renamed-by-ui")
        page.click("#remote-delete")
        answer(page, "Delete the folder")
        entry_row(page, scratch + "/renamed-by-ui").wait_for(state="detached", timeout=WAIT)
        return "gone from the listing"
    step(page, watch, "remote: Delete", delete)

    def menu():
        entry_row(page, scratch + "/a.txt").locator("td").last.click(button="right")
        page.wait_for_selector("#row-menu:not(.hidden)", timeout=5000)
        items = page.locator("#row-menu button").all_inner_texts()
        page.keyboard.press("Escape")
        assert any("Delete" in i for i in items), "no Delete in %s" % items
        return ", ".join(i.strip() for i in items)
    step(page, watch, "remote: right-click menu (V-64)", menu)

    def view():
        pick(page, scratch + "/a.txt")
        page.click("#remote-view")
        page.wait_for_selector("#resultbox:not(.hidden) .viewer-text", timeout=WAIT)
        text = page.locator("#result-body .viewer-text").inner_text()
        assert "hello from" in text, "shows %r" % text[:60]
        return repr(text[:40])
    step(page, watch, "remote: View a text file (V-53)", view)


def check_many(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]
    for name in ("many-1", "many-2"):
        st, body, _ = api.post("/remotes/%s/mkdir" % remote, {"path": scratch, "name": name})
        if st != 200:
            say("fail", "remote: make %s to choose" % name, "%s %s" % (st, body))

    def delete_two():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        # Made behind the page's back: it shows them once asked to look again.
        page.click("#remote-refresh")
        for name in ("many-1", "many-2"):
            entry_row(page, scratch + "/" + name).wait_for(timeout=WAIT)
        for name in ("many-1", "many-2"):
            box = entry_row(page, scratch + "/" + name).locator("input[data-pick]")
            if not box.is_checked():
                box.click()
        page.click("#remote-delete")
        asked = answer(page, "Delete 2 items")
        for name in ("many-1", "many-2"):
            entry_row(page, scratch + "/" + name).wait_for(state="detached", timeout=WAIT)
        return "asked %r · both gone" % asked
    step(page, watch, "remote: choose two, Delete both (V-64)", delete_two, toast_ok=True)


def check_api_tab(page, watch, api, ctx):
    def open_api():
        page.click('#tabs button[data-tab="api"]')
        page.wait_for_selector("#key-form", timeout=10000)
        page.wait_for_selector("#hook-form", timeout=10000)
        return "keys and webhooks shown"
    step(page, watch, "API tab: keys and webhooks are there", open_api)

    def make_key():
        page.fill("#key-name", "ui check key")
        page.select_option("#key-scope", "read")
        page.locator("#key-form button[type=submit]").click()
        page.wait_for_selector("#key-made", timeout=10000)
        key = page.input_value("#key-made")
        assert len(key) > 20, "the key shown is %r" % key
        page.locator('button[data-key-name="ui check key"]').wait_for(timeout=5000)
        return "a key of %d characters, listed as read" % len(key)
    step(page, watch, "API tab: make a key (V-44)", make_key)

    def take_back_key():
        drop = page.locator('button[data-key-name="ui check key"]')
        drop.click()
        answer(page, "Take back", yes=False)
        page.wait_for_timeout(300)
        assert drop.count() == 1, "gone after Cancel"
        drop.click()
        answer(page, "Take back")
        drop.wait_for(state="detached", timeout=10000)
        return "Cancel kept it, Yes took it back"
    step(page, watch, "API tab: take a key back, asked first", take_back_key)

    def add_hook():
        page.fill("#hook-url", "http://127.0.0.1:9/ui-check")
        page.fill("#hook-name", "ui check hook")
        page.locator("#hook-form button[type=submit]").click()
        page.locator('[data-hook-name="ui check hook"][data-hook-drop]').wait_for(timeout=10000)
        return "listed"
    step(page, watch, "API tab: add a webhook (V-54)", add_hook, toast_ok=True)

    def drop_hook():
        drop = page.locator('[data-hook-name="ui check hook"][data-hook-drop]')
        drop.click()
        answer(page, "Stop telling", yes=False)
        page.wait_for_timeout(300)
        assert drop.count() == 1, "gone after Cancel"
        drop.click()
        answer(page, "Stop telling")
        drop.wait_for(state="detached", timeout=10000)
        return "Cancel kept it, Yes removed it"
    step(page, watch, "API tab: remove a webhook, asked first", drop_hook)


def check_backup_and_schedule(page, watch, api, ctx):
    name = "ui check backup " + ctx["scratch"][-6:]

    def make_backup():
        page.click('#tabs button[data-tab="tasks"]')
        page.click("#tab-tasks [data-new-backup]")
        page.wait_for_selector("#backup-go", timeout=5000)
        page.fill("#backup-source", ctx["local"])
        page.fill("#backup-dest", "%s:%s/backup" % (ctx["remote"], ctx["scratch"]))
        page.fill("#backup-name", name)
        page.click("#backup-go")
        page.wait_for_selector("#resultbox.hidden", state="attached", timeout=10000)
        st, tasks, _ = api.get("/tasks")
        made = [t for t in tasks or [] if t.get("name") == name]
        assert made, "no task called %r" % name
        ctx["backup_task"] = made[0]["id"]
        st, schedules, _ = api.get("/schedules")
        mine = [x for x in schedules or [] if x.get("taskId") == made[0]["id"]]
        assert mine, "no schedule for it"
        ctx["backup_schedule"] = mine[0]["id"]
        return "a task and a schedule at %s" % mine[0].get("time")
    step(page, watch, "Tasks: New backup makes a task and a schedule (V-56)", make_backup, toast_ok=True)

    if not ctx.get("backup_schedule"):
        return

    def switch():
        page.click('#tabs button[data-tab="schedules"]')
        button = page.locator('[data-sched-switch="%s"]' % ctx["backup_schedule"])
        button.wait_for(timeout=10000)
        before = api.get("/schedules/" + urllib.parse.quote(ctx["backup_schedule"], safe=""))[1] or {}
        if not isinstance(before, dict) or "active" not in before:
            before = next(x for x in api.get("/schedules")[1] if x["id"] == ctx["backup_schedule"])
        button.click()
        page.wait_for_timeout(800)
        after = next(x for x in api.get("/schedules")[1] if x["id"] == ctx["backup_schedule"])
        assert after["active"] != before["active"], "still %s" % after["active"]
        return "switched %s → %s" % ("on" if before["active"] else "off", "on" if after["active"] else "off")
    step(page, watch, "Schedules: switch one off and on (V-41)", switch, allowed={("GET", "/schedules/")})

    def drop_schedule():
        drop = page.locator('[data-sched-drop="%s"]' % ctx["backup_schedule"])
        drop.click()
        answer(page, "Delete the schedule", yes=False)
        page.wait_for_timeout(300)
        assert drop.count() == 1, "gone after Cancel"
        drop.click()
        answer(page, "Delete the schedule")
        drop.wait_for(state="detached", timeout=10000)
        ctx["backup_schedule"] = None
        return "Cancel kept it, Yes deleted it"
    step(page, watch, "Schedules: delete one, asked first", drop_schedule)


def check_tasks(page, watch, api, ctx):
    def forget():
        page.click('#tabs button[data-tab="tasks"]')
        button = page.locator('#tab-tasks [data-forget="%s"]' % ctx["task"])
        button.wait_for(timeout=10000)
        button.click()
        answer(page, "Delete the task")
        button.wait_for(state="detached", timeout=10000)
        st, _, _ = api.get("/tasks/" + urllib.parse.quote(ctx["task"], safe=""))
        assert st == 404, "the daemon still has it (%s)" % st
        ctx["task_gone"] = True
        return "asked, and gone from the page and the daemon"
    step(page, watch, "tasks: Delete a task", forget)


def check_phone(page, watch, ctx):
    page.set_viewport_size({"width": 375, "height": 800})
    for name in ("remotes", "jobs", "queue", "tasks", "schedules", "history"):
        def fits(name=name):
            page.click('#tabs button[data-tab="%s"]' % name)
            page.wait_for_selector("#tab-%s.on" % name, timeout=5000)
            wide = page.evaluate("document.documentElement.scrollWidth")
            assert wide <= 376, "the page is %d px wide on a 375 px screen" % wide
            return "%d px" % wide
        step(page, watch, "phone width: %s fits (V-69)" % name, fits)
    page.set_viewport_size({"width": 1366, "height": 900})


# ---- around it ------------------------------------------------------------------

def set_up(api, ctx):
    path = "/remotes/%s/receive?path=%s&name=a.txt" % (ctx["remote"], urllib.parse.quote(ctx["scratch"]))
    st, body, _ = api.call("POST", path, raw=b"hello from check_ui\n")
    if st != 202 or not (api.wait_job(body["requestId"]) or {}).get("state") == "finished":
        sys.exit("could not put a file on %s: %s %s" % (ctx["remote"], st, body))
    ctx["made_scratch"] = True

    st, system, _ = api.get("/system")
    ctx["queue_was_running"] = system.get("queueRunning")
    api.post("/queue/pause")    # so what is queued waits to be looked at

    st, task, _ = api.post("/tasks", {
        "type": "download", "name": "zz ui check (never runs)",
        "source": "%s:%s/nothing" % (ctx["remote"], ctx["scratch"]), "dest": ctx["local"], "operation": "copy"})
    if st != 201:
        sys.exit("could not make a task: %s %s" % (st, task))
    ctx["task"] = task["id"]


def clean_up(api, ctx):
    if ctx.get("task") and not ctx.get("task_gone"):
        for entry in (api.get("/queue")[1] or []):
            if entry.get("taskId") == ctx["task"]:
                api.call("DELETE", "/queue/" + urllib.parse.quote(entry["requestId"], safe=""))
        api.call("DELETE", "/tasks/" + urllib.parse.quote(ctx["task"], safe=""))
    if ctx.get("backup_schedule"):
        api.call("DELETE", "/schedules/" + urllib.parse.quote(ctx["backup_schedule"], safe=""))
    if ctx.get("backup_task"):
        api.call("DELETE", "/tasks/" + urllib.parse.quote(ctx["backup_task"], safe=""))
    for key in (api.get("/keys")[1] or []):
        if key.get("name") == "ui check key":
            api.call("DELETE", "/keys/" + key["id"])
    for hook in (api.get("/webhooks")[1] or []):
        if hook.get("name") == "ui check hook":
            api.call("DELETE", "/webhooks/" + hook["id"])
    if ctx.get("queue_was_running"):
        api.post("/queue/start")
    if ctx.get("made_scratch"):
        st, _, _ = api.post("/remotes/%s/delete" % ctx["remote"], {"path": ctx["scratch"], "isDir": True})
        say("pass" if st == 200 else "fail", "scratch folder removed from the remote",
            "%s:%s -> %s" % (ctx["remote"], ctx["scratch"], st))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", default="http://127.0.0.1:19991")
    parser.add_argument("--token", default=os.environ.get("RB_TOKEN", ""))
    parser.add_argument("--container", help="read the token from this container (e.g. rclonebrowser-dev)")
    parser.add_argument("--remote", required=True, help="a remote to make the scratch folder on")
    parser.add_argument("--show", action="store_true", help="open the browser window, to watch it")
    parser.add_argument("--app-js", help="use this app.js instead of the one the program serves: "
                        "a change to the page, tried without building anything")
    parser.add_argument("--slow", type=int, default=0, help="ms between actions, with --show")
    args = parser.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except AttributeError:
            pass

    token = args.token or (verify_live.token_from_container(args.container) if args.container
                           else verify_live.token_from_windows())
    if not token:
        sys.exit("no token: pass --token, --container or set RB_TOKEN")
    api = Api(args.base, token)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    ctx = {"remote": args.remote, "scratch": "zz-rb-ui-" + stamp, "base": args.base.rstrip("/"),
           "local": "/media" if args.container else os.path.expanduser("~")}

    print("Clicking through %s with remote %s, scratch folder %s\n" % (args.base, args.remote, ctx["scratch"]))
    started = time.time()
    set_up(api, ctx)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(channel="chrome", headless=not args.show, slow_mo=args.slow)
            context = browser.new_context(viewport={"width": 1366, "height": 900})
            # Signed in the way the page keeps it: the token in its storage.
            context.add_init_script("localStorage.setItem('rb.token', %r)" % token)
            if args.app_js:
                with open(args.app_js, "rb") as f:
                    script = f.read()
                context.route("**/app.js", lambda route: route.fulfill(
                    status=200, body=script, headers={"Content-Type": "application/javascript"}))
                print("app.js from %s\n" % args.app_js)
            page = context.new_page()
            watch = Watch(page)

            def load():
                page.goto(ctx["base"] + "/#/remotes")
                page.wait_for_selector("#app:not(.hidden)", timeout=WAIT)
                return page.title()
            step(page, watch, "the page opens, signed in", load)

            check_tabs(page, watch)
            check_queue(page, watch, api, ctx)
            check_remote(page, watch, api, ctx)
            check_many(page, watch, api, ctx)
            check_api_tab(page, watch, api, ctx)
            check_backup_and_schedule(page, watch, api, ctx)
            check_tasks(page, watch, api, ctx)
            check_phone(page, watch, ctx)
            browser.close()
    finally:
        clean_up(api, ctx)

    counts = {k: sum(1 for r in RESULTS if r[0] == k) for k in ("PASS", "FAIL", "SKIP", "LOOK")}
    summary = "%(PASS)d passed, %(FAIL)d failed" % counts
    print("\n" + summary + " (%d s)" % (time.time() - started))
    with open("ui-report.md", "w", encoding="utf-8") as report:
        report.write("# check_ui — %s\n\n%s · %s · remote %s\n\n| | step | what it saw |\n|---|---|---|\n" % (
            stamp, summary, args.base, args.remote))
        for mark, name, detail in RESULTS:
            report.write("| %s | %s | %s |\n" % (mark, name, str(detail).replace("|", "\\|").replace("\n", " ")))
    print("written to ui-report.md")
    sys.exit(1 if counts["FAIL"] else 0)


if __name__ == "__main__":
    main()
