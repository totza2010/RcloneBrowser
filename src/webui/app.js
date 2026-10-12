"use strict";

// The Web UI's whole conversation with the daemon. See docs/WEB-UI.md.
//
// Three things here are deliberate and worth knowing before changing anything:
//
//   * the secret travels in the Authorization header and nowhere else. It is
//     never put in a URL, because URLs end up in logs, in history and in the
//     Referer of anything the page links to.
//   * that secret is either a session, handed out for the administrator
//     password, or the token the daemon printed. The page cannot tell them
//     apart and does not need to: both are "Bearer <something>".
//   * the event stream is read with fetch() rather than EventSource, because
//     EventSource cannot send a header. The alternative -- accepting the
//     secret as a query parameter for that one endpoint -- would have put it
//     in a URL after all.

const KEY = "rb.token";
const THEME_KEY = "rb.theme";

let secret = localStorage.getItem(KEY) || "";
let tab = "jobs";
let logFor = "";
let auth = { passwordSet: false, user: "admin" };

const state = {
  system: {},
  jobs: [],
  queue: [],
  tasks: [],
  schedules: [],
  history: [],
  remotes: [],
};

// Where the file browser is standing.
const browsing = {
  remote: "", path: "", entries: [], error: "", loading: false,
  // The row a command would act on. One for now; deleting will want several.
  chosen: "",
};

const $ = (id) => document.getElementById(id);

// Everything that came from outside goes through this before it becomes
// HTML. Task names, remote paths and file names are all somebody else's text.
function esc(value) {
  return String(value === undefined || value === null ? "" : value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function bytes(n) {
  if (n === undefined || n === null || n < 0) return "";
  if (!n) return "0 B";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024;
    i += 1;
  }
  return (i === 0 ? n : n.toFixed(1)) + " " + units[i];
}

function seconds(s) {
  if (!s && s !== 0) return "";
  const m = Math.floor(s / 60);
  const r = Math.floor(s % 60);
  return m ? m + "m " + r + "s" : r + "s";
}

function when(iso) {
  if (!iso) return "";
  const d = new Date(iso);
  return isNaN(d) ? iso : d.toLocaleString();
}

// ---- light or dark --------------------------------------------------------

function applyTheme(theme) {
  document.documentElement.setAttribute("data-theme", theme);
  const button = $("theme-toggle");
  if (button) button.textContent = theme === "light" ? "Dark" : "Light";
}

function startTheme() {
  // A choice already made wins. Otherwise follow the machine: somebody whose
  // desktop is light did not ask for a dark page.
  const stored = localStorage.getItem(THEME_KEY);
  if (stored) {
    applyTheme(stored);
    return;
  }
  const dark = window.matchMedia
    ? window.matchMedia("(prefers-color-scheme: dark)").matches
    : true;
  applyTheme(dark ? "dark" : "light");
}

// ---- talking to the daemon ------------------------------------------------

async function api(method, path, body) {
  const options = { method: method, headers: {} };
  if (secret) {
    options.headers["Authorization"] = "Bearer " + secret;
  }
  if (body !== undefined) {
    options.headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(body);
  }

  const response = await fetch("/api/v1" + path, options);
  const text = await response.text();

  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch (e) {
    data = text; // a log comes back as plain text
  }
  return { status: response.status, data: data };
}

// A command, with whatever the daemon said if it refused. Its refusals carry
// reasons -- "that task is already running", "its last run has not finished"
// -- and showing them is the entire difference between this and a UI where
// nothing happens and nobody knows why.
async function command(method, path, body) {
  try {
    const answer = await api(method, path, body);
    if (answer.status >= 400) {
      toast(reasonFrom(answer));
      return false;
    }
    return true;
  } catch (e) {
    toast("could not reach the daemon");
    return false;
  }
}

function reasonFrom(answer) {
  if (answer.data && answer.data.error) return answer.data.error;
  return "the daemon answered " + answer.status;
}

let toastTimer = 0;
function toast(message) {
  const box = $("toast");
  box.textContent = message;
  box.classList.remove("hidden");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => box.classList.add("hidden"), 6000);
}

