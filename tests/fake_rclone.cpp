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
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
  const char *command = argc > 1 ? argv[1] : "";

  // config providers  one backend, "fake", with a sensitive option, a
  //                   password, an ordinary one and an advanced one
  // config dump       the one remote, secrets and all, as rclone gives it
  if (std::strcmp(command, "config") == 0 && argc > 2) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "Failed to load config file: not this time\n");
      return 1;
    }
    if (std::strcmp(argv[2], "providers") == 0) {
      std::printf(
          "[{\"Name\":\"fake\",\"Description\":\"A stand-in\",\"Prefix\":\"fake\","
          "\"Options\":["
          "{\"Name\":\"access_token\",\"Help\":\"Access Token Cookie\",\"Type\":\"string\","
          "\"DefaultStr\":\"\",\"Required\":true,\"IsPassword\":false,\"Sensitive\":true,"
          "\"Advanced\":false,\"Hide\":0,\"Exclusive\":false,\"Examples\":null},"
          "{\"Name\":\"chunk_size\",\"Help\":\"Chunk Size\\nLonger words.\",\"Type\":\"SizeSuffix\","
          "\"DefaultStr\":\"512Mi\",\"Required\":false,\"IsPassword\":false,\"Sensitive\":false,"
          "\"Advanced\":false,\"Hide\":0,\"Exclusive\":false,"
          "\"Examples\":[{\"Value\":\"1Gi\",\"Help\":\"bigger\"}]},"
          "{\"Name\":\"pass\",\"Help\":\"Password\",\"Type\":\"string\",\"DefaultStr\":\"\","
          "\"Required\":false,\"IsPassword\":true,\"Sensitive\":false,\"Advanced\":false,"
          "\"Hide\":0,\"Exclusive\":false,\"Examples\":null},"
          "{\"Name\":\"page_size\",\"Help\":\"Page size\",\"Type\":\"int\",\"DefaultStr\":\"500\","
          "\"Required\":false,\"IsPassword\":false,\"Sensitive\":false,\"Advanced\":true,"
          "\"Hide\":0,\"Exclusive\":false,\"Examples\":null}]},"
          "{\"Name\":\"fakeoauth\",\"Description\":\"Signs in with a browser\",\"Prefix\":\"fakeoauth\","
          "\"Options\":[{\"Name\":\"client_id\",\"Help\":\"Client id\",\"Type\":\"string\","
          "\"Sensitive\":true},{\"Name\":\"token\",\"Help\":\"OAuth token\",\"Type\":\"string\","
          "\"Sensitive\":true,\"Advanced\":true}]}]\n");
      return 0;
    }
    // create and delete: succeed, and write down what they were given when
    // FAKE_RCLONE_RECORD names a file, so a test can see the command.
    if (std::strcmp(argv[2], "create") == 0 || std::strcmp(argv[2], "delete") == 0 ||
        std::strcmp(argv[2], "update") == 0) {
      if (const char *record = std::getenv("FAKE_RCLONE_RECORD")) {
        if (FILE *file = std::fopen(record, "ab")) {
          for (int i = 2; i < argc; ++i) {
            std::fprintf(file, "%s%s", i > 2 ? "\x1f" : "", argv[i]);
          }
          std::fprintf(file, "\n");
          std::fclose(file);
        }
      }
      // A remote that signs in with a browser asks two things after it is
      // made, the way drive does: whether to refresh the token, and then a
      // question of its own.
      bool hasToken = false;
      const char *state = "";
      for (int i = 3; i < argc; ++i) {
        hasToken = hasToken || std::strncmp(argv[i], "token=", 6) == 0;
        if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
          state = argv[i + 1];
        }
      }
      if (std::strcmp(state, "*oauth-confirm,pick,,") == 0) {
        std::printf("{\"State\":\"pick_done\",\"Option\":{\"Name\":\"config_pick\","
                    "\"Help\":\"Which one?\",\"DefaultStr\":\"a\",\"Exclusive\":true,"
                    "\"Examples\":[{\"Value\":\"a\",\"Help\":\"the first\"},"
                    "{\"Value\":\"b\",\"Help\":\"the second\"}]},\"Error\":\"\"}\n");
        return 0;
      }
      if (hasToken && *state == '\0') {
        std::printf("{\"State\":\"*oauth-confirm,pick,,\",\"Option\":{\"Name\":"
                    "\"config_refresh_token\",\"Help\":\"Already have a token - refresh?\","
                    "\"DefaultStr\":\"true\"},\"Error\":\"\"}\n");
        return 0;
      }
      std::printf("{\"State\":\"\",\"Option\":null,\"Error\":\"\",\"Result\":\"\"}\n");
      return 0;
    }
    if (std::strcmp(argv[2], "dump") == 0) {
      std::printf("{\"fake\":{\"type\":\"fake\",\"access_token\":\"SECRET-TOKEN\","
                  "\"chunk_size\":\"1Gi\",\"pass\":\"OBSCURED-PASS\","
                  "\"page_size\":\"10\",\"client_secret\":\"ALSO-SECRET\","
                  "\"region\":\"home\"}}\n");
      return 0;
    }
  }

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

  // A file's bytes: FAKE_RCLONE_CAT, or a line saying so, as far as --count.
  if (std::strcmp(command, "cat") == 0) {
    if (std::getenv("FAKE_RCLONE_FAIL") != nullptr) {
      std::fprintf(stderr, "ERROR : object not found\n");
      return 3;
    }
    const char *given = std::getenv("FAKE_RCLONE_CAT");
    std::string text = given != nullptr ? given : "hello from cat\n";
    for (int i = 2; i + 1 < argc; ++i) {
      if (std::strcmp(argv[i], "--count") == 0) {
        const size_t count = size_t(std::atoll(argv[i + 1]));
        if (text.size() > count) {
          text.resize(count);
        }
      }
    }
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }

  if (std::strcmp(command, "copy") == 0 || std::strcmp(command, "sync") == 0 ||
      std::strcmp(command, "move") == 0 || std::strcmp(command, "mount") == 0 ||
      std::strcmp(command, "bisync") == 0 ||
      std::strcmp(command, "serve") == 0) {
    // What a serve was given, and the password it found in its environment
    // -- which is where it has to come from, never the arguments.
    if (std::strcmp(command, "serve") == 0) {
      if (const char *record = std::getenv("FAKE_RCLONE_RECORD")) {
        if (FILE *file = std::fopen(record, "ab")) {
          for (int i = 1; i < argc; ++i) {
            std::fprintf(file, "%s%s", i > 1 ? "\x1f" : "", argv[i]);
          }
          const char *pass = std::getenv("RCLONE_PASS");
          const char *config = std::getenv("RCLONE_CONFIG_PASS");
          std::fprintf(file, "\x1fRCLONE_PASS=%s\x1fRCLONE_CONFIG_PASS=%s\n",
                       pass != nullptr ? pass : "", config != nullptr ? config : "");
          std::fclose(file);
        }
      }
    }
    // A remote control to talk to, when a test stands one up: the line rclone
    // prints when it is serving one.
    if (const char *port = std::getenv("FAKE_RCLONE_RC_PORT")) {
      std::printf("2026/10/19 12:00:00 NOTICE: Serving remote control on "
                  "http://127.0.0.1:%s/\n", port);
      std::fflush(stdout);
    }
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
