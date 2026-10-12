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
let tab = "remotes";
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

// Jobs started from the file browser, and the folder that was on screen when
// each began -- so it can be read again when the job ends, if it is still
// the one being looked at.
const watching = new Map();

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
  // The pictures of the remotes come in two shades, one for each.
  if ($("remote-list") && state.remotes.length) drawRemotes();
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

    if (!data.running && watching.has(data.requestId)) {
      const was = watching.get(data.requestId);
      watching.delete(data.requestId);
      if (was.remote === browsing.remote && was.path === browsing.path) {
        browse(browsing.remote, browsing.path);
      }
    }

    // A job that ended changes the queue and the history too, and neither of
    // those announces itself in as much detail.
    if (!data.running) refresh();
    else draw();
    return;
  }

  if (name === "queue" || name === "schedule" || name === "hello" || name === "tasks") {
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
          (job.running && job.stream
            ? '<button class="small" data-stream="' + esc(job.requestId) + '">Play</button>'
            : "") +
          (job.running
            ? '<button class="danger small" data-stop="' + esc(job.requestId) + '">' +
              (job.kind === "mount" ? "Unmount" : "Stop") + "</button>"
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
  // A mount is a task too, and is not a transfer: it is shown, and left to
  // the window to change.
  const editable = (task) => ["copy", "move", "sync"].includes(task.operation);

  const cards = state.tasks.map((task) => (
    '<div class="card">' +
      '<div class="card-head">' +
        '<span class="name">' + esc(task.name) + "</span>" +
        '<span class="muted">' + esc(task.type || (task.isDownload ? "download" : "upload")) +
          (task.operation ? " · " + esc(task.operation) : "") + "</span>" +
        '<div class="card-actions">' +
          '<button class="small" data-run="' + esc(task.id) + '">Run</button>' +
          '<button class="ghost small" data-enqueue="' + esc(task.id) + '">Queue</button>' +
          '<button class="ghost small" data-dry="' + esc(task.id) + '">Dry run</button>' +
          (editable(task)
            ? '<button class="ghost small" data-edit="' + esc(task.id) + '">Edit</button>'
            : "") +
          '<button class="danger small" data-forget="' + esc(task.id) + '">Delete</button>' +
        "</div>" +
      "</div>" +
      '<div class="path">' + esc(task.source) + " → " + esc(task.dest) + "</div>" +
    "</div>"
  ));

  $("tab-tasks").innerHTML =
    '<div class="toolbar">' +
      '<button class="ghost small" data-new-task="upload">New upload task</button>' +
      '<button class="ghost small" data-new-task="download">New download task</button>' +
    "</div>" +
    (cards.length ? cards.join("") : '<p class="empty">No saved tasks.</p>');
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

// The picture for a kind of remote: the one the window uses, asked for by
// the kind's name. The lighter drawing is for a dark page. A kind there is
// no picture for is answered with the one for "unknown".
function remoteIcon(type) {
  const dark = document.documentElement.getAttribute("data-theme") !== "light";
  const name = String(type || "unknown").toLowerCase().replace(/[^a-z0-9]+/g, "_");
  return "/icons/remote/" + name + (dark ? "_inv" : "") + ".png";
}

function drawRemotes() {
  $("remote-list").innerHTML = state.remotes.length
    ? state.remotes.map((r) => (
        '<button data-remote="' + esc(r.name) + '"' +
          (r.name === browsing.remote ? ' class="on"' : "") + ">" +
          // Hidden if it will not load, rather than shown as a broken frame.
          '<img class="remote-icon" alt="" src="' + esc(remoteIcon(r.type)) +
            '" onerror="this.style.display=\'none\'">' +
          '<span class="remote-text">' +
            esc(r.name) +
            '<span class="type">' + esc(r.type) + "</span>" +
            '<span class="type">' + esc(lastRunLine("↑", r.lastUpload)) + "</span>" +
          "</span>" +
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
      '<div class="path">' + (run.taskName ? esc(run.taskName) + " · " : "") +
        esc(run.source) + " → " + esc(run.dest) + "</div>" +
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
  $("remote-link").disabled = !entry;
  $("remote-copy").disabled = !entry;
  $("remote-download").disabled = !entry;
  $("remote-move").disabled = !entry;
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

// Emptied as well as hidden: a film left in a hidden box goes on playing.
function closeResult() {
  $("resultbox").classList.add("hidden");
  $("result-body").innerHTML = "";
}
$("result-close").addEventListener("click", closeResult);

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

// ---- choosing a folder ----------------------------------------------------
//
// One chooser for everything that needs a destination. It walks a remote's
// folders the way the listing does, with the same request, and resolves with
// the place chosen or with null when it is closed.

// `local` is the other kind of place: the machine the program runs on. Then
// `files` says whether a file may be chosen as well as a folder, and
// `chosen` is the file that was.
const picking = { remote: "", path: "", done: null, local: false, files: false, chosen: null };

async function pickLoadLocal() {
  $("pick-error").textContent = "";
  $("pick-list").innerHTML = "<p>reading…</p>";
  picking.chosen = null;

  // The path on this machine, a piece at a time. An empty path is the list
  // of places to start from.
  const crumbs = ['<button data-pick="">This machine</button>'];
  if (picking.path) {
    crumbs.push("<span>›</span>");
    crumbs.push("<span>" + esc(picking.path) + "</span>");
  }
  $("pick-crumbs").innerHTML = crumbs.join("");

  let answer;
  try {
    answer = await api("GET", "/local/ls?path=" + encodeURIComponent(picking.path));
  } catch (e) {
    $("pick-list").innerHTML = "<p>could not reach the daemon</p>";
    return;
  }
  if (answer.status !== 200) {
    $("pick-list").innerHTML = "<p>" + esc(reasonFrom(answer)) + "</p>";
    return;
  }
  if (answer.data.path) picking.path = answer.data.path;

  // A way back up, unless this is already where the looking starts.
  const up = picking.path
    ? picking.path.replace(/[\\/]+$/, "").replace(/[\\/][^\\/]*$/, "")
    : null;
  const rows = [];
  if (picking.path) {
    rows.push('<button data-pick="' + esc(up && up !== picking.path ? up : "") + '">↑ up</button>');
  }
  for (const e of answer.data.entries || []) {
    if (e.isDir) {
      rows.push('<button data-pick="' + esc(e.path) + '">📁 ' + esc(e.name) + "</button>");
    } else if (picking.files) {
      rows.push('<button data-pick-file="' + esc(e.path) + '">📄 ' + esc(e.name) +
                ' <span class="muted">' + bytes(e.size) + "</span></button>");
    }
  }
  $("pick-list").innerHTML = rows.length
    ? rows.join("")
    : "<p>Nothing in here.</p>";

  // In a container this is the line: what was mounted in, and nothing else.
  $("pick-error").textContent = "";
  if (!picking.path && answer.data.restricted) {
    $("pick-error").textContent = "";
  }
}

async function pickLoad() {
  if (picking.local) return pickLoadLocal();

  $("pick-error").textContent = "";
  $("pick-list").innerHTML = "<p>reading…</p>";

  const parts = picking.path ? picking.path.split("/") : [];
  let walked = "";
  const crumbs = ['<button data-pick="">' + esc(picking.remote) + ":</button>"];
  for (const part of parts) {
    walked = walked ? walked + "/" + part : part;
    crumbs.push("<span>/</span>");
    crumbs.push('<button data-pick="' + esc(walked) + '">' + esc(part) + "</button>");
  }
  $("pick-crumbs").innerHTML = crumbs.join("");

  let answer;
  try {
    answer = await api("GET", "/remotes/" + encodeURIComponent(picking.remote) +
                       "/ls?path=" + encodeURIComponent(picking.path));
  } catch (e) {
    $("pick-list").innerHTML = "<p>could not reach the daemon</p>";
    return;
  }
  if (answer.status !== 200) {
    // The reason, where the folders would have been.
    $("pick-list").innerHTML = "<p>" + esc(reasonFrom(answer)) + "</p>";
    return;
  }

  const folders = (answer.data.entries || []).filter((e) => e.isDir)
    .sort((a, b) => a.name.localeCompare(b.name));
  $("pick-list").innerHTML = folders.length
    ? folders.map((f) =>
        '<button data-pick="' + esc(f.path) + '">📁 ' + esc(f.name) + "</button>").join("")
    : "<p>No folders in here. This folder can still be chosen.</p>";
}

// Choosing a place on the machine the program runs on. With `files` a file
// may be chosen too, which is what an upload needs.
function pickLocal(title, action, files) {
  picking.local = true;
  picking.files = !!files;
  picking.chosen = null;
  picking.path = "";

  $("pick-title").textContent = title;
  $("pick-ok").textContent = action;
  $("pick-dry").checked = false;
  $("pick-remote").parentElement.classList.add("hidden");
  $("pickbox").classList.remove("hidden");
  pickLoad();

  return new Promise((resolve) => { picking.done = resolve; });
}

function pickFolder(title, action, remote, path) {
  picking.local = false;
  picking.files = false;
  picking.chosen = null;
  $("pick-remote").parentElement.classList.remove("hidden");
  picking.remote = remote;
  picking.path = path || "";

  $("pick-title").textContent = title;
  $("pick-ok").textContent = action;
  $("pick-dry").checked = false;
  $("pick-remote").innerHTML = state.remotes.map((r) =>
    '<option value="' + esc(r.name) + '"' + (r.name === remote ? " selected" : "") +
    ">" + esc(r.name) + " (" + esc(r.type) + ")</option>").join("");
  $("pickbox").classList.remove("hidden");
  pickLoad();

  return new Promise((resolve) => { picking.done = resolve; });
}

function pickClose(result) {
  $("pickbox").classList.add("hidden");
  if (picking.done) picking.done(result);
  picking.done = null;
}

$("pick-remote").addEventListener("change", () => {
  picking.remote = $("pick-remote").value;
  picking.path = "";
  pickLoad();
});
$("pickbox").addEventListener("click", (event) => {
  const file = event.target.closest("button[data-pick-file]");
  if (file) {
    // A file is chosen, not entered: it is marked, and the button below
    // then means this file rather than the folder it is in.
    event.stopPropagation();
    picking.chosen = file.dataset.pickFile;
    for (const b of $("pick-list").querySelectorAll("button")) {
      b.classList.toggle("on", b === file);
    }
    return;
  }

  const button = event.target.closest("button[data-pick]");
  if (!button) return;
  event.stopPropagation();
  picking.path = button.dataset.pick;
  pickLoad();
});
$("pick-cancel").addEventListener("click", () => pickClose(null));
$("pick-ok").addEventListener("click", () => {
  if (picking.local) {
    const path = picking.chosen || picking.path;
    if (!path) {
      $("pick-error").textContent = "choose a folder first";
      return;
    }
    pickClose({ path: path, isFile: !!picking.chosen, dryRun: $("pick-dry").checked });
    return;
  }
  pickClose({ remote: picking.remote, path: picking.path, dryRun: $("pick-dry").checked });
});

// ---- the transfer form ------------------------------------------------------
//
// One form for a transfer started now and for a saved task, because they are
// the same thing: the window's dialog is both, and so is this. Every control
// that stands for a field of a task carries the field's name, so reading the
// form and filling it are each one loop rather than forty lines.

const form = { type: "upload", id: "", isFolder: null, base: "", leaf: "", watch: null };

const formFields = () => Array.from($("taskbox").querySelectorAll("[data-field]"));

// Which end is on a remote: what a download comes from, where an upload goes.
const remoteEnd = () => (form.type === "download" ? "source" : "dest");
const localEnd = () => (form.type === "download" ? "dest" : "source");

// An end, put into its controls. The one on a remote is a remote chosen from
// the ones there are and a path inside it; the other is a path.
function setEnd(which, value) {
  value = value || "";
  const input = $("task-" + which);
  if (which !== remoteEnd()) {
    input.value = value;
    return;
  }

  const at = value.indexOf(":");
  const name = at > 0 ? value.slice(0, at) : "";
  const names = state.remotes.map((r) => r.name);
  const options = names.map((n) =>
    '<option value="' + esc(n) + '">' + esc(n) + ":</option>");
  // A task can outlive its remote. It is shown as it is, and said to be so.
  if (name && !names.includes(name)) {
    options.unshift('<option value="' + esc(name) + '">' + esc(name) + ": — not a remote here</option>");
  }
  if (!name) options.unshift('<option value="">choose a remote…</option>');

  const select = $("task-" + which + "-remote");
  select.innerHTML = options.join("");
  select.value = name;
  input.value = at > 0 ? value.slice(at + 1) : value;
}

function getEnd(which) {
  const typed = $("task-" + which).value.trim();
  if (which !== remoteEnd()) return typed;
  const name = $("task-" + which + "-remote").value;
  return name ? name + ":" + typed.replace(/^\/+/, "") : "";
}

function endNote(which, text, bad) {
  const note = $("task-" + which + "-note");
  note.textContent = text;
  note.classList.toggle("bad", !!bad);
  $("task-" + which).classList.toggle("bad", !!bad && which === localEnd());
  $("task-" + which + "-remote").classList.toggle("bad", !!bad && which === remoteEnd());
}

// Says what the two ends are while they are being typed, rather than after
// the task has been saved with something that is not there in it. The
// daemon checks again when it saves; this is so nobody has to find out then.
async function checkEnds() {
  const far = remoteEnd();
  const near = localEnd();

  const name = $("task-" + far + "-remote").value;
  const known = state.remotes.some((r) => r.name === name);
  endNote(far, !name ? "choose a remote" : known ? "" : name + ": is not a remote here", !known);

  const path = getEnd(near);
  if (!path) {
    endNote(near, "", false);
    return;
  }
  let answer;
  try {
    answer = await api("GET", "/local/stat?path=" + encodeURIComponent(path));
  } catch (e) {
    return;
  }
  if (getEnd(near) !== path) return; // typed on since

  if (answer.status !== 200) {
    endNote(near, reasonFrom(answer), true);
  } else if (form.type === "upload") {
    if (!answer.data.exists) {
      endNote(near, "there is no such file or folder on the machine the program runs on", true);
    } else {
      form.isFolder = answer.data.isDir;
      endNote(near, answer.data.isDir ? "a folder" : "a file", false);
    }
  } else if (answer.data.exists && !answer.data.isDir) {
    endNote(near, "that is a file; a download needs a folder to go into", true);
  } else {
    endNote(near, answer.data.exists ? "a folder" : "not there yet — it will be made", false);
  }
}

let endsTimer = 0;
function endsChanged() {
  clearTimeout(endsTimer);
  endsTimer = setTimeout(checkEnds, 350);
}

function formFill(task) {
  for (const el of formFields()) {
    const value = task[el.dataset.field];
    if (el.type === "checkbox") el.checked = !!value;
    else el.value = value === undefined || value === null ? "" : value;
  }
  for (const radio of document.getElementsByName("task-operation")) {
    radio.checked = radio.value === (task.operation || "copy");
  }
  for (const which of ["source", "dest"]) {
    $("task-" + which + "-remote").classList.toggle("hidden", which !== remoteEnd());
    $("task-" + which).placeholder = which === remoteEnd()
      ? "folder/inside/the/remote — empty is its top"
      : "a path on the machine the program runs on";
    endNote(which, "", false);
    setEnd(which, task[which]);
  }
  formRules();
  drawExtra();
  for (const key of PATTERN_KEYS) drawPatterns(key);
  unitsFill();
  checkEnds();
}

function formRead() {
  const task = { type: form.type };
  for (const el of formFields()) {
    const key = el.dataset.field;
    if (el.type === "checkbox") task[key] = el.checked;
    else if (key === "maxDepth") task[key] = Math.max(0, parseInt(el.value, 10) || 0);
    else task[key] = el.value;
  }
  for (const radio of document.getElementsByName("task-operation")) {
    if (radio.checked) task.operation = radio.value;
  }
  task.source = getEnd("source");
  task.dest = getEnd("dest");
  if (form.isFolder !== null) task.isFolder = form.isFolder;
  return task;
}

// ---- a number and its unit --------------------------------------------------
//
// rclone takes "10M" and "3d" as text, and the window has a box to type them
// into. Here the number is typed and the unit chosen, so "10 MB", "3 days"
// and every other near miss cannot be written. What is saved is the same
// text either way.

const UNIT_KEYS = ["bandwidth", "minSize", "minAge", "maxAge"];
// What a bare number means to rclone, which is what to show for one.
const UNIT_DEFAULT = { bandwidth: "K", minSize: "K", minAge: "s", maxAge: "s" };

const unitPart = (kind, key) =>
  $("taskbox").querySelector("[data-unit-" + kind + '="' + key + '"]');
const unitSaved = (key) =>
  $("taskbox").querySelector('input[type="hidden"][data-field="' + key + '"]');

function unitsFill() {
  for (const key of UNIT_KEYS) {
    const saved = unitSaved(key).value.trim();
    const number = unitPart("number", key);
    const choice = unitPart("choice", key);
    const note = unitPart("note", key);
    number.value = "";
    choice.value = UNIT_DEFAULT[key];
    note.textContent = "";
    note.classList.remove("bad");
    if (!saved) continue;

    // Sizes are the same in either case and with or without "i" and "B"
    // ("1.5Gi", "2mb"); ages are not ("m" is minutes, "M" months).
    const found = /^([0-9]+(?:\.[0-9]+)?)([a-zA-Z]*)$/.exec(saved);
    let unit = found ? found[2] : null;
    if (unit !== null && !key.endsWith("Age")) {
      unit = unit ? unit[0].toUpperCase() : UNIT_DEFAULT[key];
    } else if (unit === "") {
      unit = UNIT_DEFAULT[key];
    }
    const known = unit !== null &&
      Array.from(choice.options).some((o) => o.value === unit);
    if (known) {
      number.value = found[1];
      choice.value = unit;
    } else {
      // Something the window let through that is not one number and a unit:
      // a timetable of rates, "1h30m", a date. Kept as it is until a number
      // is typed over it, and said to be there.
      note.textContent = "set to " + saved + " — kept as it is unless a number is typed here";
    }
  }
}

function unitChanged(key) {
  const number = unitPart("number", key).value.trim();
  const note = unitPart("note", key);
  // Emptying the number un-sets it, unless what is there was never a number
  // in the first place and has not been typed over.
  if (!number && note.textContent) return;
  note.textContent = "";
  unitSaved(key).value = number ? number + unitPart("choice", key).value : "";
}

// ---- patterns: one at a time ------------------------------------------------
//
// Include, exclude and filter are lists, and a list typed into a box one to
// a line is a list with no way to see where one ends -- a trailing space, a
// blank line, two patterns on one line. So each is added on its own and
// shown as a row. What is saved is still one to a line.

const PATTERN_KEYS = ["included", "excluded", "filtered"];

const patternPart = (kind, key) =>
  $("taskbox").querySelector("[data-pattern-" + kind + '="' + key + '"]');
const patternSaved = (key) =>
  $("taskbox").querySelector('textarea[data-field="' + key + '"]');
const patternLines = (key) =>
  patternSaved(key).value.split("\n").map((line) => line.trim()).filter(Boolean);

function drawPatterns(key) {
  const lines = patternLines(key);
  // The order of filter rules is what they mean, so it is shown and can be
  // changed; for the other two it makes no difference.
  const ordered = key === "filtered";
  patternPart("list", key).innerHTML = lines.length
    ? lines.map((line, i) =>
        '<div class="flag-row">' +
          (ordered ? '<span class="order">' + (i + 1) + "</span>" : "") +
          "<code>" + esc(line) + "</code>" +
          '<span class="grow"></span>' +
          (ordered && i > 0
            ? '<button type="button" class="ghost small" data-pattern-up="' + key + ":" + i + '">↑</button>'
            : "") +
          '<button type="button" class="ghost small" data-pattern-drop="' + key + ":" + i + '">Remove</button>' +
        "</div>").join("")
    : '<p class="muted">' + (ordered ? "No rules." : "No patterns.") + "</p>";
}

function patternWrite(key, lines) {
  patternSaved(key).value = lines.join("\n");
  drawPatterns(key);
}

function patternAdd(key) {
  const input = patternPart("new", key);
  const note = patternPart("note", key);
  const sign = patternPart("sign", key);
  let typed = input.value.trim();
  note.textContent = "";
  note.classList.remove("bad");
  if (!typed) return;

  if (sign) {
    // The + or - is the chooser's to say; one typed as well is dropped
    // rather than doubled.
    typed = sign.value + " " + typed.replace(/^[+-]\s+/, "");
  }
  const lines = patternLines(key);
  if (lines.includes(typed)) {
    note.textContent = typed + " is already in the list";
    note.classList.add("bad");
    return;
  }
  lines.push(typed);
  input.value = "";
  patternWrite(key, lines);
  input.focus();
}

$("taskbox").addEventListener("keydown", (event) => {
  const key = event.target.dataset && event.target.dataset.patternNew;
  if (!key || event.key !== "Enter") return;
  event.preventDefault();
  patternAdd(key);
});
$("taskbox").addEventListener("input", (event) => {
  const key = event.target.dataset && event.target.dataset.unitNumber;
  if (key) unitChanged(key);
});
$("taskbox").addEventListener("change", (event) => {
  const key = event.target.dataset && event.target.dataset.unitChoice;
  if (key) unitChanged(key);
});

// ---- extra flags: chosen, not typed ----------------------------------------
//
// The window's box is free text with completion. Here a flag is picked from
// what the rclone on this machine says it has, so one that is misspelt, or
// belongs to another build, cannot get into a task. What is saved is still
// the same text the window keeps, one flag to a line, so a task made in
// either place reads the same in the other.

const flags = { loaded: false, list: [] };

async function loadFlags() {
  if (flags.loaded) return;
  try {
    const answer = await api("GET", "/rclone/flags");
    if (answer.status === 200) {
      flags.list = answer.data.flags || [];
      flags.loaded = !!answer.data.loaded;
    }
  } catch (e) { /* said below, in the pane */ }
  drawExtra();
}

const flagNamed = (name) =>
  flags.list.find((f) => f.name === name || (f.short && f.short === name));

// The remote this transfer is to or from, as the list of remotes has it.
const formRemote = () => {
  const name = $("task-" + remoteEnd() + "-remote").value;
  return state.remotes.find((r) => r.name === name) || null;
};

// Whether a flag means anything to this transfer. Most do to any; a
// backend's own ("--drive-chunk-size") only where that backend is one of
// the two ends -- and one end is always the local disk.
//
// When the remote is a layer over another (crypt, union, alias) what is
// underneath cannot be told from here, so nothing is ruled out.
function flagApplies(flag) {
  if (!flag.backend || flag.backend === "local") return true;
  const remote = formRemote();
  if (!remote || remote.wraps) return true;
  return flag.backend === remote.flagPrefix;
}

// One line into arguments, the way the program does it: split at spaces
// that are not inside quotes, and the quotes dropped.
function splitArgs(line) {
  const args = [];
  let now = "";
  let quoted = false;
  let any = false;
  for (const ch of line) {
    if (ch === '"') {
      quoted = !quoted;
      any = true;
    } else if (!quoted && /\s/.test(ch)) {
      if (now || any) args.push(now);
      now = "";
      any = false;
    } else {
      now += ch;
    }
  }
  if (now || any) args.push(now);
  return args;
}

// The saved text as rows: a flag, and its value if it has one.
function parseExtra(text) {
  const rows = [];
  for (const line of (text || "").split("\n")) {
    for (const arg of splitArgs(line)) {
      const last = rows[rows.length - 1];
      if (last && last.value === null) {
        const flag = flagNamed(last.name);
        const owed = flags.loaded ? !!(flag && flag.takesValue) : !arg.startsWith("-");
        if (owed) {
          last.value = arg;
          continue;
        }
      }
      const equals = arg.indexOf("=");
      if (arg.startsWith("-") && equals > 0) {
        rows.push({ name: arg.slice(0, equals), value: arg.slice(equals + 1) });
      } else {
        rows.push({ name: arg, value: null });
      }
    }
  }
  return rows;
}

function writeExtra(rows) {
  $("task-extra").value = rows.map((row) => {
    if (row.value === null || row.value === "") return row.name;
    return row.name + " " + (/\s/.test(row.value) ? '"' + row.value + '"' : row.value);
  }).join("\n");
  drawExtra();
}

function drawExtra() {
  const rows = parseExtra($("task-extra").value);
  $("flag-list").innerHTML = rows.length
    ? rows.map((row, i) => {
        const flag = flagNamed(row.name);
        const bad = flags.loaded && !flag;
        return '<div class="flag-row' + (bad ? " bad" : "") + '" title="' +
                 esc(flag ? flag.description : "") + '">' +
               "<code>" + esc(row.name) + "</code>" +
               (row.value !== null ? "<span>" + esc(row.value) + "</span>" : "") +
               (bad ? '<span class="error">not a flag this rclone has, or not one a task may be given here</span>' : "") +
               (flag && !flagApplies(flag)
                 ? '<span class="aside">for ' + esc(flag.backend) + " remotes — this one is " +
                   esc(formRemote().type) + ", so rclone will take no notice of it</span>"
                 : "") +
               '<span class="grow"></span>' +
               '<button type="button" class="ghost small" data-flag-drop="' + i + '">Remove</button>' +
               "</div>";
      }).join("")
    : '<p class="muted">No extra flags.</p>';

  const remote = formRemote();
  const fitting = flags.list.filter(flagApplies).length;
  $("flag-state").textContent = flags.loaded
    ? (fitting === flags.list.length || !remote
        ? flags.list.length + " flags to choose from, as this rclone lists them."
        : fitting + " of this rclone's " + flags.list.length + " flags apply to " +
          remote.name + ": (" + remote.type + ") — the general ones and its own.")
    : "The list of flags has not come back from rclone, so a flag can be typed in full; it is checked when the task is saved.";
}

// The list to choose from. Clicking the box opens all of it, to be scrolled
// through; typing narrows it. `open` is whether to show it if it is not
// showing already -- something else changing (the remote, say) redraws a
// list that is open and leaves a closed one closed.
function flagMenu(open) {
  const typed = $("flag-name").value.trim().replace(/^-+/, "").toLowerCase();
  const menu = $("flag-menu");
  if (!flags.loaded || (!open && menu.classList.contains("hidden"))) {
    if (!flags.loaded) menu.classList.add("hidden");
    return;
  }
  const every = $("flag-all").checked;
  const offered = flags.list.filter((flag) => every || flagApplies(flag));

  let found;
  if (!typed) {
    // Everything, with this remote's own first: they are the ones nobody
    // knows by heart, and the reason for looking.
    const remote = formRemote();
    const own = remote ? remote.flagPrefix : "";
    found = offered.filter((f) => own && f.backend === own)
      .concat(offered.filter((f) => !(own && f.backend === own)));
  } else {
    // Names that begin with it first, then anything that mentions it.
    const begins = [];
    const mentions = [];
    for (const flag of offered) {
      const name = flag.name.slice(2);
      if (name.startsWith(typed)) begins.push(flag);
      else if (name.includes(typed) || (flag.description || "").toLowerCase().includes(typed)) {
        mentions.push(flag);
      }
    }
    found = begins.concat(mentions);
  }
  menu.innerHTML = found.length
    ? found.map((flag) =>
        '<button type="button" data-flag="' + esc(flag.name) + '"><b>' + esc(flag.name) + "</b>" +
        (flag.type ? ' <span class="muted">' + esc(flag.type) + "</span>" : "") +
        (flag.backend ? ' <span class="muted">· ' + esc(flag.backend) + "</span>" : "") +
        "<small>" + esc(flag.description) + "</small></button>").join("")
    : '<p class="muted">' + (every || !formRemote()
        ? "No flag of this rclone matches."
        : "No flag that applies to this remote matches.") + "</p>";
  menu.classList.remove("hidden");
  menu.scrollTop = 0;
}

function flagChoose(name) {
  const flag = flagNamed(name);
  $("flag-name").value = name;
  $("flag-menu").classList.add("hidden");
  $("flag-value").disabled = !!flag && !flag.takesValue;
  $("flag-value").value = "";
  $("flag-value").placeholder = flag && flag.takesValue ? flag.type : "takes no value";
  $("flag-help").textContent = flag ? flag.description : "";
  if (flag && flag.takesValue) $("flag-value").focus();
  else $("flag-add").focus();
}

function flagAdd() {
  const name = $("flag-name").value.trim();
  if (!name) return;
  const flag = flagNamed(name);
  if (flags.loaded && !flag) {
    $("flag-help").textContent = name + " is not a flag this rclone has — choose one from the list.";
    flagMenu(true);
    return;
  }
  if (!flag && !name.startsWith("-")) {
    $("flag-help").textContent = "A flag begins with --";
    return;
  }
  const value = $("flag-value").value.trim();
  if (flag && flag.takesValue && !value) {
    $("flag-help").textContent = flag.name + " needs a value: " + flag.type;
    $("flag-value").focus();
    return;
  }

  const rows = parseExtra($("task-extra").value);
  rows.push({
    name: flag ? flag.name : name,
    value: flag && !flag.takesValue ? null : (value || null),
  });
  $("flag-name").value = "";
  $("flag-value").value = "";
  $("flag-value").disabled = true;
  $("flag-value").placeholder = "value";
  $("flag-help").textContent = "";
  writeExtra(rows);
  $("flag-menu").classList.add("hidden");
}

$("flag-name").addEventListener("input", () => {
  // Typing again un-chooses: the value box waits for a flag that takes one.
  const flag = flagNamed($("flag-name").value.trim());
  $("flag-value").disabled = flags.loaded && !(flag && flag.takesValue);
  flagMenu(true);
});
$("flag-name").addEventListener("focus", () => flagMenu(true));
$("flag-name").addEventListener("click", () => flagMenu(true));
$("flag-name").addEventListener("keydown", (event) => {
  if (event.key === "Escape" && !$("flag-menu").classList.contains("hidden")) {
    // Closes the list, not the form.
    event.stopPropagation();
    $("flag-menu").classList.add("hidden");
    return;
  }
  if (event.key !== "Enter") return;
  event.preventDefault();
  const flag = flagNamed($("flag-name").value.trim());
  if (flag && flag.takesValue && !$("flag-value").value) flagChoose(flag.name);
  else flagAdd();
});
$("flag-value").addEventListener("keydown", (event) => {
  if (event.key !== "Enter") return;
  event.preventDefault();
  flagAdd();
});
$("flag-add").addEventListener("click", flagAdd);
$("flag-all").addEventListener("change", () => flagMenu(true));
// A click anywhere else in the form puts the list away.
$("taskbox").addEventListener("mousedown", (event) => {
  if (!event.target.closest("#flag-menu, #flag-name, label:has(#flag-all)")) {
    $("flag-menu").classList.add("hidden");
  }
});

// What only means something for one kind of transfer is greyed for the
// others, as the window does it.
function formRules() {
  const operation = formRead().operation;
  const rule = (id, on) => {
    $(id).disabled = !on;
    const label = $(id).closest("label");
    if (label) label.classList.toggle("off", !on);
  };
  rule("task-timing", operation === "sync");
  rule("task-delete-excluded", operation === "sync");
  rule("task-delete-empty", operation === "move");
}

function formPane(name) {
  for (const b of $("task-tabs").querySelectorAll("button")) {
    b.classList.toggle("on", b.dataset.pane === name);
  }
  for (const pane of $("taskbox").querySelectorAll("section.pane")) {
    pane.classList.toggle("on", pane.dataset.pane === name);
  }
  if (name === "command") formCommand();
}

// The command, asked of the daemon rather than put together here: it is the
// one that will run it, and it adds the options set in the preferences.
async function formCommand() {
  $("task-command").textContent = "…";
  try {
    const answer = await api("POST", "/tasks/preview", formRead());
    $("task-command").textContent =
      answer.status === 200 ? answer.data.command : reasonFrom(answer);
  } catch (e) {
    $("task-command").textContent = "could not reach the daemon";
  }
}

// `preset` is what is already known -- where an upload is going, what a
// download is of. `how` is what the form is told about itself: a title, and
// for one opened from a folder, the folder.
async function openTransfer(type, preset, how) {
  how = how || {};
  form.type = type;
  form.id = how.id || "";
  form.base = how.base || "";
  form.leaf = how.leaf || "";
  form.watch = how.watch || null;
  form.isFolder = preset.isFolder === undefined ? null : !!preset.isFolder;

  let task = preset;
  if (!form.id) {
    // A new one begins where the window's dialog does.
    let defaults = {};
    try {
      const answer = await api("GET", "/tasks/defaults?type=" + type);
      if (answer.status === 200) defaults = answer.data;
    } catch (e) { /* the form still opens, only emptier */ }
    task = Object.assign({}, defaults, preset);
  }

  $("task-title").textContent = how.title ||
    (form.id ? "Edit task" : type === "download" ? "New download task" : "New upload task");
  $("task-where").textContent = type === "download"
    ? "From is a remote, written remote:path. To is a folder on the machine the program runs on — not the one this browser is on, unless they are the same."
    : "From is a file or a folder on the machine the program runs on — not the one this browser is on, unless they are the same. To is a remote, written remote:path.";
  $("task-error").textContent = "";
  $("task-queue").checked = false;

  formFill(task);
  formPane("settings");
  $("taskbox").classList.remove("hidden");
  $("task-" + localEnd()).focus();
  loadFlags();
}

function closeTransfer() {
  $("taskbox").classList.add("hidden");
}

// The chooser, without its dry-run box: here that is one of the buttons.
async function formPick(open) {
  const dry = $("pick-dry").closest("label");
  dry.classList.add("hidden");
  const chosen = await open();
  dry.classList.remove("hidden");
  return chosen;
}

const underName = (folder, name) =>
  name ? folder.replace(/[\\/]+$/, "") + "/" + name : folder;

// The end that is on the machine the program runs on.
async function formBrowseLocal() {
  const upload = form.type === "upload";
  const chosen = await formPick(() => pickLocal(
    upload ? "Upload — choose a file or a folder on the machine the program runs on"
           : "Download — choose a folder on the machine the program runs on",
    "Choose this", upload));
  if (!chosen) return;

  if (!upload) {
    // A folder arrives under its own name, as it does from the window.
    setEnd("dest", underName(chosen.path, form.leaf));
    checkEnds();
    return;
  }
  setEnd("source", chosen.path);
  form.isFolder = !chosen.isFile;
  if (form.base) {
    // And a folder sent lands under its own name too.
    const name = chosen.isFile ? "" : chosen.path.replace(/[\\/]+$/, "").split(/[\\/]/).pop();
    setEnd("dest", name && !form.base.endsWith(":")
      ? form.base + "/" + name
      : form.base + name);
  }
  checkEnds();
}

// The end that is on a remote.
async function formBrowseRemote() {
  const which = remoteEnd();
  const name = $("task-" + which + "-remote").value;
  const known = state.remotes.some((r) => r.name === name);
  const remote = known ? name : (browsing.remote || (state.remotes[0] || {}).name);
  if (!remote) {
    $("task-error").textContent = "there are no remotes to choose from";
    return;
  }
  const chosen = await formPick(() => pickFolder(
    "Choose a folder on a remote", "Choose this", remote,
    known ? $("task-" + which).value.trim() : ""));
  if (!chosen) return;
  setEnd(which, chosen.remote + ":" + chosen.path);
  // Chosen by hand now, so it no longer follows what is being sent.
  if (form.type === "upload") form.base = "";
  checkEnds();
}

$("task-source-browse").addEventListener("click", () =>
  form.type === "upload" ? formBrowseLocal() : formBrowseRemote());
$("task-dest-browse").addEventListener("click", () =>
  form.type === "upload" ? formBrowseRemote() : formBrowseLocal());

for (const id of ["task-source", "task-dest"]) {
  $(id).addEventListener("input", endsChanged);
  $(id + "-remote").addEventListener("change", () => {
    checkEnds();
    drawExtra();
    flagMenu();
  });
}

$("task-tabs").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-pane]");
  if (button) formPane(button.dataset.pane);
});
$("taskbox").addEventListener("change", (event) => {
  if (event.target.name === "task-operation") formRules();
});
$("task-cancel").addEventListener("click", closeTransfer);
$("task-form").addEventListener("submit", (event) => event.preventDefault());

// Saved first, whichever button it was: the window's are "Save and Run" and
// "Save and Dry run", and a run with no task behind it would have nothing to
// be run again from.
async function formSubmit(act) {
  $("task-error").textContent = "";
  const target = form.id ? "/tasks/" + encodeURIComponent(form.id) : "/tasks";

  let saved;
  try {
    saved = await api(form.id ? "PUT" : "POST", target, formRead());
  } catch (e) {
    $("task-error").textContent = "could not reach the daemon";
    return;
  }
  if (saved.status >= 400) {
    $("task-error").textContent = reasonFrom(saved);
    return;
  }
  // From here it exists. If starting it is refused, pressing again changes
  // this one rather than making a second.
  form.id = saved.data.id;
  $("task-name").value = saved.data.name;

  if (act === "save") {
    closeTransfer();
    toast("Task saved: " + saved.data.name);
    refresh();
    return;
  }

  const queue = act === "run" && $("task-queue").checked;
  let started;
  try {
    started = await api("POST",
      "/tasks/" + encodeURIComponent(form.id) + (queue ? "/enqueue" : "/run"),
      act === "dry" ? { dryRun: true } : undefined);
  } catch (e) {
    $("task-error").textContent = "saved, but could not reach the daemon to start it";
    return;
  }
  if (started.status >= 400) {
    $("task-error").textContent = "saved, but not started: " + reasonFrom(started);
    refresh();
    return;
  }

  // The folder on screen is read again when a real upload into it ends.
  if (form.watch && act === "run" && started.data && started.data.requestId) {
    watching.set(started.data.requestId, form.watch);
  }
  closeTransfer();
  toast(queue ? "Saved and added to the queue"
              : act === "dry" ? "Dry run started — see Jobs" : "Started — see Jobs");
  refresh();
}

$("taskbox").addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button) return;
  if (button.dataset.act) return formSubmit(button.dataset.act);
  if (button.dataset.flag) return flagChoose(button.dataset.flag);
  if (button.dataset.flagDrop !== undefined) {
    const rows = parseExtra($("task-extra").value);
    rows.splice(Number(button.dataset.flagDrop), 1);
    return writeExtra(rows);
  }
  if (button.dataset.patternAdd) return patternAdd(button.dataset.patternAdd);
  const at = button.dataset.patternDrop || button.dataset.patternUp;
  if (at) {
    const key = at.split(":")[0];
    const index = Number(at.split(":")[1]);
    const lines = patternLines(key);
    const moved = lines.splice(index, 1)[0];
    if (button.dataset.patternUp) lines.splice(index - 1, 0, moved);
    patternWrite(key, lines);
  }
});

