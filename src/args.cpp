// args.cpp -- CLI argument parsing and usage display.

#include "args.h"
#include "config.h"
#include "git_version.h"
#include "util.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <format>
#include <getopt.h>
#include <string>
#include <system_error>
#include <utility>

static std::expected<int, std::string> parsePositiveMinutes(
    const char* a_optarg, const char* a_what)
{
  const char* begin = a_optarg;
  const char* end = begin + std::strlen(begin);
  int val = 0;
  auto [ptr, ec] = std::from_chars(begin, end, val);
  if (ec != std::errc {} || ptr != end || val <= 0)
  {
    return std::unexpected(std::format(
        "Error: invalid {} '{}'. Expected a positive integer (minutes).\n",
        a_what, a_optarg));
  }
  return val;
}

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
      "      --extend <input.caf>  Extend mode: pad an existing ra recording "
      "(requires --pad-to and -o)\n"
      "      --pad-to <min>        Target declared duration in minutes "
      "(extend mode)\n"
      "  -h, --help                Show this help\n"
      "  -V, --version             Show version and exit\n",
      RA_PROGRAM_NAME);
}

Args parseArgs(int a_argc, char* a_argv[])
{
  RecordingArgs rec;
  std::optional<std::string> extend_caf_file;
  std::optional<int> pad_to_min;

  constexpr int k_opt_extend = 256;
  constexpr int k_opt_pad_to = 257;

  static const struct option long_options[] = {
      {"output", required_argument, nullptr, 'o'},
      {"device", required_argument, nullptr, 'd'},
      {"list-devices", no_argument, nullptr, 'l'},
      {"monitor", no_argument, nullptr, 'M'},
      {"test", no_argument, nullptr, 't'},
      {"max-duration", required_argument, nullptr, 'D'},
      {"quiet", no_argument, nullptr, 'q'},
      {"verbose", no_argument, nullptr, 'v'},
      {"extend", required_argument, nullptr, k_opt_extend},
      {"pad-to", required_argument, nullptr, k_opt_pad_to},
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
        rec.output_path = optarg;
        break;
      case 'd':
        rec.device_selector = optarg;
        break;
      case 'l':
        rec.list_devices = true;
        break;
      case 'M':
        rec.monitor = true;
        break;
      case 't':
        rec.test = true;
        break;
      case 'D':
        {
          auto result = parsePositiveMinutes(optarg, "duration");
          if (!result)
          {
            printErr("{}", result.error());
            exit(1);
          }
          rec.max_duration_min = *result;
          break;
        }
      case 'q':
        rec.quiet = true;
        break;
      case 'v':
        rec.verbose = true;
        break;
      case k_opt_extend:
        extend_caf_file = optarg;
        break;
      case k_opt_pad_to:
        {
          auto result = parsePositiveMinutes(optarg, "--pad-to value");
          if (!result)
          {
            printErr("{}", result.error());
            exit(1);
          }
          pad_to_min = *result;
          break;
        }
      case 'h':
        printUsage();
        exit(0);
      case 'V':
        printErr(
            "ra - macOS Audio Recorder - v{}" RA_GIT_HASH_DISPLAY "\n"
            "  Records 24-bit audio to CAF files\n"
            "  " RA_COPYRIGHT "\n",
            RA_VERSION);
        exit(0);
      default:
        printUsage();
        exit(1);
    }
  }

  bool any_extend = extend_caf_file || pad_to_min;
  if (any_extend)
  {
    // output_path is shared with extend mode, so exclude it from the
    // "recording flags are all at default" check. Comparing against a
    // default-constructed RecordingArgs via the defaulted operator== covers
    // every recording field structurally -- adding a new RecordingArgs field
    // is automatically rejected here without touching this predicate.
    RecordingArgs without_output = rec;
    without_output.output_path.reset();
    bool recording_clean = (without_output == RecordingArgs {});
    bool have_required = extend_caf_file && pad_to_min && rec.output_path;
    bool no_positional = (optind == a_argc);
    if (!have_required || !recording_clean || !no_positional)
    {
      printErr(
          "Error: --extend requires exactly --extend <input.caf> "
          "--pad-to <min> -o <output.caf> and no other arguments.\n");
      exit(1);
    }
    return ExtendArgs {
        .extend_caf_file = std::move(*extend_caf_file),
        .pad_to_min = *pad_to_min,
        .output_path = std::move(*rec.output_path),
    };
  }

  if (rec.quiet && rec.verbose)
  {
    printErr("Error: --quiet and --verbose are mutually exclusive.\n");
    exit(1);
  }

  return rec;
}