// ---- the gate -------------------------------------------------------------

async function secretWorks() {
  if (!secret) return false;
  try {
    const answer = await api("GET", "/system");
    return answer.status === 200;
  } catch (e) {
    return false;
  }
}

async function readAuth() {
  try {
    const answer = await api("GET", "/auth");
    if (answer.status === 200 && answer.data) auth = answer.data;
  } catch (e) {
    // leave the defaults; the forms below still work
  }
}

function showGate(message, form) {
  $("app").classList.add("hidden");
  $("gate").classList.remove("hidden");
  $("gate-error").textContent = message || "";

  const wanted = form || (auth.passwordSet ? "login" : "token");
  $("login-form").classList.toggle("hidden", wanted !== "login");
  $("token-form").classList.toggle("hidden", wanted !== "token");
  $("use-password").classList.toggle("hidden", !auth.passwordSet);
  $("token-why").textContent = auth.passwordSet
    ? "Paste the token the daemon printed when it started."
    : "No password has been set. Paste the token the daemon printed when it started.";

  $("login-user").value = auth.user || "admin";
  ($(wanted === "login" ? "login-password" : "gate-token")).focus();
}

$("login-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const answer = await api("POST", "/login", {
    user: $("login-user").value,
    password: $("login-password").value,
  });

  if (answer.status !== 200) {
    // The daemon's own words, including how long a locked-out guesser has to
    // wait. Replacing them with "sign-in failed" would hide the one thing
    // worth knowing.
    $("gate-error").textContent = reasonFrom(answer);
    return;
  }

  secret = answer.data.session;
  localStorage.setItem(KEY, secret);
  $("login-password").value = "";
  start();
});

$("token-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  secret = $("gate-token").value.trim();
  if (!(await secretWorks())) {
    showGate("that token was not accepted", "token");
    return;
  }
  localStorage.setItem(KEY, secret);
  start();
});

$("use-token").addEventListener("click", () => showGate("", "token"));
$("use-password").addEventListener("click", () => showGate("", "login"));

$("sign-out").addEventListener("click", async () => {
  // Told to the daemon as well as forgotten here, so the session cannot be
  // used again by anything that copied it.
  try {
    await api("POST", "/logout");
  } catch (e) {
    // going anyway
  }
  localStorage.removeItem(KEY);
  secret = "";
  location.reload();
});

// ---- the password ---------------------------------------------------------

function openPassword() {
  $("passwordbox").classList.remove("hidden");
  $("current-wrap").classList.toggle("hidden", !auth.passwordSet);
  $("password-title").textContent = auth.passwordSet
    ? "Change the administrator password"
    : "Set an administrator password";
  $("password-error").textContent = "";
  $("password-new").value = "";
  $("password-again").value = "";
  $("password-current").value = "";
}

$("password-open").addEventListener("click", openPassword);
$("password-open-2").addEventListener("click", openPassword);
$("password-close").addEventListener("click", () =>
  $("passwordbox").classList.add("hidden"));

$("password-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const wanted = $("password-new").value;

  if (wanted !== $("password-again").value) {
    $("password-error").textContent = "the two do not match";
    return;
  }

  const answer = await api("POST", "/password", {
    current: $("password-current").value,
    new: wanted,
  });

  if (answer.status !== 200) {
    $("password-error").textContent = reasonFrom(answer);
    return;
  }

  // Setting the password signed everybody out, this page included. The
  // daemon handed back a session so that whoever just set it is not thrown
  // out of the page they are standing in.
  secret = answer.data.session;
  localStorage.setItem(KEY, secret);
  auth.passwordSet = true;

  $("passwordbox").classList.add("hidden");
  $("no-password").classList.add("hidden");
  toast("the password was set; everyone else was signed out");
});

// ---- the live stream ------------------------------------------------------

function setLive(on) {
  $("live").classList.toggle("live", on);
  $("live").title = on ? "listening for events" : "not connected";
}

