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
import json
import re
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
        self.sent_at = {}
        page.on("request", lambda r: (self.pending.__setitem__(id(r), r),
                                      self.sent_at.__setitem__(id(r), time.time())))
        page.on("requestfinished", lambda r: self.pending.pop(id(r), None))
        page.on("requestfailed", lambda r: self.pending.pop(id(r), None))

    def waiting(self):
        found = []
        streams = 0
        for key, r in self.pending.items():
            path = urllib.parse.urlparse(r.url).path
            if path.endswith("/events"):
                streams += 1
            else:
                found.append("%s %s (%.0fs)" % (r.method, path, time.time() - self.sent_at.get(key, time.time())))
        if found and streams != 1:
            found.append("%d event streams open" % streams)
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


def tool(page, selector):
    """A toolbar button, pressed as a person would: its menu opened first
    when it is in one (V-91)."""
    page.evaluate("""s => {
        const menu = document.querySelector(s).closest('.tool-menu');
        if (menu && menu.classList.contains('hidden')) menu.previousElementSibling.click();
    }""", selector)
    page.click(selector)


# How far apart two colours are to a reader (WCAG): 4.5 is what body text
# needs. Computed in the page, from what it actually painted.
CONTRAST_JS = r"""el => {
    const rgb = (c) => (c.match(/[0-9.]+/g) || [0, 0, 0]).slice(0, 3).map(Number);
    const lum = (c) => {
        const [r, g, b] = rgb(c).map((v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); });
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    };
    let back = el, bg = 'rgba(0, 0, 0, 0)';
    while (back) {
        bg = getComputedStyle(back).backgroundColor;
        if (!/rgba\(.*, 0\)$/.test(bg) && bg !== 'transparent') break;
        back = back.parentElement;
    }
    const a = lum(getComputedStyle(el).color), b = lum(bg);
    return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
}"""


def on_screen_errors(page, toast_ok=False):
    found = []
    if shown(page, "#toast") and not toast_ok:
        found.append("error bar: " + page.locator("#toast").inner_text().strip())
    if shown(page, "#taskbox"):
        said = page.locator("#task-error").inner_text().strip()
        if said:
            found.append("transfer form: " + said)
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
    # The transfer form is left open for the next step to go on with,
    # unless this one went wrong.
    for close in ("#ask-no", "#log-close", "#result-close") + (
            ("#pick-cancel", "#task-cancel", "#remote-form-cancel") if problems else ()):
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
            tool(page, button)
            page.wait_for_selector("#resultbox:not(.hidden)", timeout=5000)
            heading = page.locator("#result-title").inner_text()
            assert heading.startswith(title), "the box says %r" % heading
            page.wait_for_function(
                "!document.getElementById('result-body').innerText.includes('asking rclone')", timeout=WAIT)
            return "%s · %s" % (heading, page.locator("#result-body").inner_text().strip()[:80].replace("\n", " "))
        step(page, watch, "remote: %s (rclone's answer in the box)" % title.split()[0], question)

    def size_cancel():
        tool(page, "#remote-size")
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
        tool(page, "#remote-view")
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


def check_menus(page, watch, api, ctx):
    """The menus on the remotes page, in both themes: the right-click menu
    could not be read on a light page (white on white), the toolbar ran to
    two lines, and a remote's card had no menu (V-91)."""
    remote, scratch = ctx["remote"], ctx["scratch"]

    def open_folder():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        page.locator("#listing tr[data-entry]").first.wait_for(timeout=WAIT)

    def theme(name):
        page.evaluate("t => document.documentElement.setAttribute('data-theme', t)", name)

    def readable(selector):
        worst = None
        for item in page.locator(selector).all():
            if item.is_enabled():
                ratio = item.evaluate(CONTRAST_JS)
                worst = ratio if worst is None else min(worst, ratio)
        assert worst is not None, "nothing to read in %s" % selector
        return worst

    def row_menu_readable():
        open_folder()
        said = []
        for name in ("light", "dark"):
            theme(name)
            page.locator("#listing tr[data-entry] td").last.click(button="right")
            page.wait_for_selector("#row-menu:not(.hidden)", timeout=5000)
            worst = readable("#row-menu button")
            page.keyboard.press("Escape")
            assert worst >= 4.5, "%s: the menu's text is %.1f:1 against it (4.5 needed)" % (name, worst)
            said.append("%s %.1f:1" % (name, worst))
        return "right-click menu: " + " · ".join(said)
    step(page, watch, "Right-click menu can be read, light and dark (V-91)", row_menu_readable)

    def toolbar_menus():
        open_folder()
        said = []
        for name in ("light", "dark"):
            theme(name)
            openers = page.locator(".toolbar .tool-open")
            for i in range(openers.count()):
                opener = openers.nth(i)
                opener.click()
                menu = opener.locator("xpath=following-sibling::div[contains(@class,'tool-menu')]")
                assert menu.is_visible(), "%s did not open" % opener.inner_text()
                assert page.locator(".tool-menu:not(.hidden)").count() == 1, "more than one menu open"
                box = menu.bounding_box()
                assert box["x"] + box["width"] <= page.viewport_size["width"], "%s runs off the page" % opener.inner_text()
                worst = readable(".tool-menu:not(.hidden) button")
                assert worst >= 4.5, "%s %s: %.1f:1" % (name, opener.inner_text(), worst)
                if name == "light":
                    said.append("%s (%d)" % (opener.inner_text().rstrip(" ▾"), menu.locator("button").count()))
            page.keyboard.press("Escape")
            assert page.locator(".tool-menu:not(.hidden)").count() == 0, "Escape left a menu open"
        # Chosen from a menu: it does that, and the menu shuts.
        tool(page, "#remote-about")
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=5000)
        assert page.locator(".tool-menu:not(.hidden)").count() == 0, "the menu stayed open"
        page.click("#result-close")
        # One line at this width: every button's top the same.
        tops = page.evaluate("""() => [...document.querySelectorAll('.toolbar > button, .toolbar > .tool-group')]
            .filter((b) => b.offsetParent).map((b) => Math.round(b.getBoundingClientRect().top))""")
        assert len(set(tops)) == 1, "the toolbar is %d lines" % len(set(tops))
        return "one line · " + ", ".join(said) + " · Escape shuts · readable light and dark"
    step(page, watch, "Toolbar: the rest in menus, one line (V-91)", toolbar_menus)

    def card_menu():
        open_folder()
        theme("light")
        page.locator('#remote-list button[data-remote="%s"]' % remote).click(button="right")
        page.wait_for_selector("#card-menu:not(.hidden)", timeout=5000)
        items = page.locator("#card-menu button").all_inner_texts()
        assert items == ["Open", "Settings…", "Info", "New remote…"], "it offers %r" % items
        worst = readable("#card-menu button")
        assert worst >= 4.5, "the card menu's text is %.1f:1" % worst
        page.click('#card-menu button[data-card="info"]')
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=5000)
        title = page.locator("#result-title").inner_text()
        assert title.startswith("About " + remote), "Info opened %r" % title
        page.click("#result-close")
        page.locator("#remote-list button[data-remote-new]").click(button="right")
        page.wait_for_selector("#card-menu:not(.hidden)", timeout=5000)
        only = page.locator("#card-menu button").all_inner_texts()
        page.keyboard.press("Escape")
        assert only == ["New remote…"], "on + New remote it offers %r" % only
        return "%s: %s · Info → %r · + New remote: %s" % (remote, ", ".join(items), title, only[0])
    step(page, watch, "A remote's card has a right-click menu (V-91)", card_menu)
    theme("dark")


