// ra -- Long-duration audio recorder for macOS, targeting Logic Pro import.
// Outputs 24-bit CAF files via CoreAudio with a lock-free ring buffer
// architecture.

#include "args.h"
#include "device.h"
#include "extend_caf.h"
#include "session.h"
#include "util.h"

#include <variant>

int main(int argc, char* argv[])
{
  auto args = parseArgs(argc, argv);

  if (auto* extend = std::get_if<ExtendArgs>(&args))
  {
    auto result = extendCafFile(*extend);
    if (!result)
    {
      printErr(result.error());
      return 1;
    }
    printErr("{} expanded to {} minutes. New expanded file {}.\n",
             result->resolved_input, result->pad_to_minutes,
             result->resolved_output);
    return 0;
  }

  auto& rec = std::get<RecordingArgs>(args);
  auto input_devices = getInputDevices();
  if (!input_devices)
  {
    printErr(input_devices.error());
    return 1;
  }

  if (rec.list_devices)
  {
    printDeviceList(*input_devices);
    return input_devices->empty() ? 1 : 0;
  }

  return runSession(rec, *input_devices);
}
