// Copyright 2018-2024 Simul.co

#pragma once

#if PLATFORM_WINDOWS
# if PLATFORM_64BITS
#  define TELEPORT_PLATFORM TEXT("Win64")
#  define TELEPORT_LIBAVSTREAM TEXT("libavstream.dll")
# else
#  define TELEPORT_PLATFORM TEXT("Win32")
#  define TELEPORT_LIBAVSTREAM TEXT("libavstream.dll")
# endif
#else
# error "Teleport: Unsupported platform!"
#endif