// Reads text/event-stream by hand. Reconnects on its own: a laptop that slept
// comes back to a dead socket, and a dashboard that needs a reload after every
// nap is a dashboard nobody leaves open.
async function listen() {
  for (;;) {
    try {
      const response = await fetch("/api/v1/events", {
        headers: { "Authorization": "Bearer " + secret },
      });
      if (response.status === 401) {
        setLive(false);
        await readAuth();
        showGate("the session ended");
        return;
      }

      setLive(true);
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";

      for (;;) {
        const chunk = await reader.read();
        if (chunk.done) break;
        buffer += decoder.decode(chunk.value, { stream: true });

        let cut;
        while ((cut = buffer.indexOf("\n\n")) >= 0) {
          handleEvent(buffer.slice(0, cut));
          buffer = buffer.slice(cut + 2);
        }
      }
    } catch (e) {
      // fall through to the wait below
    }

    setLive(false);
    await new Promise((done) => setTimeout(done, 3000));
  }
}

function handleEvent(block) {
  let name = "";
  let payload = "";
  for (const line of block.split("\n")) {
    if (line.startsWith("event: ")) name = line.slice(7).trim();
    else if (line.startsWith("data: ")) payload += line.slice(6);
    // a line starting with ':' is the keep-alive comment, and is ignored
  }
  if (!name || !payload) return;

  let data;
  try {
    data = JSON.parse(payload);
  } catch (e) {
    return;
  }

  if (name === "job") {
    const at = state.jobs.findIndex((j) => j.requestId === data.requestId);
    if (at >= 0) state.jobs[at] = data;
    else state.jobs.unshift(data);

    // A job that ended changes the queue and the history too, and neither of
    // those announces itself in as much detail.
    if (!data.running) refresh();
    else draw();
    return;
  }

  if (name === "queue" || name === "schedule" || name === "hello") {
    refresh();
  }
}

// ---- reading the current state -------------------------------------------

async function refresh() {
  const [system, jobs, queue, tasks, schedules, history, remotes] =
    await Promise.all([
      api("GET", "/system"),
      api("GET", "/jobs"),
      api("GET", "/queue"),
      api("GET", "/tasks"),
      api("GET", "/schedules"),
      api("GET", "/history?limit=100"),
      api("GET", "/remotes"),
    ]);

  if (system.status === 401) {
    await readAuth();
    showGate("the session ended");
    return;
  }

  state.system = system.data || {};
  state.jobs = jobs.data || [];
  state.queue = queue.data || [];
  state.tasks = tasks.data || [];
  state.schedules = schedules.data || [];
  state.history = history.data || [];
  state.remotes = remotes.data || [];
  draw();
}

// ---- drawing --------------------------------------------------------------

function draw() {
  const s = state.system;
  $("version").textContent = s.rcloneVersion ? "rclone " + s.rcloneVersion : "";
  $("summary").textContent =
    (s.runningJobs || 0) + " running · " + (s.queued || 0) + " queued";
  $("queue-toggle").textContent = s.queueRunning ? "Pause queue" : "Start queue";
  $("no-password").classList.toggle("hidden", auth.passwordSet);

  drawJobs();
  drawQueue();
  drawTasks();
  drawRemotes();
  drawSchedules();
  drawHistory();
}

function jobCard(job) {
  const stats = job.stats || {};
  const done = stats.totalBytes ? (stats.bytes / stats.totalBytes) * 100 : 0;
  const status = job.running ? "running" : (job.state || "finished");

  const figures = [];
  if (stats.totalBytes) {
    figures.push(bytes(stats.bytes) + " of " + bytes(stats.totalBytes));
  } else if (stats.bytes) {
    figures.push(bytes(stats.bytes));
  }
  if (stats.transfers) figures.push(stats.transfers + " files");
  if (stats.errors) figures.push(stats.errors + " errors");
  if (stats.elapsedSeconds) figures.push(seconds(stats.elapsedSeconds));
  figures.push("started by " + esc(job.startedBy || "?"));

  return (
    '<div class="card">' +
      '<div class="card-head">' +
        '<span class="name">' + esc(job.info || job.kind) + "</span>" +
        '<span class="tag ' + esc(status) + '">' + esc(status) + "</span>" +
        (job.dryRun ? '<span class="tag">dry run</span>' : "") +
        '<div class="card-actions">' +
          '<button class="ghost small" data-log="' + esc(job.requestId) + '">Log</button>' +
          (job.running
            ? '<button class="danger small" data-stop="' + esc(job.requestId) + '">Stop</button>'
            : "") +
        "</div>" +
      "</div>" +
      '<div class="path">' + esc(job.source) + " → " + esc(job.dest) + "</div>" +
      (job.running && stats.totalBytes
        ? '<div class="bar"><span style="width:' + done.toFixed(1) + '%"></span></div>'
        : "") +
      '<div class="figures"><span>' + figures.join("</span><span>") + "</span></div>" +
    "</div>"
  );
}