def check_new_remote_form(page, watch, api, ctx):
    """New remote, opened while the program is still asking rclone what kinds
    of remote there are, as just after it starts: the kinds come once they
    are known, and Sign in shows only for a kind that signs in (V-90).
    Nothing is saved: the form is cancelled."""
    answered_early = {"n": 0}

    def still_reading(route):
        if answered_early["n"] < 2:
            answered_early["n"] += 1
            route.fulfill(status=200, content_type="application/json",
                          body='{"loaded":false,"providers":[]}')
        else:
            route.continue_()

    def open_form():
        page.goto(ctx["base"] + "/#/remotes")
        page.wait_for_selector("#tab-remotes.on", timeout=5000)
        page.click("[data-remote-new]")
        page.wait_for_selector("#remotebox:not(.hidden)", timeout=5000)

    def kinds_come():
        page.route("**/api/v1/providers", still_reading)
        try:
            open_form()
            page.wait_for_function("document.querySelectorAll('#remote-form-type option[value]').length > 10",
                                   timeout=15000)
        finally:
            page.unroute("**/api/v1/providers", still_reading)
        kinds = page.locator("#remote-form-type option[value]").count()
        assert answered_early["n"] == 2, "asked %d times while still reading" % answered_early["n"]
        assert not shown(page, "#remote-form-signin"), "Sign in shown with no kind chosen"
        page.fill("#remote-form-filter", "te")
        teldrive = page.locator('#remote-form-type option[value="teldrive"]').count()
        return "%d kinds after two 'still reading' answers · Sign in hidden · 'te' %s teldrive" % (
            kinds, "finds" if teldrive else "does not find (this rclone has none)")
    step(page, watch, "New remote: the kinds come even just after a start (V-90)", kinds_come)

    def sign_in_only_for_its_kind():
        page.fill("#remote-form-filter", "drive")
        page.select_option("#remote-form-type", "drive")
        page.wait_for_selector("#remote-form-signin:not(.hidden)", timeout=10000)
        page.click("#remote-form-cancel")
        open_form()
        page.wait_for_function("document.querySelectorAll('#remote-form-type option[value]').length > 10",
                               timeout=15000)
        page.wait_for_timeout(500)
        assert not shown(page, "#remote-form-signin"), "the last kind's Sign in is still on show"
        page.click("#remote-form-cancel")
        return "drive: Sign in · opened again: hidden"
    step(page, watch, "New remote: Sign in only for a kind that signs in (V-90)", sign_in_only_for_its_kind)

    def drives_are_a_list():
        drive_question = {"state": "drive_final", "name": "config_driveid",
                          "help": "Select drive you want to use", "default": "bad-drive",
                          "exclusive": False, "password": False, "error": "",
                          "examples": [{"value": "bad-drive", "help": "Old (personal)"},
                                       {"value": "good-drive", "help": "OneDrive (personal)"}]}
        refused = {"state": "drive_again", "name": "", "help": "", "default": "", "exclusive": False,
                   "password": False, "examples": [],
                   "error": 'Failed to query root for drive "bad-drive": HTTP error 400'}
        answers = []

        def made(route):
            if route.request.method != "POST":
                return route.continue_()
            route.fulfill(status=201, content_type="application/json",
                          body=json.dumps({"name": "zz-rb-ui-fake", "question": drive_question}))

        def answered(route):
            result = json.loads(route.request.post_data or "{}").get("result")
            answers.append(result)
            following = refused if result == "bad-drive" else drive_question
            route.fulfill(status=200, content_type="application/json",
                          body=json.dumps({"question": following}))

        page.route("**/api/v1/remotes", made)
        page.route("**/api/v1/remotes/*/answer", answered)
        try:
            open_form()
            page.wait_for_function("document.querySelectorAll('#remote-form-type option[value]').length > 10",
                                   timeout=15000)
            page.fill("#remote-form-filter", "local")
            page.select_option("#remote-form-type", "local")
            page.wait_for_timeout(500)
            page.fill("#remote-form-name", "zz-rb-ui-fake")
            page.click("#remote-form-save")
            page.wait_for_selector("#remote-form-q-choice", timeout=5000)
            offered = page.locator("#remote-form-q-choice option").all_inner_texts()
            assert len(offered) == 3 and "OneDrive (personal)" in offered[1] and offered[2] == "Other…", \
                "the drives offered: %r" % offered
            assert page.input_value("#remote-form-q-choice") == "bad-drive", "rclone's default not chosen"
            assert not shown(page, "#remote-form-q-text"), "a box to type in, unasked"
            page.select_option("#remote-form-q-choice", label="Other…")
            assert shown(page, "#remote-form-q-text"), "Other… opened no box"
            page.select_option("#remote-form-q-choice", "bad-drive")
            page.click("#remote-form-save")
            page.wait_for_function("document.getElementById('remote-form-q-error').innerText.includes('Failed to query root')",
                                   timeout=5000)
            assert page.locator("#remote-form-q-input input, #remote-form-q-input select").count() == 0, \
                "something to answer in, with nothing asked"
            page.click("#remote-form-save")
            page.wait_for_selector("#remote-form-q-choice", timeout=5000)
            page.select_option("#remote-form-q-choice", "good-drive")
            page.click("#remote-form-save")
            page.wait_for_timeout(800)
        finally:
            page.unroute("**/api/v1/remotes", made)
            page.unroute("**/api/v1/remotes/*/answer", answered)
            if shown(page, "#remotebox"):
                page.click("#remote-form-cancel")
        assert answers == ["bad-drive", "", "good-drive"], "answered %r" % answers
        return "3 offered (2 drives, Other…) · the refused one: its error alone · asked again · good-drive sent"
    # Ends with "Leave the rest", which says so in the bar.
    step(page, watch, "New remote: rclone's choices are a list, held to or not (V-91)", drives_are_a_list,
         toast_ok=True)


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


def check_log_level_setting(page, watch, api, ctx):
    """API tab → Program settings: how much rclone writes into a transfer's
    log (V-88). Set, read back, and put back as it was."""
    st, settings, _ = api.get("/settings")
    if st != 200 or "rcloneLogLevel" not in (settings or {}):
        return say("fail", "Program settings: rclone's log level (V-88)", "not in /settings: %s" % settings)
    was = settings["rcloneLogLevel"]
    if settings.get("rcloneLogLevelFromEnvironment"):
        return say("skip", "Program settings: rclone's log level (V-88)", "set by RB_RCLONE_LOG_LEVEL")

    def choose():
        page.click('#tabs button[data-tab="api"]')
        page.wait_for_selector("#set-log-level", timeout=10000)
        offered = page.locator("#set-log-level option").all_inner_texts()
        page.select_option("#set-log-level", "DEBUG")
        page.locator("#settings-form button[type=submit]").click()
        page.wait_for_timeout(800)
        now = api.get("/settings")[1].get("rcloneLogLevel")
        assert now == "DEBUG", "the program says %r" % now
        return "offered %s · DEBUG saved" % ", ".join(o.split(" ")[0] for o in offered)
    try:
        step(page, watch, "Program settings: rclone's log level (V-88)", choose, toast_ok=True)
    finally:
        api.call("PUT", "/settings", {"rcloneLogLevel": was})


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


def open_upload(page, ctx):
    page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(ctx["remote"]),
                                                  urllib.parse.quote(ctx["scratch"])))
    entry_row(page, ctx["scratch"] + "/a.txt").wait_for(timeout=WAIT)
    tool(page, "#remote-upload")
    page.wait_for_selector("#taskbox:not(.hidden)", timeout=5000)


def pane(page, name):
    page.click('#task-tabs button[data-pane="%s"]' % name)
    page.wait_for_selector('section[data-pane="%s"].on, section[data-pane="%s"]:visible' % (name, name),
                           timeout=5000)


def command_now(page):
    pane(page, "command")
    page.wait_for_function("!['', '…'].includes(document.getElementById('task-command').innerText.trim())",
                           timeout=WAIT)
    return page.locator("#task-command").inner_text()


def field(page, name):
    return page.locator('#taskbox [data-field="%s"]' % name)


