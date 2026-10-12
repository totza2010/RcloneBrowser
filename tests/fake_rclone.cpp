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
//
// Added when a listing of 7,103 real files showed that the API answered from
// one path only and left the other -- more entries than were asked for --
// hanging forever. The bug needed a folder bigger than the limit to appear,
// and this is how a test gets one.

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

  if (std::strcmp(command, "version") == 0) {
    std::printf("rclone v1.99.0-fake\n");
    return 0;
  }

  return 0;
}