// Running first, then the newest. The daemon hands jobs over in the order
// they were started and a live event put a new one at the top, so the same
// list came out in two different orders depending on how it had arrived.
function jobOrder(a, b) {
  if (a.running !== b.running) return a.running ? -1 : 1;
  return String(b.startedAt || "").localeCompare(String(a.startedAt || ""));
}

function drawJobs() {
  $("tab-jobs").innerHTML = state.jobs.length
    ? state.jobs.slice().sort(jobOrder).map(jobCard).join("")
    : '<p class="empty">Nothing is running.</p>';
}

function drawQueue() {
  const rows = state.queue.map((entry) => {
    const task = state.tasks.find((t) => t.id === entry.taskId);
    return (
      "<tr>" +
        "<td>" + esc(task ? task.name : entry.taskId) + "</td>" +
        "<td>" + (entry.running ? '<span class="tag running">running</span>' : "waiting") + "</td>" +
        "<td>" + (entry.dryRun ? "dry run" : "") + "</td>" +
        '<td style="text-align:right">' +
          (entry.running
            ? ""
            : '<button class="ghost small" data-unqueue="' + esc(entry.requestId) + '">Remove</button>') +
        "</td>" +
      "</tr>"
    );
  });

  $("tab-queue").innerHTML = rows.length
    ? "<table><thead><tr><th>Task</th><th>State</th><th></th><th></th></tr></thead><tbody>" +
      rows.join("") + "</tbody></table>"
    : '<p class="empty">The queue is empty.</p>';
}

function drawTasks() {
  const cards = state.tasks.map((task) => (
    '<div class="card">' +
      '<div class="card-head">' +
        '<span class="name">' + esc(task.name) + "</span>" +
        '<span class="muted">' + (task.isDownload ? "download" : "upload") + "</span>" +
        '<div class="card-actions">' +
          '<button class="small" data-run="' + esc(task.id) + '">Run</button>' +
          '<button class="ghost small" data-enqueue="' + esc(task.id) + '">Queue</button>' +
          '<button class="ghost small" data-dry="' + esc(task.id) + '">Dry run</button>' +
        "</div>" +
      "</div>" +
      '<div class="path">' + esc(task.source) + " → " + esc(task.dest) + "</div>" +
    "</div>"
  ));

  $("tab-tasks").innerHTML = cards.length
    ? cards.join("")
    : '<p class="empty">No saved tasks. They are made in the desktop window.</p>';
}

function drawSchedules() {
  const rows = state.schedules.map((s) => (
    "<tr>" +
      "<td>" + esc(s.name) + "</td>" +
      "<td>" + (s.active ? "active" : '<span class="muted">paused</span>') + "</td>" +
      "<td>" + esc(when(s.nextRun)) + "</td>" +
      "<td>" + esc(s.lastStatus || "") + "</td>" +
      '<td style="text-align:right">' +
        '<button class="ghost small" data-fire="' + esc(s.id) + '">Run now</button>' +
      "</td>" +
    "</tr>"
  ));

  $("tab-schedules").innerHTML = rows.length
    ? "<table><thead><tr><th>Schedule</th><th>State</th><th>Next run</th><th>Last</th><th></th></tr></thead><tbody>" +
      rows.join("") + "</tbody></table>"
    : '<p class="empty">No schedules.</p>';
}