def check_transfer_form(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]
    src = ctx.get("local_src")

    def defaults():
        open_upload(page, ctx)
        kind = page.eval_on_selector("#task-dest-remote", "e => e.tagName")
        assert kind == "SELECT", "To's remote is a %s, not a list to choose from" % kind
        assert page.input_value("#task-dest-remote") == remote, page.input_value("#task-dest-remote")
        assert page.input_value("#task-dest") == scratch, "To is %r" % page.input_value("#task-dest")
        tabs = page.locator("#task-tabs button").count()
        assert tabs == 7, "%d tabs" % tabs
        got = (field(page, "transfers").input_value(), field(page, "checkers").input_value())
        assert got == ("4", "8"), "transfers/checkers are %s/%s" % got
        return "To = %s:%s · 7 tabs · transfers 4 · checkers 8" % (remote, scratch)
    step(page, watch, "transfer form: Upload opens with the folder as To (V-38 #1, V-39 #1)", defaults)

    def from_note():
        page.fill("#task-source", "/no/such/place-for-ui-check")
        page.wait_for_selector("#task-source-note.bad", timeout=5000)
        bad = page.locator("#task-source-note").inner_text()
        if not src:
            return "not there: %r (no folder of this machine to try a real one)" % bad
        page.fill("#task-source", src)
        page.wait_for_function("document.getElementById('task-source-note').innerText === 'a folder'",
                               timeout=5000)
        return "not there: %r · real: 'a folder'" % bad
    step(page, watch, "transfer form: From says whether it is there (V-39 #2)", from_note,
         allowed={("GET", "/local/stat")})

    def sync_options():
        pane(page, "settings")
        page.check('input[name="task-operation"][value="sync"]')
        assert page.is_enabled("#task-timing") and page.is_enabled("#task-delete-excluded"), \
            "the delete choices are greyed out for sync"
        page.check('input[name="task-operation"][value="copy"]')
        assert not page.is_enabled("#task-timing") and not page.is_enabled("#task-delete-excluded"), \
            "the delete choices can be used for copy"
        return "sync: delete choices on · copy: off"
    step(page, watch, "transfer form: Sync's own choices (V-38 #4)", sync_options)

    def units():
        pane(page, "transfer")
        page.fill('[data-unit-number="bandwidth"]', "10")
        page.select_option('[data-unit-choice="bandwidth"]', "M")
        with_it = command_now(page)
        assert "--bwlimit 10M" in with_it, with_it
        pane(page, "transfer")
        page.fill('[data-unit-number="bandwidth"]', "")
        page.wait_for_timeout(500)
        without = command_now(page)
        assert "--bwlimit" not in without, without
        return "10 MiB/s → --bwlimit 10M · emptied → gone"
    step(page, watch, "transfer form: a number and its unit (V-39 #14, #15)", units)

    def patterns():
        pane(page, "include")
        new = page.locator('[data-pattern-new="included"]')
        new.fill("*.mkv")
        new.press("Enter")
        new.fill("*.mkv")
        new.press("Enter")
        note = page.locator('[data-pattern-note="included"]').inner_text()
        assert "already" in note, "a second *.mkv said %r" % note
        new.fill("*.srt")
        page.click('[data-pattern-add="included"]')
        rows = page.locator('[data-pattern-list="included"] .flag-row')
        assert rows.count() == 2, "%d rows" % rows.count()
        rows.nth(1).locator("[data-pattern-drop]").click()
        assert rows.count() == 1, "Remove left %d" % rows.count()
        pane(page, "exclude")
        page.fill('[data-pattern-new="excluded"]', "*.nfo")
        page.click('[data-pattern-add="excluded"]')
        line = command_now(page)
        assert "--include" in line and "*.mkv" in line, line
        assert "--exclude" in line and "*.nfo" in line, line
        assert "*.srt" not in line, line
        return "Enter adds · the same twice: %r · Remove · both in the command" % note
    step(page, watch, "transfer form: include and exclude patterns (V-39 #17, #19)", patterns)

    def filters():
        pane(page, "filter")
        new = page.locator('[data-pattern-new="filtered"]')
        page.select_option('[data-pattern-sign="filtered"]', "-")
        new.fill("*.tmp")
        page.click('[data-pattern-add="filtered"]')
        page.select_option('[data-pattern-sign="filtered"]', "+")
        new.fill("*.mkv")
        page.click('[data-pattern-add="filtered"]')
        rows = page.locator('[data-pattern-list="filtered"] .flag-row')
        assert rows.count() == 2, "%d rows" % rows.count()
        assert rows.nth(0).locator(".order").inner_text() == "1", "no order numbers"
        rows.nth(1).locator("[data-pattern-up]").click()
        first = rows.nth(0).locator("code").inner_text()
        assert first == "+ *.mkv", "the first rule is %r after ↑" % first
        line = command_now(page)
        assert "--filter" in line, line
        return "numbered · ↑ moved '+ *.mkv' first · --filter in the command"
    step(page, watch, "transfer form: filter rules in order (V-39 #18)", filters)

    def extra():
        pane(page, "extra")
        page.fill("#flag-name", "fast")
        page.locator('#flag-menu button[data-flag="--fast-list"]').wait_for(timeout=WAIT)
        offered = page.locator("#flag-menu button[data-flag]").count()
        page.click('#flag-menu button[data-flag="--fast-list"]')
        assert not page.is_enabled("#flag-value"), "a value can be typed for --fast-list"
        page.click("#flag-add")
        page.locator('#flag-list code:text-is("--fast-list")').wait_for(timeout=5000)
        page.fill("#flag-name", "--max-transfer")
        page.fill("#flag-value", "")
        page.click("#flag-add")
        page.wait_for_timeout(300)
        assert page.locator('#flag-list code:text-is("--max-transfer")').count() == 0, \
            "--max-transfer was added with no value"
        said = page.locator("#flag-help").inner_text().strip()
        assert "needs a value" in said, "about the missing value it said %r" % said
        return "%d offered for 'fast' · --fast-list added, no value · --max-transfer refused: %r" % (offered, said)
    step(page, watch, "transfer form: extra options (V-39 #4, #5, #6)", extra)
    page.click("#task-cancel")

    if not src:
        say("skip", "transfer form: saving and running", "no folder of this machine to send from")
        return

    def tasks_now():
        return {t["id"]: t for t in (api.get("/tasks")[1] or [])}

    def save_unnamed():
        before = tasks_now()
        open_upload(page, ctx)
        page.fill("#task-source", src)
        page.click('#taskbox [data-act="save"]')
        page.wait_for_selector("#taskbox.hidden", state="attached", timeout=10000)
        made = [t for i, t in tasks_now().items() if i not in before]
        assert len(made) == 1, "%d new tasks" % len(made)
        ctx.setdefault("form_tasks", []).append(made[0]["id"])
        name = made[0]["name"]
        assert name.startswith("_tmp_"), "named %r" % name
        page.click('#tabs button[data-tab="tasks"]')
        page.locator('#tab-tasks [data-edit="%s"]' % made[0]["id"]).wait_for(timeout=10000)
        ctx["unnamed"] = made[0]["id"]
        return "named %s · listed in Tasks" % name
    step(page, watch, "transfer form: Save task with no name (V-38 #9)", save_unnamed, toast_ok=True)

    if ctx.get("unnamed"):
        def edit():
            before = len(tasks_now())
            page.click('#tab-tasks [data-edit="%s"]' % ctx["unnamed"])
            page.wait_for_selector("#taskbox:not(.hidden)", timeout=5000)
            assert page.input_value("#task-source").rstrip("/\\") == src.rstrip("/\\"), \
                "From is %r" % page.input_value("#task-source")
            pane(page, "transfer")
            field(page, "transfers").fill("3")
            page.click('#taskbox [data-act="save"]')
            page.wait_for_selector("#taskbox.hidden", state="attached", timeout=10000)
            now = tasks_now()
            assert len(now) == before, "%d tasks, %d before" % (len(now), before)
            assert str(now[ctx["unnamed"]].get("transfers")) == "3", now[ctx["unnamed"]].get("transfers")
            return "opened with its values · transfers 4 → 3 · no new task"
        step(page, watch, "Tasks: Edit a task (V-38 #11)", edit, toast_ok=True)

    def new_from_tasks():
        page.click('#tabs button[data-tab="tasks"]')
        page.click('#tab-tasks [data-new-task="upload"]')
        page.wait_for_selector("#taskbox:not(.hidden)", timeout=5000)
        empty = (page.input_value("#task-source"), page.input_value("#task-name"))
        assert empty == ("", ""), "not empty: %s" % (empty,)
        assert page.is_enabled("#task-source-browse") and page.is_enabled("#task-dest-browse"), "Browse greyed"
        page.click("#task-cancel")
        return "empty · both Browse buttons work"
    step(page, watch, "Tasks: New upload task opens empty (V-38 #14)", new_from_tasks)

    def run_one(act, name, dest, queue=False):
        open_upload(page, ctx)
        page.fill("#task-source", src)
        page.fill("#task-dest", dest)
        page.fill("#task-name", name)
        if queue:
            page.check("#task-queue")
        page.click('#taskbox [data-act="%s"]' % act)
        page.wait_for_selector("#taskbox.hidden", state="attached", timeout=10000)
        made = [t for t in tasks_now().values() if t.get("name") == name]
        assert made, "no task called %r" % name
        ctx.setdefault("form_tasks", []).append(made[0]["id"])
        return made[0]["id"]

    def dry():
        task = run_one("dry", "ui check dry run", scratch + "/dry")
        page.click('#tabs button[data-tab="jobs"]')
        deadline = time.time() + 60
        row = None
        while time.time() < deadline and not row:
            rows = api.get("/history?limit=10&task=" + urllib.parse.quote(task))[1] or []
            row = next((r for r in rows if r.get("state") not in ("running", "")), None)
            time.sleep(1)
        assert row and row.get("dryRun"), "its run: %s" % row
        return "saved · ran as a dry run · %s" % row.get("state")
    step(page, watch, "transfer form: Save and Dry run (V-38 #6)", dry, toast_ok=True)

    def run():
        task = run_one("run", "ui check run", scratch + "/up")
        deadline = time.time() + 120
        row = None
        while time.time() < deadline and not row:
            rows = api.get("/history?limit=10&task=" + urllib.parse.quote(task))[1] or []
            row = next((r for r in rows if r.get("state") not in ("running", "")), None)
            time.sleep(1)
        assert row and row.get("state") == "finished", "its run: %s" % row
        st, ls, _ = api.get("/remotes/%s/ls?path=%s" % (remote, urllib.parse.quote(scratch + "/up")))
        names = [e["name"] for e in (ls or {}).get("entries", [])]
        assert "a.txt" in names, "up/ holds %s" % names
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        entry_row(page, scratch + "/up").wait_for(timeout=WAIT)
        return "finished · up/a.txt on the remote · listed"
    step(page, watch, "transfer form: Save and Run (V-38 #7)", run, toast_ok=True)

    def queued():
        task = run_one("run", "ui check queued", scratch + "/queued", queue=True)
        waiting = api.get("/queue")[1] or []
        mine = [e for e in waiting if e.get("taskId") == task]
        assert mine and not mine[0].get("running"), "the queue holds %s" % waiting
        return "in the queue, not started (the queue is paused)"
    step(page, watch, "transfer form: Add to the queue (V-38 #8)", queued, toast_ok=True)


