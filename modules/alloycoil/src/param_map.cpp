#include "io/param_map.h"
#include "param_manifest.generated.h"

// Alloy Coil has no discrete parameters and no parameter needs a side effect, so
// this file is only the table definitions. Lookup and dispatch are the
// platform's - see platform/src/param_map.cpp.

const ParamDescriptor *const kParamTable = kParamManifest;
const uint8_t                kParamCount = kParamManifestCount;

const EnumParamDescriptor *const kEnumTable = kEnumManifest;
const uint8_t                    kEnumCount = kEnumManifestCount;