function duration(row) {
  if (!row.startedAt || !row.finishedAt) return "";
  const took = (new Date(row.finishedAt) - new Date(row.startedAt)) / 1000;
  return isNaN(took) || took < 0 ? "" : seconds(took);
}

function drawHistory() {
  const rows = state.history.map((row) => (
    "<tr>" +
      "<td>" + esc(when(row.startedAt)) + "</td>" +
      "<td>" + esc(row.taskName || row.kind) + "</td>" +
      "<td>" + esc(row.kind) + (row.dryRun ? " (dry run)" : "") + "</td>" +
      "<td>" + esc(row.startedBy) + "</td>" +
      '<td><span class="tag ' + esc(row.state) + '">' + esc(row.state) + "</span></td>" +
      "<td>" + esc(duration(row)) + "</td>" +
      "<td>" + bytes(row.bytes) + "</td>" +
      '<td style="text-align:right">' +
        '<button class="ghost small" data-log="' + esc(row.requestId) + '">Log</button>' +
      "</td>" +
    "</tr>"
  ));

  $("tab-history").innerHTML = rows.length
    ? "<table><thead><tr><th>Started</th><th>Task</th><th>Kind</th><th>Started by</th><th>Result</th><th>Took</th><th>Bytes</th><th></th></tr></thead><tbody>" +
      rows.join("") + "</tbody></table>"
    : '<p class="empty">Nothing has run yet.</p>';
}

// ---- remotes and their folders -------------------------------------------

function drawRemotes() {
  $("remote-list").innerHTML = state.remotes.length
    ? state.remotes.map((r) => (
        '<button data-remote="' + esc(r.name) + '"' +
          (r.name === browsing.remote ? ' class="on"' : "") + ">" +
          esc(r.name) +
          '<span class="type">' + esc(r.type) + "</span>" +
          '<span class="type">' + esc(lastRunLine("↑", r.lastUpload)) + "</span>" +
        "</button>"
      )).join("")
    : '<p class="empty">No remotes. They are configured with rclone itself.</p>';

  $("browser").classList.toggle("hidden", !browsing.remote);
  if (!browsing.remote) return;

  const chosen = state.remotes.find((r) => r.name === browsing.remote);
  $("remote-info").innerHTML = chosen
    ? runDetails("Last upload", chosen.lastUpload) +
      runDetails("Last download", chosen.lastDownload)
    : "";

  drawCrumbs();
  drawListing();
}

// The one line under a remote's name: when it was last uploaded to and how
// long that took. The same line the desktop window puts there, from the same
// row of the history.
function lastRunLine(arrow, run) {
  if (!run) return arrow + " never";
  const started = new Date(run.startedAt);
  let line = arrow + " " + started.toLocaleString(undefined, {
    day: "numeric", month: "short", hour: "2-digit", minute: "2-digit",
  });
  if (run.durationSeconds === null || run.durationSeconds === undefined) {
    return line + " · running";
  }
  line += " · " + seconds(run.durationSeconds);
  // Said only when it did not go well: "finished" on every remote is noise.
  if (run.state !== "finished") line += " · " + run.state;
  // How much went up that time. Left off when nothing did.
  if (run.bytes > 0) line += " · " + bytes(run.bytes);
  return line;
}

// The whole of one run: began, ended, how long, how it went.
function runDetails(heading, run) {
  if (!run) {
    return '<div class="card"><div class="card-head"><span class="name">' +
      esc(heading) + '</span><span class="muted">never</span></div></div>';
  }
  const still = run.durationSeconds === null || run.durationSeconds === undefined;
  return (
    '<div class="card">' +
      '<div class="card-head">' +
        '<span class="name">' + esc(heading) + "</span>" +
        '<span class="tag ' + esc(run.state) + '">' + esc(run.state) + "</span>" +
        '<div class="card-actions">' +
          '<button class="ghost small" data-log="' + esc(run.requestId) + '">Log</button>' +
        "</div>" +
      "</div>" +
      '<div class="path">' + esc(run.taskName) + " · " + esc(run.source) + " → " + esc(run.dest) + "</div>" +
      '<div class="figures">' +
        "<span>started " + esc(when(run.startedAt)) + "</span>" +
        "<span>" + (still ? "still running" : "finished " + esc(when(run.finishedAt))) + "</span>" +
        (still ? "" : "<span>took " + esc(seconds(run.durationSeconds)) + "</span>") +
        "<span>sent " + bytes(run.bytes) +
          (run.transfers ? " in " + run.transfers + " file(s)" : "") + "</span>" +
      "</div>" +
    "</div>"
  );
}