def tab_on(page, name):
    return page.locator("#tab-%s.on" % name).count() == 1


def check_history_and_addresses(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]
    run_task = next((t for t in (api.get("/tasks")[1] or []) if t.get("name") == "ui check run"), None)

    def search():
        page.click('#tabs button[data-tab="history"]')
        page.wait_for_selector("#history-q", timeout=5000)
        page.click("#history-q")
        page.keyboard.type(scratch, delay=20)
        page.wait_for_timeout(1500)
        rows = page.locator("#tab-history tr[data-run]")
        n = rows.count()
        assert n > 0, "nothing found for %r" % scratch
        texts = rows.all_inner_texts()
        focused = page.evaluate("document.activeElement && document.activeElement.id")
        assert focused == "history-q", "the search box lost the focus to %r" % focused
        everything = api.get("/history?limit=100")[1] or []
        return "%d rows for the scratch folder (of %d) · still typing in the box" % (n, len(everything))
    step(page, watch, "History: find by words, the box keeps the focus (V-60 #2)", search)

    def open_run():
        row = page.locator("#tab-history tr[data-run]").first
        rid = row.get_attribute("data-run")
        row.locator("td").nth(1).click()
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        assert "#/history/" in page.url, "the address is " + page.url
        body = page.locator("#result-body").inner_text()
        assert body.strip(), "an empty box"
        log = page.locator("#result-body button[data-log]")
        said = ""
        if log.count():
            log.first.click()
            page.wait_for_selector("#logbox:not(.hidden)", timeout=10000)
            page.wait_for_function("document.getElementById('log-body').innerText.trim().length > 0",
                                   timeout=WAIT)
            said = " · its log: %d lines" % len(page.locator("#log-body").inner_text().splitlines())
            page.click("#log-close")
        page.go_back()
        page.wait_for_selector("#resultbox.hidden", state="attached", timeout=5000)
        assert page.url.endswith("#/history"), "Back went to " + page.url
        return "opened %s%s · Back closed it" % (rid[:9], said)
    step(page, watch, "History: a run in full, its address, Back (V-60 #4, V-66 #4)", open_run)

    def back_through_tabs():
        page.goto(ctx["base"] + "/#/remotes")
        page.wait_for_selector("#tab-remotes.on", timeout=5000)
        page.click('#tabs button[data-tab="jobs"]')
        page.click('#tabs button[data-tab="history"]')
        page.go_back()
        page.wait_for_selector("#tab-jobs.on", timeout=5000)
        first = page.url.split("#", 1)[-1]
        page.go_back()
        page.wait_for_selector("#tab-remotes.on", timeout=5000)
        return "History → Back: %s → Back: %s" % (first, page.url.split("#", 1)[-1])
    step(page, watch, "Back walks the tabs (V-66 #1)", back_through_tabs)

    def back_through_folders():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        page.click("#remote-refresh")
        entry_row(page, scratch + "/up").wait_for(timeout=WAIT)
        entry_row(page, scratch + "/up").locator("button").first.click()
        entry_row(page, scratch + "/up/a.txt").wait_for(timeout=WAIT)
        deeper = page.url.split("#", 1)[-1]
        page.go_back()
        entry_row(page, scratch + "/a.txt").wait_for(timeout=WAIT)
        return "%s → Back → %s" % (deeper, page.url.split("#", 1)[-1])
    if ctx.get("local_src"):
        step(page, watch, "Back from a folder two deep (V-66 #2)", back_through_folders)

    def reload_stays():
        page.click('#tabs button[data-tab="schedules"]')
        page.wait_for_selector("#tab-schedules.on", timeout=5000)
        page.reload()
        page.wait_for_selector("#app:not(.hidden)", timeout=WAIT)
        page.wait_for_selector("#tab-schedules.on", timeout=5000)
        return page.url.split("#", 1)[-1]
    step(page, watch, "F5 keeps the tab (V-66 #5)", reload_stays)

    if not run_task:
        say("skip", "task in full", "the transfer form made no task that ran")
        return

    def task_in_full():
        page.click('#tabs button[data-tab="tasks"]')
        page.click('#tab-tasks [data-task-open="%s"]' % run_task["id"])
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        page.wait_for_function("document.getElementById('result-body').innerText.includes('The last times it ran')",
                               timeout=WAIT)
        text = page.locator("#result-body").inner_text()
        for part in ("When it runs", "The last times it ran", "The command"):
            assert part in text, "no %r in the box" % part
        runs = page.locator("#result-body tr[data-run-open]")
        listed = runs.count()
        assert listed >= 1, "no runs listed"
        runs.first.click()
        page.wait_for_function("location.hash.startsWith('#/history/')", timeout=10000)
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        return "settings · when · %d runs · the command · a run opens in full" % listed
    step(page, watch, "Tasks: a task in full, and one of its runs (V-67 #1, #2)", task_in_full)

    def task_address():
        page.goto(ctx["base"] + "/#/tasks")
        page.wait_for_selector("#tab-tasks.on", timeout=5000)
        page.goto(ctx["base"] + "/#/tasks/" + urllib.parse.quote(run_task["id"], safe=""))
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        page.reload()
        page.wait_for_selector("#app:not(.hidden)", timeout=WAIT)
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        page.go_back()
        page.wait_for_selector("#resultbox.hidden", state="attached", timeout=5000)
        return "opened from its address · F5 kept it · Back closed it"
    step(page, watch, "Tasks: a task's own address, F5, Back (V-67 #4)", task_address)


def names_in(api, remote, path):
    st, ls, _ = api.get("/remotes/%s/ls?path=%s" % (remote, urllib.parse.quote(path)))
    return sorted(e["name"] for e in (ls or {}).get("entries", []))


def wait_for_names(api, remote, path, wanted, seconds=90):
    deadline = time.time() + seconds
    names = []
    while time.time() < deadline:
        names = names_in(api, remote, path)
        if all(w in names for w in wanted):
            return names
        time.sleep(2)
    raise AssertionError("%s:%s holds %s, not %s" % (remote, path, names, wanted))


def clear_choice(page):
    """Nothing chosen. Each click draws the list again, so the boxes are
    looked for afresh every time."""
    for _ in range(50):
        checked = page.locator("#listing input[data-pick]:checked")
        if checked.count() == 0:
            return
        checked.first.click()
    raise AssertionError("rows stay chosen")


def choose_alone(page, path):
    """Only this row chosen, by its own box: as a person would after
    clearing what was chosen before."""
    clear_choice(page)
    entry_row(page, path).locator("input[data-pick]").click()


