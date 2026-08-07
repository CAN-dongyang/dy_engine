#pragma once

// Tracy's scoped zones are RAII objects: entering a scope starts a CPU zone and
// destroying the object at scope exit closes it, including on early returns.
#if defined(DY_TRACY_ENABLED)
#include <tracy/Tracy.hpp>
#define DY_PROFILE_CPU_ZONE() ZoneScoped
#define DY_PROFILE_CPU_ZONE_NAMED(name) ZoneScopedN(name)
#else
#define DY_PROFILE_CPU_ZONE() ((void)0)
#define DY_PROFILE_CPU_ZONE_NAMED(name) ((void)sizeof(name))
#endif