// ---- copying and moving ---------------------------------------------------

async function copyOrMove(move) {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  if (!entry) return;

  const verb = move ? "Move" : "Copy";
  const from = { remote: browsing.remote, path: browsing.path };
  const where = await pickFolder(
    verb + " " + entry.name + " to…", verb + " here", browsing.remote, browsing.path);
  if (!where) return;

  // Started, not finished: this takes as long as the files are large. The
  // Jobs tab shows how it is going, and the folder is read again when it
  // ends if it is still the one on screen.
  let answer;
  try {
    answer = await api("POST", remoteUrl(move ? "move" : "copy"), {
      path: entry.path, isDir: entry.isDir,
      to: where.path, toRemote: where.remote, dryRun: where.dryRun,
    });
  } catch (e) {
    toast("could not reach the daemon");
    return;
  }
  if (answer.status !== 202) {
    toast(reasonFrom(answer));
    return;
  }

  watching.set(answer.data.requestId, from);
  toast(verb + (where.dryRun ? " (dry run)" : "") + " started — see Jobs");
  refresh();
}

// ---- between this machine and the remote ---------------------------------

// Started and reported the way a copy is. The folder on screen is read again
// when an upload into it ends.
async function startTransfer(operation, body, verb) {
  const from = { remote: browsing.remote, path: browsing.path };
  let answer;
  try {
    answer = await api("POST", remoteUrl(operation), body);
  } catch (e) {
    toast("could not reach the daemon");
    return;
  }
  if (answer.status !== 202) {
    toast(reasonFrom(answer));
    return;
  }
  watching.set(answer.data.requestId, from);
  toast(verb + (body.dryRun ? " (dry run)" : "") + " started — see Jobs");
  refresh();
}