def check_copy_move(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]
    for name in ("dst", "srcdir"):
        api.post("/remotes/%s/mkdir" % remote, {"path": scratch, "name": name})
    for folder, name in ((scratch, "f1.txt"), (scratch, "f2.txt"), (scratch + "/srcdir", "inside.txt")):
        st, body, _ = api.call("POST", "/remotes/%s/receive?path=%s&name=%s" % (
            remote, urllib.parse.quote(folder), name), raw=b"made by check_ui\n")
        if st == 202:
            api.wait_job(body["requestId"])
    # Drive lists what was just made a moment later, now and then: the steps
    # start once rclone sees all of it.
    wait_for_names(api, remote, scratch, ["dst", "srcdir", "f1.txt", "f2.txt"], 60)

    def show_scratch(*wanted):
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        page.click("#remote-refresh")
        for name in wanted or ("f1.txt", "dst", "srcdir"):
            entry_row(page, scratch + "/" + name).wait_for(timeout=WAIT)

    looked_again = []

    def see_in_chooser(folder):
        """Until the chooser lists `folder`: Drive now and then leaves a
        folder just made out of a listing for a moment, and a person would
        look again -- by clicking where they are. Said in the result."""
        here = folder.rsplit("/", 1)[0] if "/" in folder else ""
        wanted = page.locator('#pick-list button[data-pick="%s"]' % folder)
        for attempt in range(8):
            try:
                wanted.wait_for(timeout=4000)
                return
            except PlaywrightTimeout:
                looked_again.append(folder)
                page.locator('#pick-crumbs button[data-pick="%s"]' % here).click()
        wanted.wait_for(timeout=4000)

    def pick_into(folder):
        page.wait_for_selector("#pickbox:not(.hidden)", timeout=5000)
        see_in_chooser(folder)
        page.locator('#pick-list button[data-pick="%s"]' % folder).click()
        page.wait_for_selector('#pick-crumbs button[data-pick="%s"]' % folder, timeout=WAIT)

    def walk():
        show_scratch()
        choose_alone(page, scratch + "/f1.txt")
        page.click("#remote-copy")
        pick_into(scratch + "/dst")
        page.locator('#pick-crumbs button[data-pick="%s"]' % scratch).click()
        see_in_chooser(scratch + "/srcdir")
        page.locator('#pick-crumbs button[data-pick=""]').click()
        see_in_chooser(scratch)
        page.click("#pick-cancel")
        again = (" · looked again %d× (Drive listed a new folder late)" % len(looked_again)) if looked_again else ""
        return "into dst · back by its crumb · up to %s: by the first%s" % (remote, again)
    step(page, watch, "Copy: the folder chooser walks in and out (V-34 #3)", walk)

    def dry_copy():
        show_scratch()
        choose_alone(page, scratch + "/f1.txt")
        page.click("#remote-copy")
        pick_into(scratch + "/dst")
        page.check("#pick-dry")
        page.click("#pick-ok")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        time.sleep(8)
        names = names_in(api, remote, scratch + "/dst")
        assert "f1.txt" not in names, "a dry run copied: dst holds %s" % names
        rows = api.get("/history?limit=5&q=" + urllib.parse.quote(scratch + "/f1.txt"))[1] or []
        assert any(r.get("dryRun") for r in rows), "no dry run in the history: %s" % rows[:2]
        return "started · a dry run in the history · dst still empty"
    step(page, watch, "Copy: Dry run copies nothing (V-34 #5)", dry_copy, toast_ok=True)

    def copy_folder():
        show_scratch()
        choose_alone(page, scratch + "/srcdir")
        page.click("#remote-copy")
        pick_into(scratch + "/dst")
        page.uncheck("#pick-dry")
        page.click("#pick-ok")
        wait_for_names(api, remote, scratch + "/dst", ["srcdir"])
        inside = wait_for_names(api, remote, scratch + "/dst/srcdir", ["inside.txt"])
        top = names_in(api, remote, scratch + "/dst")
        assert "inside.txt" not in top, "its contents spilled into dst: %s" % top
        return "dst/srcdir/%s" % ", ".join(inside)
    step(page, watch, "Copy: a folder lands under its own name (V-34 #6)", copy_folder, toast_ok=True)

    def copy_three():
        show_scratch("a.txt", "f1.txt", "f2.txt")
        clear_choice(page)
        for name in ("a.txt", "f1.txt", "f2.txt"):
            entry_row(page, scratch + "/" + name).locator("input[data-pick]").click()
        page.click("#remote-copy")
        pick_into(scratch + "/dst")
        page.click("#pick-ok")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        said = page.locator("#toast").inner_text()
        assert "3 copy jobs started" in said, "it said %r" % said
        names = wait_for_names(api, remote, scratch + "/dst", ["a.txt", "f1.txt", "f2.txt"])
        return "%r · dst holds %s" % (said, ", ".join(names))
    step(page, watch, "Copy three at once (V-64 #3)", copy_three, toast_ok=True)

    def two_chosen():
        show_scratch("f1.txt", "f2.txt")
        clear_choice(page)
        for name in ("f1.txt", "f2.txt"):
            entry_row(page, scratch + "/" + name).locator("input[data-pick]").click()
        off = [b for b in ("#remote-rename", "#remote-link", "#remote-view") if page.is_enabled(b)]
        assert not off, "usable with two chosen: %s" % off
        assert page.is_enabled("#remote-download"), "Download is greyed out"
        title = page.get_attribute("#remote-download", "title") or ""
        assert "2" in title, "Download's tooltip: %r" % title
        return "Rename · Link · View off · Download: %r" % title
    step(page, watch, "Two chosen: what can and cannot be done (V-64 #4)", two_chosen)

    def move_one():
        show_scratch("f2.txt", "srcdir")
        choose_alone(page, scratch + "/f2.txt")
        page.click("#remote-move")
        pick_into(scratch + "/srcdir")
        page.click("#pick-ok")
        wait_for_names(api, remote, scratch + "/srcdir", ["f2.txt"])
        entry_row(page, scratch + "/f2.txt").wait_for(state="detached", timeout=WAIT)
        return "in srcdir · gone from the list on screen without a refresh"
    step(page, watch, "Move: gone from here, there instead (V-34 #8)", move_one, toast_ok=True)

    def into_itself():
        show_scratch()
        choose_alone(page, scratch + "/dst")
        page.click("#remote-copy")
        pick_into(scratch + "/dst")
        before = len(api.get("/history?limit=50&q=" + urllib.parse.quote(scratch + "/dst"))[1] or [])
        page.click("#pick-ok")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        said = page.locator("#toast").inner_text()
        after = len(api.get("/history?limit=50&q=" + urllib.parse.quote(scratch + "/dst"))[1] or [])
        assert after == before, "a job was started anyway"
        return "refused: %r" % said
    step(page, watch, "Copy a folder into itself is refused (V-34 #10)", into_itself,
         allowed={("POST", "/copy")}, toast_ok=True)

    def single_download():
        show_scratch()
        choose_alone(page, scratch + "/f1.txt")
        page.click("#remote-download")
        page.wait_for_selector("#taskbox:not(.hidden)", timeout=5000)
        source = page.input_value("#task-source")
        page.click("#task-cancel")
        assert source.endswith("f1.txt"), "From is %r" % source
        return "the whole transfer form, From = %s" % source
    step(page, watch, "Download one: the whole form (V-78 #3)", single_download)

    if not ctx.get("local_src"):
        return

    def download_three():
        show_scratch()
        clear_choice(page)
        for name in ("a.txt", "f1.txt", "dst"):
            entry_row(page, scratch + "/" + name).locator("input[data-pick]").click()
        page.click("#remote-download")
        page.wait_for_selector("#pickbox:not(.hidden)", timeout=5000)
        # Straight to the folder it is to go into: the way there is clicking
        # through folders, as the chooser's own steps above already show.
        target = ctx["local_src"]
        page.evaluate("p => { picking.path = p; pickLoad(); }", target)
        page.wait_for_function("document.getElementById('pick-crumbs').innerText.length > 0", timeout=WAIT)
        page.click("#pick-ok")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        said = page.locator("#toast").inner_text()
        assert "3 downloads started" in said, "it said %r" % said
        deadline = time.time() + 90
        names = []
        while time.time() < deadline:
            ls = api.get("/local/ls?path=" + urllib.parse.quote(target))[1] or {}
            names = [e["name"] for e in ls.get("entries", [])]
            if "f1.txt" in names and "dst" in names:
                break
            time.sleep(2)
        assert "f1.txt" in names and "dst" in names, "%s holds %s" % (target, names)
        return "%r · %s now holds %s" % (said, target, ", ".join(sorted(names)))
    step(page, watch, "Download three at once (V-78 #1)", download_three, toast_ok=True)


