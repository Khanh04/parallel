#include "type_mapping.h"

// Static member initialization
const std::map<std::string, TypeMapper::TypeInfo> TypeMapper::TYPE_MAP = {
    // Basic C++ types
    {"int",          TypeInfo("MPI_INT", "0")},
    {"double",       TypeInfo("MPI_DOUBLE", "0.0")},
    {"float",        TypeInfo("MPI_FLOAT", "0.0f")},
    {"bool",         TypeInfo("MPI_C_BOOL", "false")},
    {"char",         TypeInfo("MPI_CHAR", "'\\0'")},
    {"long",         TypeInfo("MPI_LONG", "0L")},
    {"unsigned int", TypeInfo("MPI_UNSIGNED", "0U")},
    {"long long",    TypeInfo("MPI_LONG_LONG", "0LL")},
    
    // Common STL types that need special handling
    {"std::string",  TypeInfo("", "\"\"", false, true)},
    
    // Auto types (unsupported for MPI)
    {"auto",         TypeInfo("", "0", false, false)}
};

const std::set<std::string> TypeMapper::UNSUPPORTED_PATTERNS = {
    "std::chrono",
    "__enable_if_is_duration",
    "::"  // Any type with scope resolution (except std::string)
};

std::string TypeMapper::normalizeType(const std::string& cppType) {
    if (cppType == "_Bool") {
        return "bool";
    }
    return cppType;
}

std::string TypeMapper::getMPIDatatype(const std::string& cppType) {
    std::string normalizedType = normalizeType(cppType);
    
    // Check main type map first
    auto it = TYPE_MAP.find(normalizedType);
    if (it != TYPE_MAP.end()) {
        return it->second.isSupported ? it->second.mpiType : "";
    }
    
    // Handle special types not in main map
    TypeInfo specialType = handleSpecialType(normalizedType);
    if (!specialType.mpiType.empty() || !specialType.isSupported) {
        return specialType.mpiType;
    }
    
    // Check for unsupported patterns
    if (matchesUnsupportedPattern(normalizedType)) {
        return ""; // Unsupported type
    }
    
    // Default fallback for simple unrecognized types
    return "MPI_INT";
}

std::string TypeMapper::getDefaultValue(const std::string& cppType) {
    std::string normalizedType = normalizeType(cppType);
    
    // Check main type map first
    auto it = TYPE_MAP.find(normalizedType);
    if (it != TYPE_MAP.end()) {
        return it->second.defaultValue;
    }
    
    // Handle special types not in main map
    TypeInfo specialType = handleSpecialType(normalizedType);
    if (!specialType.defaultValue.empty()) {
        return specialType.defaultValue;
    }
    
    // Special handling for STL types
    if (normalizedType.find("std::") != std::string::npos || normalizedType.find("vector") != std::string::npos) {
        if (normalizedType.find("std::chrono") != std::string::npos) {
            return "std::chrono::system_clock::time_point{}";
        }
        // Default construction for other STL types
        return normalizedType + "{}";
    }
    
    // Default fallback
    return "0";
}

bool TypeMapper::isTypeSupported(const std::string& cppType) {
    std::string normalizedType = normalizeType(cppType);
    
    auto it = TYPE_MAP.find(normalizedType);
    if (it != TYPE_MAP.end()) {
        return it->second.isSupported;
    }
    
    // Check for unsupported patterns
    return !matchesUnsupportedPattern(normalizedType);
}

bool TypeMapper::isSTLType(const std::string& cppType) {
    std::string normalizedType = normalizeType(cppType);
    
    auto it = TYPE_MAP.find(normalizedType);
    if (it != TYPE_MAP.end()) {
        return it->second.isSTLType;
    }
    
    return normalizedType.find("std::") != std::string::npos;
}

TypeMapper::TypeInfo TypeMapper::handleSpecialType(const std::string& normalizedType) {
    // Handle specific special cases that need custom logic
    if (normalizedType.find("std::chrono") != std::string::npos) {
        return TypeInfo("", "std::chrono::system_clock::time_point{}", false, true);
    }
    
    // Return empty TypeInfo for no special handling needed
    return TypeInfo();
}

bool TypeMapper::matchesUnsupportedPattern(const std::string& type) {
    // Skip std::string as it's specifically handled
    if (type == "std::string") {
        return false;
    }
    
    for (const std::string& pattern : UNSUPPORTED_PATTERNS) {
        if (type.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}