function drawCrumbs() {
  const parts = browsing.path ? browsing.path.split("/") : [];
  let walked = "";

  const crumbs = ['<button data-go="">' + esc(browsing.remote) + ":</button>"];
  for (const part of parts) {
    walked = walked ? walked + "/" + part : part;
    crumbs.push("<span>/</span>");
    crumbs.push('<button data-go="' + esc(walked) + '">' + esc(part) + "</button>");
  }
  $("crumbs").innerHTML = crumbs.join("");
}

function drawListing() {
  if (browsing.loading) {
    $("listing").innerHTML = '<p class="empty">reading…</p>';
    return;
  }

  if (browsing.error) {
    // The reason, in the place the files would have been. A folder that will
    // not open used to show as an empty list, which reads as "there is
    // nothing here" -- a different thing entirely.
    $("listing").innerHTML =
      '<p class="empty">Could not open this folder.</p>' +
      '<p class="empty error" style="text-align:center">' + esc(browsing.error) + "</p>";
    return;
  }

  if (!browsing.entries.length) {
    $("listing").innerHTML = '<p class="empty">This folder is empty.</p>';
    return;
  }

  // Folders first, then by name, the way every file manager does it.
  const sorted = browsing.entries.slice().sort((a, b) => {
    if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
    return a.name.localeCompare(b.name);
  });

  const rows = sorted.map((entry) => (
    '<tr data-entry="' + esc(entry.path) + '"' +
      (entry.path === browsing.chosen ? ' class="chosen"' : "") + ">" +
      "<td>" +
        (entry.isDir
          ? '<button class="entry-name dir" data-go="' + esc(entry.path) + '">📁 ' + esc(entry.name) + "</button>"
          : '<span class="entry-name file">📄 ' + esc(entry.name) + "</span>") +
      "</td>" +
      "<td>" + (entry.isDir ? "" : bytes(entry.size)) + "</td>" +
      "<td>" + esc(when(entry.modTime)) + "</td>" +
    "</tr>"
  ));

  $("listing").innerHTML =
    "<table><thead><tr><th>Name</th><th>Size</th><th>Modified</th></tr></thead><tbody>" +
    rows.join("") + "</tbody></table>";
  drawChosen();
}

// What the commands that act on one thing are allowed to do right now.
function drawChosen() {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  $("remote-rename").disabled = !entry;
  $("remote-delete").disabled = !entry;
  $("remote-chosen").textContent = entry ? entry.name : "";
  for (const row of document.querySelectorAll("#listing tbody tr")) {
    row.classList.toggle("chosen", row.dataset.entry === browsing.chosen);
  }
}

// Clicking a row chooses it. Clicking a folder's name still opens it, which
// is handled with the other buttons below and never reaches here.
$("listing").addEventListener("click", (event) => {
  if (event.target.closest("button")) return;
  const row = event.target.closest("tr[data-entry]");
  if (!row) return;
  browsing.chosen = browsing.chosen === row.dataset.entry ? "" : row.dataset.entry;
  drawChosen();
});

async function browse(remote, path) {
  browsing.remote = remote;
  browsing.path = path || "";
  browsing.chosen = "";
  browsing.entries = [];
  browsing.error = "";
  browsing.loading = true;
  drawRemotes();

  const answer = await api(
    "GET",
    "/remotes/" + encodeURIComponent(remote) + "/ls?path=" +
      encodeURIComponent(browsing.path));

  browsing.loading = false;
  if (answer.status !== 200) {
    browsing.error = reasonFrom(answer);
  } else {
    browsing.entries = answer.data.entries || [];
    if (answer.data.truncated) {
      toast("this folder has more entries than were sent");
    }
  }
  drawRemotes();
}

