#pragma once

// Wrapper to avoid naming conflicts between 2-op and 4-op instruments
namespace Instruments2OP {
  #include <midi_instruments.h>
}

namespace Instruments4OP {
  #include <midi_instruments_4op.h>
}

namespace Drums {
  #include <midi_drums.h>
}
