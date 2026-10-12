// A stand-in for rclone, for the tests. Not installed and not registered as a
// test: it exists so that the code which runs rclone can be exercised without
// one, and without a network.
//
// It answers two of rclone's commands and ignores every argument it does not
// need:
//
//   listremotes  one remote called "fake", in the "name: type" form
//                "listremotes --long" produces
//   lsjson       a directory of FAKE_RCLONE_ENTRIES entries (default 5),
//                every other one a folder
//   mkdir,       succeed, or fail with rclone's kind of message when
//   moveto,      FAKE_RCLONE_FAIL is set
//   purge,
//   deletefile
//   size, about, answer as rclone does, in JSON when asked with --json;
//   tree         fail when FAKE_RCLONE_FAIL is set
//   copy, sync,  does nothing for FAKE_RCLONE_SLEEP_MS (default 0) and then
//   move         exits 0 -- a transfer that is still going when asked about
//
// Added when a listing of 7,103 real files showed that the API answered from
// one path only and left the other -- more entries than were asked for --
// hanging forever. The bug needed a folder bigger than the limit to appear,
// and this is how a test gets one.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char *argv[]) {
  const char *command = argc > 1 ? argv[1] : "";

  if (std::strcmp(command, "listremotes") == 0) {
    std::printf("fake: fake\n");
    return 0;
  }

  if (std::strcmp(command, "lsjson") == 0) {
    int count = 5;
    if (const char *asked = std::getenv("FAKE_RCLONE_ENTRIES")) {
      count = std::atoi(asked);
    }

    // Printed a piece at a time on purpose: the parser is fed in chunks by
    // whatever arrives on the pipe, and a listing that only works when the
    // whole array lands at once is a listing that works only for small
    // folders.
    std::printf("[\n");
    for (int i = 0; i < count; ++i) {
      std::printf("%s{\"Path\":\"item-%d\",\"Name\":\"item-%d\","
                  "\"Size\":%d,\"IsDir\":%s,"
                  "\"ModTime\":\"2026-08-31T12:00:00.000000000Z\"}",
                  i == 0 ? "" : ",\n", i, i, i * 100,
                  (i % 2 == 0) ? "true" : "false");
      std::fflush(stdout);
    }
    std::printf("\n]\n");
    return 0;
  }

  if (std::strcmp(command, "copy") == 0 || std::strcmp(command, "sync") == 0 ||
      std::strcmp(command, "move") == 0 || std::strcmp(command, "mount") == 0) {
    int ms = 0;
    if (const char *asked = std::getenv("FAKE_RCLONE_SLEEP_MS")) {
      ms = std::atoi(asked);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 0;
  }

  // Questions. Each answers as rclone does, and "has" says whether a flag
  // was among the arguments.
  const auto has = [argc, argv](const char *flag) {
    for (int i = 2; i < argc; ++i) {
      if (std::strcmp(argv[i], flag) == 0) {
        return true;
      }
    }
    return false;
  };

  if (std::strcmp(command, "size") == 0 || std::strcmp(command, "about") == 0 ||
      std::strcmp(command, "tree") == 0) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "2026/10/12 12:00:00 CRITICAL: Failed to create "
                           "file system: didn't find section in config file\n");
      return 1;
    }
    if (std::strcmp(command, "size") == 0) {
      if (has("--json")) {
        std::printf("{\"count\":3,\"bytes\":1536,\"sizeless\":0}\n");
      } else {
        std::printf("Total objects: 3\nTotal size: 1.500 KiB (1536 Byte)\n");
      }
    } else if (std::strcmp(command, "about") == 0) {
      // Over several lines, as rclone prints it, with a notice first.
      std::printf("2026/10/12 12:00:00 NOTICE: something said beforehand\n");
      std::printf("{\n\t\"total\": 1000,\n\t\"used\": 400,\n\t\"free\": 600\n}\n");
    } else {
      // Indentation is the whole of what a tree says.
      std::printf("/\n|-- films\n|   `-- 2026\n`-- music\n    `-- live\n");
    }
    return 0;
  }

  if (std::strcmp(command, "link") == 0 || std::strcmp(command, "cleanup") == 0 ||
      std::strcmp(command, "dedupe") == 0) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "2026/10/12 12:00:00 ERROR : this backend does "
                           "not support that\n");
      return 1;
    }
    if (std::strcmp(command, "link") == 0) {
      std::printf("https://example.test/share/abc123\n");
    } else if (std::strcmp(command, "dedupe") == 0) {
      std::printf("2026/10/12 12:00:00 NOTICE: a.txt: Found 2 files with "
                  "duplicate names\n");
      if (has("--dry-run")) {
        std::printf("2026/10/12 12:00:00 NOTICE: a.txt: Skipped delete as "
                    "--dry-run is set\n");
      }
    }
    return 0;
  }

  // A listing for export, with a notice among the lines as rclone prints
  // them, and a name with a quote in it.
  if (std::strcmp(command, "lsl") == 0) {
    std::printf("   300000 2026-10-12 14:16:41.398206300 films/a.mkv\n");
    std::printf("2026/10/12 12:00:00 NOTICE: something said in the middle\n");
    std::printf("     5000 2026-10-11 09:00:00.000000000 b \"quoted\".txt\n");
    std::printf("        0 2026-01-01 00:00:00.000000000 empty\n");
    return 0;
  }

  // A comparison. Differs, and ends in an error as rclone does, when
  // FAKE_RCLONE_DIFFER is set; fails outright when FAKE_RCLONE_FAIL is.
  if (std::strcmp(command, "check") == 0) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "2026/10/12 12:00:00 CRITICAL: Failed to create "
                           "file system: didn't find section in config file\n");
      return 1;
    }
    if (std::getenv("FAKE_RCLONE_DIFFER") != nullptr) {
      std::printf("2026/10/12 12:00:00 ERROR : zzz.txt: file not in fake:a\n");
      std::printf("2026/10/12 12:00:00 NOTICE: fake:b: 1 differences found\n");
      std::printf("2026/10/12 12:00:00 NOTICE: fake:b: 2 matching files\n");
      std::printf("2026/10/12 12:00:00 NOTICE: Failed to check: 1 differences "
                  "found\n");
      return 1;
    }
    std::printf("2026/10/12 12:00:00 NOTICE: fake:b: 0 differences found\n");
    std::printf("2026/10/12 12:00:00 NOTICE: fake:b: 3 matching files\n");
    return 0;
  }

  // Making a folder. Refuses when told to, with a line of the kind rclone
  // prints, so the code that has to pass that line on has one to pass.
  if (std::strcmp(command, "mkdir") == 0 ||
      std::strcmp(command, "moveto") == 0 ||
      std::strcmp(command, "purge") == 0 ||
      std::strcmp(command, "deletefile") == 0) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "2026/10/12 12:00:00 ERROR : Attempt 1/3 failed "
                           "with 1 errors and: directory not found\n");
      return 3;
    }
    return 0;
  }

  if (std::strcmp(command, "version") == 0) {
    std::printf("rclone v1.99.0-fake\n");
    return 0;
  }

  return 0;
}
