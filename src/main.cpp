// ra -- Long-duration audio recorder for macOS, targeting Logic Pro import.
// Outputs 24-bit CAF files via CoreAudio with a lock-free ring buffer
// architecture.

#include "args.h"
#include "device.h"
#include "session.h"

int main(int argc, char* argv[])
{
  auto args = parseArgs(argc, argv);
  auto input_devices = getInputDevices();

  if (args.list_devices_)
  {
    printDeviceList(input_devices);
    return input_devices.empty() ? 1 : 0;
  }

  return runSession(args, input_devices);
}
