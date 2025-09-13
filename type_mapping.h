#ifndef TYPE_MAPPING_H
#define TYPE_MAPPING_H

#include <string>
#include <map>
#include <set>

/**
 * Utility class for handling C++ to MPI type mappings and default value generation.
 * Eliminates code duplication across type-related operations.
 */
class TypeMapper {
public:
    struct TypeInfo {
        std::string mpiType;
        std::string defaultValue;
        bool isSupported;
        bool isSTLType;
        
        TypeInfo(const std::string& mpi = "", const std::string& def = "", 
                bool supported = true, bool stl = false) 
            : mpiType(mpi), defaultValue(def), isSupported(supported), isSTLType(stl) {}
    };

private:
    static const std::map<std::string, TypeInfo> TYPE_MAP;
    static const std::set<std::string> UNSUPPORTED_PATTERNS;

public:
    /**
     * Normalize C++ type names (e.g., "_Bool" -> "bool")
     */
    static std::string normalizeType(const std::string& cppType);
    
    /**
     * Get MPI datatype for a C++ type
     * Returns empty string for unsupported types
     */
    static std::string getMPIDatatype(const std::string& cppType);
    
    /**
     * Get default value for a C++ type
     */
    static std::string getDefaultValue(const std::string& cppType);
    
    /**
     * Check if a type is supported for MPI operations
     */
    static bool isTypeSupported(const std::string& cppType);
    
    /**
     * Check if a type is an STL type
     */
    static bool isSTLType(const std::string& cppType);

private:
    /**
     * Handle special cases for complex types not in the main map
     */
    static TypeInfo handleSpecialType(const std::string& normalizedType);
    
    /**
     * Check if type matches unsupported patterns
     */
    static bool matchesUnsupportedPattern(const std::string& type);
};

#endif // TYPE_MAPPING_H