def schedule_row(page, sid):
    return page.locator('#tab-schedules tr:has([data-sched-switch="%s"])' % sid)


def check_schedules(page, watch, api, ctx):
    task = ctx["task"]

    def from_card():
        page.click('#tabs button[data-tab="tasks"]')
        page.click('#tab-tasks [data-sched-new="%s"]' % task)
        page.wait_for_selector("#schedbox:not(.hidden)", timeout=5000)
        chosen = page.input_value("#sched-task")
        assert chosen == task, "the form chose %r" % chosen
        assert page.is_checked('input[name="sched-mode"][value="daily"]'), "not daily"
        days = page.locator("#sched-daily [data-day]:checked").count()
        assert days == 7, "%d days ticked" % days
        assert page.input_value("#sched-time") == "06:30", page.input_value("#sched-time")
        assert page.is_checked("#sched-active"), "not switched on"
        return "the task chosen · every day · 06:30 · switched on"
    step(page, watch, "Schedules: Schedule on a task's card (V-41 #1)", from_card)

    def weekdays():
        before = {x["id"] for x in api.get("/schedules")[1] or []}
        for day in ("sat", "sun"):
            page.uncheck('#sched-daily [data-day="%s"]' % day)
        page.fill("#sched-time", "07:00")
        page.locator('#sched-form button[type="submit"]').click()
        page.wait_for_selector("#schedbox.hidden", state="attached", timeout=10000)
        made = [x for x in api.get("/schedules")[1] or [] if x["id"] not in before]
        assert len(made) == 1, "%d new schedules" % len(made)
        ctx["ui_schedule"] = made[0]["id"]
        page.click('#tabs button[data-tab="schedules"]')
        row = schedule_row(page, made[0]["id"])
        row.wait_for(timeout=10000)
        text = row.inner_text()
        assert "Mon" in text and "Fri" in text and "07:00" in text, "the row says %r" % text
        assert made[0].get("nextRun"), "no next run"
        page.click('#tabs button[data-tab="tasks"]')
        card = page.locator('#tab-tasks .card:has([data-sched-new="%s"])' % task)
        assert "07:00" in card.inner_text(), "the task's card has no ⏱ line"
        return "%r · next %s · on the task's card" % (" ".join(text.split())[:60], made[0]["nextRun"])
    step(page, watch, "Schedules: Mon–Fri at 07:00 (V-41 #2)", weekdays, toast_ok=True)

    if not ctx.get("ui_schedule"):
        return
    sid = ctx["ui_schedule"]

    def to_cron():
        page.click('#tabs button[data-tab="schedules"]')
        page.click('[data-sched-edit="%s"]' % sid)
        page.wait_for_selector("#schedbox:not(.hidden)", timeout=5000)
        page.check('input[name="sched-mode"][value="cron"]')
        page.fill("#sched-cron", "30 6,18 * * MON-FRI")
        page.locator('#sched-form button[type="submit"]').click()
        page.wait_for_selector("#schedbox.hidden", state="attached", timeout=10000)
        page.wait_for_function("document.querySelector('#tab-schedules').innerText.includes('6,18')",
                               timeout=10000)
        text = " ".join(schedule_row(page, sid).inner_text().split())
        one = next(x for x in api.get("/schedules")[1] if x["id"] == sid)
        assert one.get("cron") == "30 6,18 * * MON-FRI", one
        return "%s · next %s" % (text[:70], one.get("nextRun"))
    step(page, watch, "Schedules: Edit to a cron expression (V-41 #5)", to_cron, toast_ok=True)

    def off_and_on():
        page.click('[data-sched-switch="%s"]' % sid)
        # Its button says what pressing it would do: "Switch on" once it is off.
        page.wait_for_selector('[data-sched-switch="%s"]:text-is("Switch on")' % sid, timeout=10000)
        off = " ".join(schedule_row(page, sid).inner_text().split())
        assert "—" in off, "next run still shown when off: %r" % off
        page.click('[data-sched-switch="%s"]' % sid)
        page.wait_for_timeout(800)
        on = next(x for x in api.get("/schedules")[1] if x["id"] == sid)
        assert on["active"], "not on again"
        return "off: next run — · on again"
    step(page, watch, "Schedules: Switch off and on (V-41 #8)", off_and_on)

    def drop():
        page.click('[data-sched-drop="%s"]' % sid)
        answer(page, "Delete the schedule")
        schedule_row(page, sid).wait_for(state="detached", timeout=10000)
        st, _, _ = api.get("/tasks/" + urllib.parse.quote(task, safe=""))
        assert st == 200, "its task went too (%s)" % st
        ctx["ui_schedule"] = None
        return "gone · the task is still there"
    step(page, watch, "Schedules: Delete keeps the task (V-41 #13)", drop)


def job_card(page, rid):
    return page.locator('#tab-jobs .card:has([data-job-open="%s"])' % rid)


def check_serve_and_stop(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]

    def serve():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        entry_row(page, scratch + "/a.txt").wait_for(timeout=WAIT)
        clear_choice(page)
        before = {j["requestId"] for j in api.get("/jobs")[1] or []}
        tool(page, "#remote-serve")
        page.wait_for_selector("#serve-go", timeout=5000)
        page.select_option("#serve-protocol", "webdav")
        page.fill("#serve-port", "18911")
        page.fill("#serve-user", "ui")
        page.fill("#serve-pass", "check-ui-pass")
        page.click("#serve-go")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        made = [j for j in api.get("/jobs")[1] or [] if j["requestId"] not in before and j["kind"] == "serve"]
        assert made, "no serve job"
        ctx["serve_job"] = made[0]["requestId"]
        page.click('#tabs button[data-tab="jobs"]')
        card = job_card(page, made[0]["requestId"])
        card.wait_for(timeout=10000)
        copy = card.locator("[data-copy-text]")
        address = copy.get_attribute("data-copy-text")
        assert address and "18911" in address, "the card's address: %r" % address
        # Its log while it runs: opened, with what it runs at the top -- a 404
        # before, for a serve nobody had used yet (2026-10-21). What rclone has
        # said, if anything yet, follows.
        card.locator('[data-log="%s"]' % made[0]["requestId"]).click()
        page.wait_for_selector("#logbox:not(.hidden)", timeout=10000)
        page.wait_for_function("document.getElementById('log-body').innerText.includes('# command:')",
                               timeout=10000)
        said = [l for l in page.locator("#log-body").inner_text().splitlines()
                if re.search(r"\d{4}/\d\d/\d\d \d\d:\d\d:\d\d", l)]
        page.click("#log-close")
        copy.click()
        page.wait_for_timeout(500)
        if shown(page, "#askbox"):
            value = page.input_value("#ask-value")
            assert value == address, "the box offers %r" % value
            page.click("#ask-yes")
            how = "offered in a box to copy (no clipboard here)"
        else:
            how = "copied"
        return "%s · Copy address: %s · its log while serving opens: %d lines from rclone so far" % (address, how, len(said))
    step(page, watch, "Serve over WebDAV: the card has the address, its log while it runs (V-52 #5, V-86)",
         serve, toast_ok=True)

    if not ctx.get("serve_job"):
        return
    rid = ctx["serve_job"]

    def stop():
        card = job_card(page, rid)
        card.locator('[data-stop="%s"]' % rid).click()
        answer(page, "Stop", yes=False)
        page.wait_for_timeout(500)
        job = next((j for j in api.get("/jobs")[1] if j["requestId"] == rid), {})
        assert job.get("running"), "Cancel stopped it anyway"
        card.locator('[data-stop="%s"]' % rid).click()
        answer(page, "Stop")
        deadline = time.time() + 20
        while time.time() < deadline:
            job = next((j for j in api.get("/jobs")[1] if j["requestId"] == rid), {})
            if not job.get("running"):
                break
            time.sleep(1)
        assert not job.get("running"), "still serving"
        ctx["serve_job"] = None
        return "asked · Cancel kept it · Yes stopped it (%s)" % job.get("state")
    step(page, watch, "Stop a job: asked first (V-80 #5)", stop)


