// args.cpp -- CLI argument parsing and usage display.

#include "args.h"
#include "config.h"
#include "util.h"

#include <cstdlib>
#include <getopt.h>

static void printUsage()
{
  printErr(
      "Usage: {} [options]\n\n"
      "  -o, --output <path>       Output file path "
      "(default: ./Recording_YYYYMMDDTHHMMSS.caf)\n"
      "  -d, --device <#|name|uid> Input device by number, name, or UID "
      "(default: system default)\n"
      "  -l, --list-devices        List available input devices and exit\n"
      "  -M, --monitor             Play input through default output device\n"
      "  -t, --test                Test mode: capture audio without "
      "writing a file\n"
      "  -D, --max-duration <min>  Stop after N minutes "
      "(default: unlimited)\n"
      "  -q, --quiet               Suppress level meter display\n"
      "  -v, --verbose             Print diagnostic output "
      "(buffer health, overruns)\n"
      "  -h, --help                Show this help\n"
      "  -V, --version             Show version and exit\n",
      RA_PROGRAM_NAME);
}

Args parseArgs(int a_argc, char* a_argv[])
{
  Args args;

  static const struct option long_options[] = {
      {"output", required_argument, nullptr, 'o'},
      {"device", required_argument, nullptr, 'd'},
      {"list-devices", no_argument, nullptr, 'l'},
      {"monitor", no_argument, nullptr, 'M'},
      {"test", no_argument, nullptr, 't'},
      {"max-duration", required_argument, nullptr, 'D'},
      {"quiet", no_argument, nullptr, 'q'},
      {"verbose", no_argument, nullptr, 'v'},
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'V'},
      {nullptr, 0, nullptr, 0}};

  int opt;
  while ((opt = getopt_long(a_argc, a_argv, "o:d:D:lMtqvhV", long_options,
                            nullptr)) != -1)
  {
    switch (opt)
    {
      case 'o':
        args.output_path_ = optarg;
        break;
      case 'd':
        args.device_selector_ = optarg;
        break;
      case 'l':
        args.list_devices_ = true;
        break;
      case 'M':
        args.monitor_ = true;
        break;
      case 't':
        args.test_ = true;
        break;
      case 'D':
        {
          char* end = nullptr;
          long val = strtol(optarg, &end, 10);
          if (end == optarg || *end != '\0' || val <= 0)
          {
            printErr(
                "Error: invalid duration '{}'. "
                "Expected a positive integer (minutes).\n",
                optarg);
            exit(1);
          }
          args.max_duration_min_ = static_cast<int>(val);
          break;
        }
      case 'q':
        args.quiet_ = true;
        break;
      case 'v':
        args.verbose_ = true;
        break;
      case 'h':
        printUsage();
        exit(0);
      case 'V':
        printErr(
            "ra - macOS Audio Recorder - v{}\n"
            "  Records 24-bit audio to CAF files\n"
            "  " RA_COPYRIGHT "\n",
            RA_VERSION);
        exit(0);
      default:
        printUsage();
        exit(1);
    }
  }

  if (args.quiet_ && args.verbose_)
  {
    printErr("Error: --quiet and --verbose are mutually exclusive.\n");
    exit(1);
  }

  return args;
}
