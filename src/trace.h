#pragma once

#include <iostream>

#ifdef TRACE
#define TRACE_LOG(msg) std::cerr << "[TRACE] " << __FILE__ << ":" << __LINE__ << " " << msg << std::endl
#else
#define TRACE_LOG(msg)
#endif