def free_drive_letter():
    for letter in "YXWVUTS":
        if not os.path.exists(letter + ":\\"):
            return letter + ":"
    return None


def check_mount(page, watch, api, ctx):
    remote, scratch = ctx["remote"], ctx["scratch"]
    windows = ctx.get("windows")
    point = free_drive_letter() if windows else ctx.get("local_src")
    if not point:
        return say("skip", "Mount", "no free drive letter / no folder to mount on")

    def mount():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        entry_row(page, scratch + "/a.txt").wait_for(timeout=WAIT)
        clear_choice(page)
        tool(page, "#remote-mount")
        page.wait_for_selector("#mount-go", timeout=5000)
        for part in ("#mount-point", "#mount-cache", "#mount-ro", "#mount-volume", "#mount-extra",
                     "#mount-save", "#mount-auto", "#mount-script"):
            assert page.locator(part).count() == 1, "no %s in the form" % part
        page.fill("#mount-point", point)
        page.check("#mount-ro")
        # Keeping it as a task, and the script, are under More.
        page.locator("#result-body summary", has_text="More").click()
        page.fill("#mount-save", "zz ui mount")
        if windows and ctx.get("local_src"):
            page.fill("#mount-script", ctx["local_src"].replace("/", "\\") + "\\after.cmd")
        before = {j["requestId"] for j in api.get("/jobs")[1] or []}
        page.click("#mount-go")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        made = [j for j in api.get("/jobs")[1] or [] if j["requestId"] not in before and j["kind"] == "mount"]
        assert made, "no mount job"
        ctx["mount_job"] = made[0]["requestId"]
        saved = [t for t in api.get("/tasks")[1] or [] if t.get("name") == "zz ui mount"]
        assert saved, "not saved as a task"
        ctx.setdefault("form_tasks", []).append(saved[0]["id"])
        return "on %s · read-only · kept as the task \"zz ui mount\"" % point
    step(page, watch, "Mount: the form, kept as a task (V-36 #3, V-75 #2)", mount, toast_ok=True)

    rid = ctx.get("mount_job")
    if not rid:
        return

    def wait_job_settled(seconds=20):
        deadline = time.time() + seconds
        job = {}
        while time.time() < deadline:
            job = next((j for j in api.get("/jobs")[1] if j["requestId"] == rid), {})
            if not job.get("running") or (job.get("script") or {}).get("exitCode") is not None:
                if not job.get("running") or not windows:
                    return job
            if windows and os.path.exists(point + "\\a.txt") and job.get("running"):
                time.sleep(2)
                return next((j for j in api.get("/jobs")[1] if j["requestId"] == rid), {})
            time.sleep(1)
        return job

    if not windows:
        def fails_with_reason():
            job = wait_job_settled()
            assert not job.get("running"), "still running"
            assert job.get("state") == "error", "ended %s" % job.get("state")
            log = api.get("/jobs/%s/log?tail=20" % urllib.parse.quote(rid, safe=""))[1]
            log = log.decode() if isinstance(log, bytes) else str(log)
            reason = next((l for l in log.splitlines() if "Fatal" in l or "ERROR" in l or "CRITICAL" in l), "")
            assert reason, "no reason from rclone in the log"
            page.click('#tabs button[data-tab="jobs"]')
            return "error · rclone said: %s" % reason.split(": ", 1)[-1][:90]
        step(page, watch, "Mount where it cannot: an error, rclone's words (V-36 #7)", fails_with_reason)
        return

    def mounted():
        job = wait_job_settled(40)
        assert job.get("running"), "not mounted: %s" % job.get("state")
        assert os.path.exists(point + "\\a.txt"), "no a.txt on %s" % point
        page.click('#tabs button[data-tab="jobs"]')
        page.click('[data-job-open="%s"]' % rid)
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        try:
            page.wait_for_function("document.getElementById('result-body').innerText.includes('mounted on')",
                                   timeout=20000)
        except PlaywrightTimeout:
            told = (next((j for j in api.get("/jobs")[1] if j["requestId"] == rid), {}).get("script") or {})
            raise AssertionError("the open box still says the script is running; the daemon says %s -- "
                                 "a program built before the box was drawn again from events?" % told)
        text = page.locator("#result-body").inner_text()
        page.click("#result-close")
        script = job.get("script") or {}
        return "%s holds a.txt · the script: %s" % (point, " ".join(script.get("output") or [])[:80])
    step(page, watch, "Mount: the drive is there, the script's output shown (V-36 #3, V-79 #2)", mounted)

    def unmount():
        page.click('#tabs button[data-tab="jobs"]')
        job_card(page, rid).locator('[data-stop="%s"]' % rid).click()
        answer(page, "Unmount")
        deadline = time.time() + 30
        while time.time() < deadline and os.path.exists(point + "\\"):
            time.sleep(1)
        assert not os.path.exists(point + "\\"), "%s is still there" % point
        ctx["mount_job"] = None
        return "%s gone" % point
    step(page, watch, "Mount: Unmount from its card (V-36 #5)", unmount)


def check_browse_and_menu(page, watch, api, ctx):
    remote, scratch, src = ctx["remote"], ctx["scratch"], ctx.get("local_src")
    if not src:
        return say("skip", "Browse in the transfer form", "no folder of this machine")
    parent, name = src.rsplit("/", 1)

    def jump(path):
        page.evaluate("p => { picking.path = p; pickLoad(); }", path)
        page.wait_for_timeout(800)

    def browse_folder():
        open_upload(page, ctx)
        page.click("#task-source-browse")
        page.wait_for_selector("#pickbox:not(.hidden)", timeout=5000)
        jump(parent)
        page.locator('#pick-list button[data-pick$="%s"]' % name).click()
        page.wait_for_timeout(800)
        page.click("#pick-ok")
        page.wait_for_selector("#pickbox.hidden", state="attached", timeout=5000)
        got = page.input_value("#task-source").replace("\\", "/").rstrip("/")
        to = page.input_value("#task-dest")
        assert got.endswith(name), "From is %r" % got
        assert to == scratch + "/" + name, "To is %r" % to
        return "From = …/%s · To = %s" % (name, to)
    step(page, watch, "Transfer form: Browse, a folder (V-38 #2)", browse_folder)

    def browse_file():
        page.click("#task-source-browse")
        page.wait_for_selector("#pickbox:not(.hidden)", timeout=5000)
        jump(src)
        page.locator('#pick-list button[data-pick-file$="a.txt"]').click()
        page.click("#pick-ok")
        page.wait_for_selector("#pickbox.hidden", state="attached", timeout=5000)
        got = page.input_value("#task-source")
        to = page.input_value("#task-dest")
        page.click("#task-cancel")
        assert got.replace("\\", "/").endswith("/a.txt"), "From is %r" % got
        assert to == scratch, "To is %r" % to
        return "From = …/a.txt · To = %s again" % to
    step(page, watch, "Transfer form: Browse, a file (V-38 #3)", browse_file)

    def menu_download():
        page.goto(ctx["base"] + "/#/remotes/%s/%s" % (urllib.parse.quote(remote), urllib.parse.quote(scratch)))
        page.click("#remote-refresh")
        for name_ in ("a.txt", "f1.txt"):
            entry_row(page, scratch + "/" + name_).wait_for(timeout=WAIT)
        clear_choice(page)
        for name_ in ("a.txt", "f1.txt"):
            entry_row(page, scratch + "/" + name_).locator("input[data-pick]").click()
        entry_row(page, scratch + "/f1.txt").locator("td").last.click(button="right")
        page.wait_for_selector("#row-menu:not(.hidden)", timeout=5000)
        page.click('#row-menu button[data-menu="remote-download"]')
        page.wait_for_selector("#pickbox:not(.hidden)", timeout=5000)
        jump(src)
        page.click("#pick-ok")
        page.wait_for_selector("#toast:not(.hidden)", timeout=10000)
        said = page.locator("#toast").inner_text()
        assert "2 downloads started" in said, "it said %r" % said
        return said
    step(page, watch, "Right-click → Download… with two chosen (V-78 #2)", menu_download, toast_ok=True)


