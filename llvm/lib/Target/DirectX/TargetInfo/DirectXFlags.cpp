
#include "DirectXFlags.h"

using namespace llvm;

cl::opt<bool> llvm::EnableDirectXGlobalIsel(
    "enable-directx-global-isel",
    cl::desc("Enable the DirectX GlobalIsel target"), cl::Optional,
    cl::init(false));

cl::opt<bool> llvm::EnableDirectXGlobalIselASMPrinter(
    "enable-directx-global-isel-asm-printer",
    cl::desc("Enable the DirectX GlobalIsel asm printer"), cl::Optional,
    cl::init(false));