// Both open the form the window opens: everything a transfer can be told,
// and whether to keep it as a task.
$("remote-upload").addEventListener("click", () => {
  const into = browsing.remote + ":" + browsing.path;
  openTransfer("upload", { dest: into }, {
    title: "Upload to " + into,
    base: into,
    watch: { remote: browsing.remote, path: browsing.path },
  });
});

$("remote-download").addEventListener("click", () => {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  if (!entry) return;
  openTransfer("download",
    { source: browsing.remote + ":" + entry.path, isFolder: entry.isDir }, {
      title: "Download " + entry.name,
      leaf: entry.isDir ? entry.name : "",
    });
});

$("remote-copy").addEventListener("click", () => copyOrMove(false));
$("remote-move").addEventListener("click", () => copyOrMove(true));

// ---- mounting --------------------------------------------------------------

$("remote-mount").addEventListener("click", () => {
  const path = subject();
  const what = browsing.remote + ":" + path;

  showResult("Mount " + what,
    '<p class="muted">Makes this folder appear as a folder or a drive on the ' +
    "machine the program is running on — not on the one this browser is on, " +
    "unless they are the same.</p>" +
    '<p><label>Where: a drive letter such as <b>X:</b> on Windows, or an empty folder' +
      '<input id="mount-point" type="text" placeholder="X:  or  /mnt/films"></label></p>' +
    '<p><label>Cache<select id="mount-cache">' +
      '<option value="off">off — reading only, the least disk used</option>' +
      '<option value="minimal">minimal</option>' +
      '<option value="writes" selected>writes — what most programs need to save files</option>' +
      '<option value="full">full — everything read is kept on disk</option>' +
    "</select></label></p>" +
    '<p><label class="check"><input type="checkbox" id="mount-ro"> Read only</label></p>' +
    '<p><button id="mount-go" class="small">Mount</button> ' +
    '<span id="mount-state" class="error"></span></p>');

  $("mount-go").addEventListener("click", async () => {
    let answer;
    try {
      answer = await api("POST", remoteUrl("mount"), {
        path: path,
        mountPoint: $("mount-point").value,
        cache: $("mount-cache").value,
        readOnly: $("mount-ro").checked,
      });
    } catch (e) {
      $("mount-state").textContent = "could not reach the daemon";
      return;
    }
    if (answer.status !== 202) {
      $("mount-state").textContent = reasonFrom(answer);
      return;
    }
    $("resultbox").classList.add("hidden");
    // Whether it came up is the job's to say: a mount that fails does so a
    // moment after it starts, and the card in Jobs is where that shows.
    toast("mount started — see Jobs; Stop there unmounts it");
    refresh();
  });
});