def check_running_transfer(page, watch, api, ctx):
    """A transfer while it runs, on the Jobs tab: every file of its source,
    the speed now and on average (V-87). Slowed so it is still running."""
    path = "/remotes/%s/receive?path=%s&name=slow.bin" % (ctx["remote"], urllib.parse.quote(ctx["scratch"]))
    st, body, _ = api.call("POST", path, raw=os.urandom(6 * 1024 * 1024), timeout=600)
    if st != 202:
        return say("fail", "a running transfer on the Jobs tab", "could not start one: %s" % st)
    rid = body["requestId"]
    for _ in range(30):
        st, _, _ = api.post("/jobs/%s/bandwidth" % urllib.parse.quote(rid, safe=""), {"rate": "150k"})
        if st == 200:
            break
        time.sleep(1)
    else:
        return say("skip", "a running transfer on the Jobs tab", "it ended before it could be slowed")
    ctx["slow_job"] = rid

    def on_the_card():
        page.click('#tabs button[data-tab="jobs"]')
        card = job_card(page, rid)
        card.wait_for(timeout=10000)
        page.wait_for_function(
            "(() => { const c = document.querySelector('#tab-jobs [data-job-open=\"%s\"]');"
            " return c && c.closest('.card').innerText.includes('of 1 files'); })()" % rid, timeout=20000)
        line = " ".join(card.inner_text().split())
        page.click('[data-job-open="%s"]' % rid)
        page.wait_for_selector("#resultbox:not(.hidden)", timeout=10000)
        page.wait_for_function("document.getElementById('result-body').innerText.includes('on average')",
                               timeout=15000)
        speed = next((l for l in page.locator("#result-body").inner_text().splitlines() if "/s" in l), "")
        job = next(j for j in api.get("/jobs")[1] if j["requestId"] == rid)
        s = job.get("stats") or {}
        now = sum((f.get("speed") or 0) for f in s.get("transferring") or [])
        assert s.get("sourceFiles") == 1, "sourceFiles %s" % s.get("sourceFiles")
        assert not now or abs((s.get("speed") or 0) - now) < now * 0.5 + 1, \
            "the speed shown %s is not what the files go at (%s)" % (s.get("speed"), now)
        return "card: %s · box: %s" % (line[:90], speed.strip()[:80])
    step(page, watch, "Jobs: a running transfer -- every file, speed now and on average (V-87)", on_the_card)
    api.post("/jobs/%s/stop" % urllib.parse.quote(rid, safe=""))
    ctx["slow_job"] = None


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

    # A folder of the program's machine with something in it, for the
    # transfer form to send: the scratch folder, downloaded there.
    if ctx.get("windows"):
        path = "/remotes/%s/receive?path=%s&name=after.cmd" % (ctx["remote"], urllib.parse.quote(ctx["scratch"]))
        st, body, _ = api.call("POST", path, raw=b"@echo mounted on %5\r\n@exit /b 0\r\n")
        if st == 202:
            api.wait_job(body["requestId"])
    root = verify_live.local_root(api, {"container": ctx.get("container"), "local_root": None})
    if root:
        st, body, _ = api.post("/remotes/%s/download" % ctx["remote"],
                               {"path": ctx["scratch"], "isDir": True, "to": root})
        if st == 202 and (api.wait_job(body["requestId"]) or {}).get("state") == "finished":
            ctx["local_src"] = root.rstrip("/\\") + "/" + ctx["scratch"]


def clean_up(api, ctx):
    if ctx.get("task") and not ctx.get("task_gone"):
        for entry in (api.get("/queue")[1] or []):
            if entry.get("taskId") == ctx["task"]:
                api.call("DELETE", "/queue/" + urllib.parse.quote(entry["requestId"], safe=""))
        api.call("DELETE", "/tasks/" + urllib.parse.quote(ctx["task"], safe=""))
    for entry in (api.get("/queue")[1] or []):
        if entry.get("taskId") in ctx.get("form_tasks", []):
            api.call("DELETE", "/queue/" + urllib.parse.quote(entry["requestId"], safe=""))
    for task in ctx.get("form_tasks", []):
        api.call("DELETE", "/tasks/" + urllib.parse.quote(task, safe=""))
    for job in ("serve_job", "mount_job", "slow_job"):
        if ctx.get(job):
            api.post("/jobs/%s/stop" % urllib.parse.quote(ctx[job], safe=""))
    if ctx.get("ui_schedule"):
        api.call("DELETE", "/schedules/" + urllib.parse.quote(ctx["ui_schedule"], safe=""))
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
    if ctx.get("local_src"):
        say("look", "left on the program's machine (delete it by hand)", ctx["local_src"])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", default="http://127.0.0.1:19991")
    parser.add_argument("--token", default=os.environ.get("RB_TOKEN", ""))
    parser.add_argument("--container", help="read the token from this container (e.g. rclonebrowser-dev)")
    parser.add_argument("--remote", required=True, help="a remote to make the scratch folder on")
    parser.add_argument("--show", action="store_true", help="open the browser window, to watch it")
    parser.add_argument("--app-js", help="use this app.js instead of the one the program serves: "
                        "a change to the page, tried without building anything")
    parser.add_argument("--from-source", action="store_true",
                        help="the page from src/webui (index.html, app.js, app.css) instead of the one "
                             "built into the program: a change to it, tried without building anything")
    parser.add_argument("--only", help="run only these groups of steps, comma-separated "
                        "(tabs, queue, remote, transfer_form, ..., phone) -- while working on one; "
                        "the groups a change touches; the whole run now and then")
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
           "container": args.container,
           "windows": not args.container and urllib.parse.urlparse(args.base).hostname in ("127.0.0.1", "localhost")
                      and os.name == "nt",
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
            served = {}
            if args.from_source:
                webui = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "webui")
                served = {"/": "index.html", "/index.html": "index.html",
                          "/app.js": "app.js", "/app.css": "app.css"}
                served = {k: os.path.join(webui, v) for k, v in served.items()}
            if args.app_js:
                served["/app.js"] = args.app_js
            kinds = {".html": "text/html; charset=utf-8", ".js": "application/javascript",
                     ".css": "text/css"}

            def from_disk(route):
                path = urllib.parse.urlparse(route.request.url).path
                if path not in served:
                    return route.continue_()
                with open(served[path], "rb") as f:
                    body = f.read()
                route.fulfill(status=200, body=body,
                              headers={"Content-Type": kinds[os.path.splitext(served[path])[1]]})
            if served:
                context.route(ctx["base"] + "/**", from_disk)
                print("the page from %s\n" % ", ".join(sorted(set(served.values()))))
            page = context.new_page()
            watch = Watch(page)

            def load():
                page.goto(ctx["base"] + "/#/remotes")
                page.wait_for_selector("#app:not(.hidden)", timeout=WAIT)
                return page.title()
            step(page, watch, "the page opens, signed in", load)

            # In order: some lean on what one before them made (the remote
            # steps make the folders the transfer form sends, and so on).
            checks = [
                ("tabs", lambda: check_tabs(page, watch)),
                ("queue", lambda: check_queue(page, watch, api, ctx)),
                ("remote", lambda: check_remote(page, watch, api, ctx)),
                ("transfer_form", lambda: check_transfer_form(page, watch, api, ctx)),
                ("history", lambda: check_history_and_addresses(page, watch, api, ctx)),
                ("copy_move", lambda: check_copy_move(page, watch, api, ctx)),
                ("browse", lambda: check_browse_and_menu(page, watch, api, ctx)),
                ("serve", lambda: check_serve_and_stop(page, watch, api, ctx)),
                ("running_transfer", lambda: check_running_transfer(page, watch, api, ctx)),
                ("mount", lambda: check_mount(page, watch, api, ctx)),
                ("schedules", lambda: check_schedules(page, watch, api, ctx)),
                ("many", lambda: check_many(page, watch, api, ctx)),
                ("menus", lambda: check_menus(page, watch, api, ctx)),
                ("new_remote", lambda: check_new_remote_form(page, watch, api, ctx)),
                ("api_tab", lambda: check_api_tab(page, watch, api, ctx)),
                ("log_level", lambda: check_log_level_setting(page, watch, api, ctx)),
                ("backup", lambda: check_backup_and_schedule(page, watch, api, ctx)),
                ("tasks", lambda: check_tasks(page, watch, api, ctx)),
                ("phone", lambda: check_phone(page, watch, ctx)),
            ]
            only = set(filter(None, (args.only or "").split(",")))
            unknown = only - {name for name, _ in checks}
            if unknown:
                sys.exit("no such group: %s (there are: %s)" % (
                    ", ".join(sorted(unknown)), ", ".join(name for name, _ in checks)))
            for name, run in checks:
                if not only or name in only:
                    run()
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