// ---- doing things to a remote's files ------------------------------------
//
// The same commands the desktop window has on a remote's tab, arriving here
// one at a time as each is moved into the core. See docs/WEB-API-PLAN.md.

$("remote-refresh").addEventListener("click", () =>
  browse(browsing.remote, browsing.path));

$("remote-mkdir").addEventListener("click", async () => {
  const where = browsing.remote + ":" + browsing.path;
  const name = prompt("Create folder in " + where);
  if (name === null || !name.trim()) return;

  // Answered when rclone has finished, so the listing read straight after
  // has the new folder in it -- or the reason there is none.
  const made = await command(
    "POST", "/remotes/" + encodeURIComponent(browsing.remote) + "/mkdir",
    { path: browsing.path, name: name });
  if (made) browse(browsing.remote, browsing.path);
});

$("remote-rename").addEventListener("click", async () => {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  if (!entry) return;

  const name = prompt("New name for " + entry.name, entry.name);
  if (name === null || !name.trim() || name.trim() === entry.name) return;

  const renamed = await command(
    "POST", "/remotes/" + encodeURIComponent(browsing.remote) + "/rename",
    { path: entry.path, name: name });
  if (renamed) browse(browsing.remote, browsing.path);
});

$("remote-delete").addEventListener("click", async () => {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  if (!entry) return;

  // Says exactly what will go, and that a folder takes its contents with it.
  // There is no undo, so the question names the thing rather than asking
  // "are you sure?" about something the reader has to remember.
  const what = browsing.remote + ":" + entry.path;
  const question = entry.isDir
    ? "Delete the folder\n\n" + what + "\n\nand everything inside it? This cannot be undone."
    : "Delete the file\n\n" + what + "\n\n? This cannot be undone.";
  if (!confirm(question)) return;

  const gone = await command(
    "POST", "/remotes/" + encodeURIComponent(browsing.remote) + "/delete",
    { path: entry.path, isDir: entry.isDir });
  if (gone) browse(browsing.remote, browsing.path);
});

// ---- asking rclone about a remote ----------------------------------------
//
// Size, Info and Tree change nothing. They take as long as rclone takes, so
// the box opens straight away saying what is being asked, and is filled in
// when the answer comes -- or says why there is none.

function showResult(title, html) {
  $("result-title").textContent = title;
  $("result-body").innerHTML = html;
  $("resultbox").classList.remove("hidden");
}

$("result-close").addEventListener("click", () =>
  $("resultbox").classList.add("hidden"));

function figures(rows) {
  return "<dl>" + rows.map((r) =>
    "<dt>" + esc(r[0]) + "</dt><dd>" + esc(r[1]) + "</dd>").join("") + "</dl>";
}

// What a question is about: the chosen row if there is one, otherwise the
// folder being looked at.
function subject() {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  return entry ? entry.path : browsing.path;
}

async function ask(question, title, path, draw) {
  showResult(title, '<p class="muted">asking rclone…</p>');
  let answer;
  try {
    answer = await api(
      "GET", "/remotes/" + encodeURIComponent(browsing.remote) + "/" + question +
        (path === null ? "" : "?path=" + encodeURIComponent(path)));
  } catch (e) {
    showResult(title, '<p class="error">could not reach the daemon</p>');
    return;
  }
  if (answer.status !== 200) {
    // rclone's own words. A backend that cannot report its quota says so
    // here, which is an answer and not a fault.
    showResult(title, '<pre class="error">' + esc(reasonFrom(answer)) + "</pre>");
    return;
  }
  showResult(title, draw(answer.data));
}

$("remote-size").addEventListener("click", () => {
  const path = subject();
  ask("size", "Size of " + browsing.remote + ":" + path, path, (d) =>
    figures([
      ["Files", String(d.count)],
      ["Total", bytes(d.bytes) + "  (" + d.bytes + " bytes)"],
    ].concat(d.sizeless ? [["Of unknown size", String(d.sizeless)]] : [])));
});