// ---- streaming --------------------------------------------------------------
//
// A file of a remote, played here or given an address a player can open.
// The address is one of this same server's, so wherever the page can be
// reached the stream can be, with no second port to open.

const streamUrl = (path) => location.origin + path;

const MEDIA = /\.(mkv|mp4|m4v|avi|mov|webm|ts|m2ts|wmv|flv|mpg|mpeg|mp3|flac|m4a|aac|ogg|opus|wav)$/i;

function saveText(name, text, type) {
  const link = document.createElement("a");
  link.href = URL.createObjectURL(new Blob([text], { type: type }));
  link.download = name;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

// A playlist: what a player opens when it is handed a file rather than an
// address. Opening the .m3u starts whatever plays media on that machine.
function playlist(items) {
  return "#EXTM3U\n" + items.map((item) =>
    "#EXTINF:-1," + item.name + "\n" + item.url + "\n").join("");
}

// The player. `items` is what there is to play -- one file, or the media of
// a folder -- and `requestId` is the job, so it can be stopped from here.
function showStream(title, items, requestId) {
  if (!items.length) {
    showResult("Streaming " + title, '<p class="muted">There are no files in this folder to play.</p>');
    return;
  }

  showResult("Streaming " + title,
    '<video id="stream-player" class="player" controls autoplay playsinline></video>' +
    '<p id="stream-cannot" class="error"></p>' +
    (items.length > 1
      ? '<div id="stream-items" class="pick-list">' + items.map((item, i) =>
          '<button type="button" data-play="' + i + '">' + esc(item.name) + "</button>").join("") + "</div>"
      : "") +
    '<p class="muted">Or in a player of your own — in VLC: Media → Open Network ' +
    "Stream. The address works until the stream is stopped, and anyone who " +
    "has it can play this while it does.</p>" +
    '<p><input id="stream-address" type="text" readonly></p>' +
    '<p><button id="stream-copy" class="ghost small">Copy address</button> ' +
      '<button id="stream-m3u" class="ghost small">Download .m3u</button> ' +
      '<button id="stream-stop" class="danger small">Stop the stream</button></p>');

  const player = $("stream-player");
  let now = 0;

  const play = (index) => {
    now = index;
    $("stream-cannot").textContent = "";
    $("stream-address").value = items[index].url;
    player.src = items[index].url;
    player.play().catch(() => { /* autoplay refused; the controls are there */ });
    const list = $("stream-items");
    if (list) {
      for (const b of list.querySelectorAll("button")) {
        b.classList.toggle("on", Number(b.dataset.play) === index);
      }
    }
  };

  // A browser plays what it has a decoder for, which is less than a player
  // does: MP4 and WebM nearly always, MKV sometimes, and the sound of some
  // films not at all. Said, with what to do about it, rather than left as a
  // black box.
  player.addEventListener("error", () => {
    $("stream-cannot").textContent =
      "This browser cannot play this file. It is still being served — open the address below in VLC or another player.";
  });
  player.addEventListener("ended", () => {
    if (now + 1 < items.length) play(now + 1);
  });
  if ($("stream-items")) {
    $("stream-items").addEventListener("click", (event) => {
      const button = event.target.closest("button[data-play]");
      if (button) {
        event.stopPropagation();
        play(Number(button.dataset.play));
      }
    });
  }

  $("stream-address").addEventListener("focus", (e) => e.target.select());
  $("stream-copy").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText($("stream-address").value);
      toast("address copied");
    } catch (e) {
      // No clipboard without HTTPS on anything but this machine itself.
      $("stream-address").focus();
      toast("select the address and copy it — this browser will not do it for a page served this way");
    }
  });
  $("stream-m3u").addEventListener("click", () =>
    saveText(title.replace(/[\\/:*?"<>|]+/g, "_") + ".m3u", playlist(items), "audio/x-mpegurl"));
  $("stream-stop").addEventListener("click", async () => {
    if (await command("POST", "/jobs/" + encodeURIComponent(requestId) + "/stop")) {
      closeResult();
      toast("stream stopped");
      refresh();
    }
  });

  play(0);
}

// What a folder's stream holds: the media in it, or failing any, every
// file. Read from the same listing the page shows.
async function streamItems(remote, folder, path) {
  let answer;
  try {
    answer = await api("GET", "/remotes/" + encodeURIComponent(remote) +
                       "/ls?path=" + encodeURIComponent(folder));
  } catch (e) {
    return [];
  }
  if (answer.status !== 200) return [];
  const files = (answer.data.entries || []).filter((e) => !e.isDir)
    .sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
  const media = files.filter((e) => MEDIA.test(e.name));
  return (media.length ? media : files).map((e) => ({
    name: e.name,
    url: streamUrl(path + encodeURIComponent(e.name)),
  }));
}

// rclone takes a moment to come up after the job starts, and a player that
// asks before then is told the stream is not answering. So the page waits
// until it is, for a few seconds, before it hands the address to anything.
async function streamReady(path) {
  for (let tries = 0; tries < 30; ++tries) {
    try {
      const answer = await fetch(path, { method: "HEAD" });
      if (answer.status !== 502) return;
    } catch (e) { /* tried again */ }
    await new Promise((resolve) => setTimeout(resolve, 300));
  }
}

$("remote-stream").addEventListener("click", async () => {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  const remote = browsing.remote;
  const path = entry ? entry.path : browsing.path;
  const isDir = entry ? entry.isDir : true;

  let answer;
  try {
    answer = await api("POST", remoteUrl("stream"), { path: path, isDir: isDir });
  } catch (e) {
    toast("could not reach the daemon");
    return;
  }
  if (answer.status !== 202) {
    toast(reasonFrom(answer));
    return;
  }
  const started = answer.data;
  toast("starting the stream…");
  refresh();
  await streamReady(started.path);

  const items = isDir
    ? await streamItems(remote, path, started.path)
    : [{ name: entry.name, url: streamUrl(started.path) }];
  showStream(remote + ":" + path, items, started.requestId);
});

// From a stream's card: the player again.
async function reopenStream(requestId) {
  const job = state.jobs.find((j) => j.requestId === requestId);
  if (!job || !job.stream) return;
  const at = job.source.indexOf(":");
  const folder = job.stream.path.endsWith("/");
  const items = folder
    ? await streamItems(job.source.slice(0, at), job.source.slice(at + 1), job.stream.path)
    : [{ name: job.source.slice(at + 1).split("/").pop(), url: streamUrl(job.stream.path) }];
  showStream(job.source, items, requestId);
}

// ---- comparing two folders, and saving a listing --------------------------

$("remote-check").addEventListener("click", async () => {
  const path = subject();
  const what = browsing.remote + ":" + path;
  const other = await pickFolder(
    "Compare " + what + " with…", "Compare with this folder", browsing.remote, "");
  if (!other) return;

  const query = "?path=" + encodeURIComponent(path) +
    "&to=" + encodeURIComponent(other.path) +
    "&toRemote=" + encodeURIComponent(other.remote);
  const title = "Check " + what + " against " + other.remote + ":" + other.path;

  showResult(title, '<p class="muted">comparing… this reads every file on both sides</p>');
  let answer;
  try {
    answer = await api("GET", "/remotes/" + encodeURIComponent(browsing.remote) + "/check" + query);
  } catch (e) {
    showResult(title, '<p class="error">could not reach the daemon</p>');
    return;
  }
  if (answer.status !== 200) {
    showResult(title, '<pre class="error">' + esc(reasonFrom(answer)) + "</pre>");
    return;
  }

  // A difference is an answer, not a failure, and is said as one.
  const d = answer.data;
  showResult(title,
    "<p>" + (d.matched
      ? '<span class="tag finished">the same</span>'
      : '<span class="tag error">' + d.differences + " difference(s)</span>") +
    (typeof d.matching === "number" ? "  " + d.matching + " matching file(s)" : "") + "</p>" +
    "<pre>" + esc(d.output) + "</pre>");
});

$("remote-export").addEventListener("click", () => {
  const path = subject();
  const what = browsing.remote + ":" + path;

  showResult("Export a list of " + what,
    '<p class="muted">Every file under this folder, as a file to keep.</p>' +
    '<p><select id="export-format">' +
      '<option value="csv">name, modified, size (CSV)</option>' +
      '<option value="txt">names only</option>' +
    "</select></p>" +
    '<p><label>How many levels down (0 for all)' +
      '<input id="export-depth" type="number" min="0" value="0"></label></p>' +
    '<p><button id="export-go" class="small">Download</button> ' +
    '<span id="export-state" class="muted"></span></p>');

  $("export-go").addEventListener("click", async () => {
    const format = $("export-format").value;
    $("export-state").textContent = "listing… this can take a while";

    // Fetched with the header and then handed over as a file. A plain link
    // would be simpler and would need the secret in its address.
    let response;
    try {
      response = await fetch(
        "/api/v1/remotes/" + encodeURIComponent(browsing.remote) + "/export" +
          "?path=" + encodeURIComponent(path) + "&format=" + format +
          "&maxDepth=" + encodeURIComponent($("export-depth").value || "0"),
        { headers: { "Authorization": "Bearer " + secret } });
    } catch (e) {
      $("export-state").textContent = "could not reach the daemon";
      return;
    }
    if (response.status !== 200) {
      let why = "the daemon answered " + response.status;
      try { why = (await response.json()).error || why; } catch (e) { /* as it is */ }
      $("export-state").textContent = why;
      return;
    }

    const blob = await response.blob();
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = (browsing.remote + "-" + (path || "root")).replace(/[^A-Za-z0-9._-]+/g, "_") + "." + format;
    document.body.appendChild(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(link.href);
    $("export-state").textContent = "saved " + bytes(blob.size);
  });
});

// ---- the ones that change something other than a name or a place ---------

// A command whose output is its result, shown in the result box.
async function run(title, path, body, draw) {
  showResult(title, '<p class="muted">running…</p>');
  let answer;
  try {
    answer = await api("POST", path, body);
  } catch (e) {
    showResult(title, '<p class="error">could not reach the daemon</p>');
    return false;
  }
  if (answer.status !== 200) {
    showResult(title, '<pre class="error">' + esc(reasonFrom(answer)) + "</pre>");
    return false;
  }
  showResult(title, draw(answer.data));
  return true;
}

const remoteUrl = (operation) =>
  "/remotes/" + encodeURIComponent(browsing.remote) + "/" + operation;

$("remote-link").addEventListener("click", () => {
  const entry = browsing.entries.find((e) => e.path === browsing.chosen);
  if (!entry) return;

  // Said before it is done, because it cannot be seen afterwards: nothing in
  // a listing shows that a file has been shared.
  const what = browsing.remote + ":" + entry.path;
  if (!confirm("Make a public link to\n\n" + what +
               "\n\n? Anybody who has the link will be able to open it.")) {
    return;
  }

  run("Public link for " + what, remoteUrl("link"), { path: entry.path }, (d) =>
    d.url
      ? '<p><input type="text" readonly value="' + esc(d.url) + '"></p>' +
        '<p class="muted">Select it and copy. Anybody with this address can open the file.</p>'
      : "<pre>" + esc(d.output || "rclone gave no address") + "</pre>");
});

$("remote-cleanup").addEventListener("click", () => {
  // The window's own wording, because what this does is not obvious from its
  // name and differs from one backend to another.
  if (!confirm("Clean up remote " + browsing.remote + ": ?\n\n" +
               "This empties the trash, or deletes old versions of files, " +
               "depending on what the backend supports. Files removed this " +
               "way cannot be recovered.\n\n" +
               "It does not touch files you can currently see.")) {
    return;
  }
  run("Cleanup of " + browsing.remote + ":", remoteUrl("cleanup"), {}, (d) =>
    '<p>Done.</p><pre>' + esc(d.output || "") + "</pre>");
});

$("remote-dedupe").addEventListener("click", () => {
  const path = subject();
  const what = browsing.remote + ":" + path;
  const modes = [
    ["skip", "skip — remove identical copies, leave the rest"],
    ["newest", "newest — keep the newest of each"],
    ["oldest", "oldest — keep the oldest of each"],
    ["largest", "largest — keep the largest of each"],
    ["smallest", "smallest — keep the smallest of each"],
    ["first", "first — keep the first of each"],
    ["rename", "rename — keep them all, under different names"],
  ];

  showResult("Duplicates under " + what,
    '<p class="muted">Files that share a name in one folder. Try it as a dry ' +
    "run first: it says what would be done and does none of it.</p>" +
    '<p><select id="dedupe-mode">' + modes.map((m) =>
      '<option value="' + m[0] + '">' + esc(m[1]) + "</option>").join("") +
    "</select></p>" +
    '<p><button id="dedupe-dry" class="small">Dry run</button> ' +
    '<button id="dedupe-run" class="danger small">Run for real</button></p>');

  const go = (dryRun) => {
    const mode = $("dedupe-mode").value;
    if (!dryRun && !confirm("Run dedupe (" + mode + ") on\n\n" + what +
                            "\n\nfor real? Files it removes do not come back.")) {
      return;
    }
    run((dryRun ? "Dry run: duplicates under " : "Duplicates under ") + what,
        remoteUrl("dedupe"), { path: path, mode: mode, dryRun: dryRun }, (d) =>
      "<pre>" + esc(d.output || "nothing was reported") + "</pre>")
      .then((done) => { if (done && !dryRun) browse(browsing.remote, browsing.path); });
  };
  $("dedupe-dry").addEventListener("click", () => go(true));
  $("dedupe-run").addEventListener("click", () => go(false));
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
  if (d.stream) return reopenStream(d.stream);
  if (d.remote) return browse(d.remote, "");
  if (d.go !== undefined) return browse(browsing.remote, d.go);

  if (d.run && (await command("POST", task(d.run, "/run")))) return refresh();
  if (d.dry && (await command("POST", task(d.dry, "/run?dryRun=1")))) return refresh();
  if (d.enqueue && (await command("POST", task(d.enqueue, "/enqueue")))) return refresh();

  if (d.newTask) return openTransfer(d.newTask, {});
  if (d.edit) {
    const found = state.tasks.find((t) => t.id === d.edit);
    if (found) openTransfer(found.type, found, { id: found.id, title: "Edit task — " + found.name });
    return;
  }
  if (d.forget) {
    const found = state.tasks.find((t) => t.id === d.forget);
    if (!confirm("Delete the task " + (found ? found.name : d.forget) + "?\n\n" +
                 "Schedules that run it will have nothing to run.")) return;
    if (await command("DELETE", task(d.forget, ""))) refresh();
    return;
  }

  if (d.stop) {
    const job = state.jobs.find((j) => j.requestId === d.stop);
    const verb = job && job.kind === "mount" ? "Unmount " : "Stop ";
    if (!confirm(verb + (job ? job.info : d.stop) + "?")) return;
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