$("remote-tree").addEventListener("click", () => {
  const path = subject();
  ask("tree", "Folders under " + browsing.remote + ":" + path, path, (d) =>
    "<pre>" + esc(d.output) + "</pre>" +
    (d.truncated ? '<p class="muted">There is more than is shown.</p>' : ""));
});

$("remote-about").addEventListener("click", () => {
  ask("about", "About " + browsing.remote + ":", null, (d) => {
    const rows = [];
    // Only what the backend reported. Not every one knows all of these, and
    // a missing figure is not a zero.
    for (const [key, label] of [["total", "Total"], ["used", "Used"],
                                ["free", "Free"], ["trashed", "In the trash"],
                                ["other", "Other"]]) {
      if (typeof d[key] === "number") rows.push([label, bytes(d[key])]);
    }
    if (typeof d.objects === "number") rows.push(["Objects", String(d.objects)]);
    return rows.length ? figures(rows)
                       : '<p class="muted">This backend reported nothing.</p>';
  });
});

// ---- a job's log ----------------------------------------------------------

async function openLog(requestId) {
  logFor = requestId;
  $("logbox").classList.remove("hidden");
  $("log-title").textContent = "Log · " + requestId;
  $("log-body").textContent = "reading…";

  const answer = await api(
    "GET", "/jobs/" + encodeURIComponent(requestId) + "/log?tail=1000");
  $("log-body").textContent =
    answer.status === 200 ? (answer.data || "(the log is empty)")
                          : reasonFrom(answer);
}

$("log-close").addEventListener("click", () => $("logbox").classList.add("hidden"));
$("log-refresh").addEventListener("click", () => openLog(logFor));

// ---- what the buttons do --------------------------------------------------

document.addEventListener("click", async (event) => {
  const button = event.target.closest("button");
  if (!button) return;

  const d = button.dataset;
  const task = (id, suffix) => "/tasks/" + encodeURIComponent(id) + suffix;

  if (d.log) return openLog(d.log);
  if (d.remote) return browse(d.remote, "");
  if (d.go !== undefined) return browse(browsing.remote, d.go);

  if (d.run && (await command("POST", task(d.run, "/run")))) return refresh();
  if (d.dry && (await command("POST", task(d.dry, "/run?dryRun=1")))) return refresh();
  if (d.enqueue && (await command("POST", task(d.enqueue, "/enqueue")))) return refresh();

  if (d.stop) {
    const job = state.jobs.find((j) => j.requestId === d.stop);
    if (!confirm("Stop " + (job ? job.info : d.stop) + "?")) return;
    if (await command("POST", "/jobs/" + encodeURIComponent(d.stop) + "/stop")) refresh();
    return;
  }

  if (d.unqueue) {
    if (await command("DELETE", "/queue/" + encodeURIComponent(d.unqueue))) refresh();
    return;
  }

  if (d.fire) {
    if (await command("POST", "/schedules/" + encodeURIComponent(d.fire) + "/run")) refresh();
    return;
  }

  if (button.id === "queue-toggle") {
    const to = state.system.queueRunning ? "/queue/pause" : "/queue/start";
    if (await command("POST", to)) refresh();
    return;
  }

  if (button.id === "theme-toggle") {
    const now = document.documentElement.getAttribute("data-theme") === "light"
      ? "dark" : "light";
    localStorage.setItem(THEME_KEY, now);
    applyTheme(now);
    return;
  }

  if (d.tab) {
    tab = d.tab;
    for (const b of document.querySelectorAll("nav button")) {
      b.classList.toggle("on", b.dataset.tab === tab);
    }
    for (const section of document.querySelectorAll(".tab")) {
      section.classList.toggle("on", section.id === "tab-" + tab);
    }
  }
});

// ---- start ----------------------------------------------------------------

function start() {
  $("gate").classList.add("hidden");
  $("app").classList.remove("hidden");
  refresh();
  listen();

  // Slowly, for the things no event announces: the next run of a schedule
  // moves with the clock, not with anything that happens here.
  setInterval(refresh, 30000);
}

(async function () {
  startTheme();
  await readAuth();
  if (await secretWorks()) start();
  else showGate(secret ? "that no longer works" : "");
})();
